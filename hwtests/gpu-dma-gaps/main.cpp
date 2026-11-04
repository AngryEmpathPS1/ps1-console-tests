/*
 * gpu/gpu-dma-gaps - a GPU upload's blocks one by one: the gap the GPU leaves before asking for each next block, by the rectangle's
 * row length, read off a timer read N single cycles after CHCR2's store (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-rowlen and -rowx on the SCPH-5501: at 4-word blocks a row that ends with a block costs 0.84 / 1.6 / 1.0 /
 * 0.4 / 0.1 cycles at rows of 4 / 8 / 12 / 16 / 32 words, a row ending mid-block from 9 words up nothing, short rows ~0.8 a block
 * wherever they end - totals over hundreds of blocks, the same at every place in VRAM (STATUS "gpu-dma-rowx"). This times each
 * block's own gap, so the model can be fitted to where in the row pattern the console waits, not to the sums.
 *
 * WHAT IT DOES. gpu-dma-knee's instrument on a short upload: SyncMode 1, GP0(A0h) into a rectangle of R words a row at (512, 0),
 * K blocks of B words (B 4: K 8, R 2, 3, 4, 5, 6, 8, 12, 16, 32; B 16: K 4, R 8, 16, 32, 64; the rectangle a row longer when R does
 * not divide K x B, so no word is left for GP0 as a command). After CHCR2's store, N single cycles (a jump into a run of nops,
 * N = 0..191), then a raw Timer 2 read: a read the bus is not free for waits for the block holding it. Each try starts at one
 * phase of Timer 2's count mod 256 - the DRAM refresh's grid (hwtests/runner: a refresh every 256 cycles, on Timer 2's grid at the
 * system clock) - so every try of a phase meets the refresh at the same point of the upload: four phases 64 apart, 8 tries a
 * value of N each, interrupts off; for each N the value most tries read.
 *
 * READ IT LIKE THIS. One line a cell and phase: B R K P (off: tries away from the common value, all N), then read-after-N for
 * N = 0..191. Flat runs are blocks (the
 * read waits for the block's end, its level the end's time); a rise of a cycle an N is a gap (the bus free); where a rise meets
 * the next flat is that block's start. A phase whose refresh falls in the upload shows one gap longer; the others are the upload
 * alone. The gaps after the blocks, cell against cell and machine against machine, are the measurement. No assertions.
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

static uint32_t buf[256] __attribute__((aligned(16)));
static uint32_t bsNow, rowNow, kNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
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

struct Cell { uint16_t b, r, k; };
static const Cell CELLS[] = {
    { 4, 2, 8 }, { 4, 3, 8 }, { 4, 4, 8 }, { 4, 5, 8 }, { 4, 6, 8 }, { 4, 8, 8 }, { 4, 12, 8 }, { 4, 16, 8 }, { 4, 32, 8 },
    { 16, 8, 4 }, { 16, 16, 4 }, { 16, 32, 4 }, { 16, 64, 4 },
};
static uint16_t lo[NMAX], vals[NMAX][8];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-gaps\n");
    printf("A short SyncMode 1 GPU upload, a timer read N single cycles after CHCR2's store: the fewest of 16 by N, N = 0..191. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 256; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 4; rowNow = 4; kNow = 8; setup(); one(0, 0); settle(); setup(); one(NMAX - 1, 0); settle();   /* cache it */
    for (unsigned c = 0; c < sizeof CELLS / sizeof CELLS[0]; c++) {
        bsNow = CELLS[c].b; rowNow = CELLS[c].r; kNow = CELLS[c].k;
        for (uint32_t p = 0; p < 256; p += 64) {
            for (int t = 0; t < 8; t++) for (int n = 0; n < NMAX; n++) {
                setup();
                waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
                vals[n][t] = one((uint32_t)n, p);
                settle();
            }
            /* each N's value most tries read (ties to the later): a try whose block met a scan-out window starts it late, and the
             * fewest would show that one try's free read; off counts the tries away from it */
            unsigned off = 0;
            for (int n = 0; n < NMAX; n++) {
                int best = 0, bestN = 0;
                for (int i = 0; i < 8; i++) { int c = 0; for (int j = 0; j < 8; j++) c += vals[n][j] == vals[n][i]; if (c > bestN || (c == bestN && vals[n][i] > vals[n][best])) { best = i; bestN = c; } }
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
    printf("\ngpu/gpu-dma-gaps: Done.\n");
    for (;;) {}   /* park after Done. */
}
