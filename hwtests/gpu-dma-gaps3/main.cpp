/*
 * gpu/gpu-dma-gaps3 - gpu-dma-gaps2's instrument at 8-word blocks: the gaps between blocks once the upload has run for a while, rows
 * of 8, 16, 24, 32, 48 and 64 words (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-a0rowdisp on the second SCPH-5501: 8-word SyncMode 1 blocks pay ~1.65 cycles a row start at every row length from 16
 * to 64 words, 16-word blocks ~1.0; this emulator pays 8-word blocks 1.7 at 16-word rows and 0.3 at 32 - the same total from 16 to
 * 64, put in another place - so dma/chopping's 64x64 rectangle (32-word rows) reads ~1% fast. gaps2 never ran 8-word blocks. Block
 * by block, this says where the console's cycles sit (STATUS "gpu-a0rowdisp").
 *
 * WHAT IT DOES. gpu-dma-gaps2's, with its cells: blocks of 8, K 48 (384 words), W 560 (about block 30 on), rows of 8, 16, 24, 32, 48,
 * 64. Each try started on the DRAM refresh's 256-cycle grid at one of four phases, 8 tries a value of N, interrupts off; for each N
 * the value most tries read.
 *
 * READ IT LIKE THIS. One line a cell and phase: B R K P (off), then read-after-(W + N) for N = 0..191 - flats are blocks, rises are
 * gaps (experiments/intake/gapread.js reads both). No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
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
/* n turns of a cached three-instruction loop: no bus access, the same cycles every time */
static inline void spin(uint32_t n) {
    __asm__ volatile(".set push\n.set noreorder\n1: addiu %0, %0, -1\nbnez %0, 1b\nnop\n.set pop\n" : "+r"(n));
}

static uint32_t buf[1024] __attribute__((aligned(16)));
static uint32_t bsNow, rowNow, kNow, wNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer (and a rectangle left short) */
    settle();
    const uint32_t words = kNow * bsNow, rows = (words + rowNow - 1) / rowNow;
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, (rows << 16) | (rowNow * 2));
    write32(GP1, 0x04000002u);   /* DMA direction CPU->VRAM */
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, (kNow << 16) | bsNow);
}
/* Start at Timer 2's count mod 256 = p, to the cycle (gpu-dma-gaps), then W's spin and N single cycles before the read */
static __attribute__((noinline, noclone)) uint16_t one(uint32_t n, uint32_t p) {
    for (unsigned k = 0; k < 4000; k++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d >= 6 && d < 56) break; }
    const uint32_t v = read16(T2_COUNT);
    sled((p - v - 6) & 0xFFu);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    spin(wNow / 3);
    sled(n);
    const uint16_t a1 = read16(T2_COUNT);
    return (uint16_t)(a1 - a0);
}

struct Cell { uint16_t b, r, k, w; };
static const Cell CELLS[] = {
    { 8, 8, 48, 560 }, { 8, 16, 48, 560 }, { 8, 24, 48, 560 }, { 8, 32, 48, 560 }, { 8, 48, 48, 560 }, { 8, 64, 48, 560 },
};
static uint16_t lo[NMAX], vals[NMAX][8];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-gaps3\n");
    printf("A long SyncMode 1 GPU upload, a timer read W + N single cycles after CHCR2's store: the value most of 8 read by N, N = 0..191. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 1024; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 8; rowNow = 8; kNow = 48; wNow = 560; setup(); one(0, 0); settle(); setup(); one(NMAX - 1, 0); settle();   /* cache it */
    for (unsigned c = 0; c < sizeof CELLS / sizeof CELLS[0]; c++) {
        bsNow = CELLS[c].b; rowNow = CELLS[c].r; kNow = CELLS[c].k; wNow = CELLS[c].w;
        for (uint32_t p = 0; p < 256; p += 64) {
            for (int t = 0; t < 8; t++) for (int n = 0; n < NMAX; n++) {
                setup();
                waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
                vals[n][t] = one((uint32_t)n, p);
                settle();
            }
            unsigned off = 0;
            for (int n = 0; n < NMAX; n++) {
                int best = 0, bestN = 0;
                for (int i = 0; i < 8; i++) { int c2 = 0; for (int j = 0; j < 8; j++) c2 += vals[n][j] == vals[n][i]; if (c2 > bestN || (c2 == bestN && vals[n][i] > vals[n][best])) { best = i; bestN = c2; } }
                lo[n] = vals[n][best]; off += 8 - bestN;
            }
            printf("%u %u %u %u (off %u):", (unsigned)bsNow, (unsigned)rowNow, (unsigned)kNow, (unsigned)p, off);
            for (int n = 0; n < NMAX; n++) printf(" %u", (unsigned)lo[n]);
            printf("\n");
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-gaps3: Done.\n");
    for (;;) {}   /* park after Done. */
}
