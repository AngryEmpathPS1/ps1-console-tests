/*
 * gpu/gpu-dma-gapsd2 - does the GPU's reset (GP1(00h)) choose which of the old GPU's two block-timing states a run is in? (2026-11-05
 * evening, for the SCPH-1001)
 *
 * WHY. hwtests/gpu-dma-gapsd on the SCPH-1001, six runs: every row exact in one of two states (four runs one way, two the other - the
 * first block and the gap after it alike, the second block 6 cycles later in one and then earlier), the 16 tries of a run agreeing at
 * every place of the video. The six ran back to back under the resident runner, no console reset between them, so whatever chooses
 * the state is set again each run - and every test program begins with initVideo, whose ResetGraph resets the GPU (GP1(00h)). If the
 * reset latches the old GPU's internal phase against the CPU's clock, a reset before EVERY try splits one run's tries between the
 * two states.
 *
 * WHAT IT DOES. gpu-dma-gapsd's measurement (SyncMode 1, GP0(A0h) into a rectangle of R words a row at (512, 0), K blocks of B words, a
 * raw Timer 2 read N = 0..63 single cycles after CHCR2's store, each try started on the DRAM refresh's grid at place 0) for its first
 * cell, B 16 R 64 K 4. R0: 16 tries a N as gapsd takes them (no reset: this run's own state). R1: 32 tries a N, each after GP1(00h) and
 * the display set again (320x240, NTSC, 15-bit, the drawing environment as gapsd's start leaves it).
 *
 * READ IT LIKE THIS. R0 / R1, then for each N its value when all tries agree, else every value with its count (v*c, highest count
 * first). The 1001's two states at N 20-27: 67 68 69 70 100 100 101 102 and 73 74 75 76 95 96 97 98. R1's tries split between them
 * (about 2:1 if the runs' 4:2 is the reset's draw) if the reset chooses; else R1 reads as R0. No assertions - compare against your
 * emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-gapsd2 --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
#define NMAX 192
#define NRUN 64
/* n more cycles, one at a time: a jump into a run of 192 nops, n from its end (cached code, a cycle each) */
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 192\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n < NMAX ? n : NMAX - 1) : "t0", "t1");
}
static uint32_t buf[256] __attribute__((aligned(16)));
static uint32_t bsNow, rowNow, kNow;
/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
/* GP1(00h), then the display and drawing environment back as the start leaves them (320x240, NTSC, 15-bit, display on) */
static void gpuReset() {
    write32(GP1, 0x00000000u);
    write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
    write32(GP1, 0x08000001u);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x03000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer (and a rectangle left one word short) */
    settle();
    const uint32_t words = kNow * bsNow, rows = (words + rowNow - 1) / rowNow;
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, (rows << 16) | (rowNow * 2));
    write32(GP1, 0x04000002u);   /* DMA direction CPU->VRAM */
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, (kNow << 16) | bsNow);
}
/* Start at Timer 2's count mod 256 = p, to the cycle: spin to 6-56 cycles short of it, read the count once, slide the rest. The
 * read-to-store distance is fixed code, so every start lands at the same place on the refresh's grid. */
static __attribute__((noinline, noclone)) uint16_t one(uint32_t n, uint32_t p) {
    for (unsigned k = 0; k < 4000; k++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d >= 6 && d < 56) break; }
    const uint32_t v = read16(T2_COUNT);
    sled((p - v - 6) & 0xFFu);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    sled(n);
    const uint16_t a1 = read16(T2_COUNT);
    return (uint16_t)(a1 - a0);
}
#define TRIES 32
static uint16_t vals[NRUN][TRIES];
static void row(const char* tag, int tries) {
    printf("%s 16 64 4 0:", tag);
    for (int n = 0; n < NRUN; n++) {
        int same = 1; for (int i = 1; i < tries; i++) if (vals[n][i] != vals[n][0]) same = 0;
        if (same) { printf(" %u", (unsigned)vals[n][0]); continue; }
        uint16_t dv[TRIES]; int dc[TRIES], nd = 0;
        for (int i = 0; i < tries; i++) { int k = 0; while (k < nd && dv[k] != vals[n][i]) k++; if (k == nd) { dv[nd] = vals[n][i]; dc[nd] = 0; nd++; } dc[k]++; }
        for (int a = 0; a < nd; a++) for (int b = a + 1; b < nd; b++) if (dc[b] > dc[a]) { int tc = dc[a]; dc[a] = dc[b]; dc[b] = tc; uint16_t tv = dv[a]; dv[a] = dv[b]; dv[b] = tv; }
        printf(" ");
        for (int a = 0; a < nd; a++) printf("%s%u*%d", a ? "," : "", (unsigned)dv[a], dc[a]);
    }
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-gapsd2\n");
    printf("gpu-dma-gapsd's B 16 R 64 K 4 at refresh place 0, N = 0..63: R0 16 tries a N as gapsd (this run's own state), R1 32 tries a N "
           "each after GP1(00h). v when all agree, else v*count. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 256; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 4; rowNow = 4; kNow = 8; setup(); one(0, 0); settle(); setup(); one(NMAX - 1, 0); settle();   /* cache it */
    bsNow = 16; rowNow = 64; kNow = 4;
    for (int t = 0; t < 16; t++) for (int n = 0; n < NRUN; n++) {
        setup();
        waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
        vals[n][t] = one((uint32_t)n, 0);
        settle();
    }
    row("R0", 16);
    for (int t = 0; t < TRIES; t++) for (int n = 0; n < NRUN; n++) {
        gpuReset();
        setup();
        waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
        vals[n][t] = one((uint32_t)n, 0);
        settle();
    }
    row("R1", TRIES);
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-gapsd2: Done.\n");
    for (;;) {}   /* park after Done. */
}
