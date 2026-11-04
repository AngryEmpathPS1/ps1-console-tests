/*
 * gpu/gpu-dma-rowlen - what a GPU DMA upload pays a block by its rectangle's row length: rows of 2 to 16 and 32 words at blocks of 4,
 * 8 to 48 at blocks of 16, timed QUIET (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-rowend on the SCPH-5501: 4-word blocks into rows of 3, 4 and 8 words read 15.0 cycles a block, into rows of 5
 * 14.63, and gpu-dma-quiet's into rows of 32 14.22 - a narrow row costs a 4-word block ~0.8 whatever the number of changes, and 5 is
 * cheaper. This sweeps the row length to see what orders it (STATUS "gpu-dma-rowend").
 *
 * WHAT IT DOES. gpu-dma-rowend's, with other rows. Interrupts off, no disc. Per cell, N words (the first multiple of both the block
 * and the row of at least 2,000, at most 511 rows) by GP0(A0h) into a rectangle of R words a row at (512, 0), GP1(04h) = 2, SyncMode 1
 * at blocks of B: B 4 with R 2-16 and 32, B 16 with R 8, 12 ... 32 and 48. 16 tries at hblank-locked offsets
 * (40 + 131 t). Timed as gpu-dma-rows2 times: a CHCR2 poll for a floor, then QUIET - a cached spin from 400 under the poll's
 * fewest, 4 cycles more each time, then Timer 2 and one CHCR2 read, until it reads done.
 *
 * READ IT LIKE THIS. B R W N blocks rows ends: the block size, the row's words, the width in pixels, the words, the blocks, the rows,
 * the row ends that fall on a block's end. Then the quiet first-done's fewest, mean and most, and its mean a block: down one B, the
 * cost a block by the row's length. No assertions. `node hwtests/run.js gpu-dma-rowlen --ntsc --frames=20000 --quiet=3000`.
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
/* n turns of a cached three-instruction loop: no bus access */
static inline void spin(uint32_t n) {
    __asm__ volatile(".set push\n.set noreorder\n1: addiu %0, %0, -1\nbnez %0, 1b\nnop\n.set pop\n" : "+r"(n));
}

static uint32_t buf[4224] __attribute__((aligned(16)));
static uint32_t bsNow, rowNow, nNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer */
    settle();
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, ((nNow / rowNow) << 16) | (rowNow * 2));   /* (512, 0): 511 rows fit */
    write32(GP1, 0x04000002u);   /* DMA direction CPU->VRAM */
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, ((nNow / bsNow) << 16) | bsNow);
}
/* start at `off` cycles after an hblank edge */
static void atLine(uint32_t off) {
    const uint16_t h = read16(T1_COUNT);
    for (unsigned k = 0; k < 100000 && read16(T1_COUNT) == h; k++) {}
    waitCycles(off);
}
static __attribute__((noinline, noclone)) uint16_t pollOne() {
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    uint16_t a2; unsigned turns = 0;
    for (;;) { a2 = read16(T2_COUNT); if (!(read32(CHCR2) & 0x01000000u)) break; if (++turns > 20000) break; }
    return (uint16_t)(a2 - a0);
}
static __attribute__((noinline, noclone)) uint16_t quietOne(uint32_t n, bool* done) {
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    spin(n);
    const uint16_t a1 = read16(T2_COUNT);
    *done = !(read32(CHCR2) & 0x01000000u);
    return (uint16_t)(a1 - a0);
}
static uint32_t gcd(uint32_t a, uint32_t b) { while (b) { const uint32_t t = a % b; a = b; b = t; } return a; }

#define TRIES 16
int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-rowlen\n");
    printf("SyncMode 1 GPU DMA uploads by the rectangle's row length, at blocks of 4 and 16, timed QUIET. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);   /* Timer 1 counts hblanks */
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 4224; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 16; rowNow = 32; nNow = 2048; setup(); pollOne(); settle(); setup(); { bool d; quietOne(10, &d); } settle();   /* cache them */
    static const uint32_t SIZES[2] = { 4, 16 };
    static const uint32_t ROWS4[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32 }, ROWS16[8] = { 8, 12, 16, 20, 24, 28, 32, 48 };
    printf("  B    R    W     N blocks  rows  ends    quiet done: fewest     mean   most   a block   poll fewest   lost\n");
    for (int s = 0; s < 2; s++) {
        bsNow = SIZES[s];
        const uint32_t* ROWS = s ? ROWS16 : ROWS4;
        for (int r = 0; r < (s ? 8 : 16); r++) {
            rowNow = ROWS[r];
            const uint32_t l = bsNow / gcd(bsNow, rowNow) * rowNow;
            nNow = (2000 + l - 1) / l * l;
            /* GP0(A0h)'s height is 9 bits: 668 rows of 3 read as 156, and the 1,500 words left would reach GP0 as commands (the
             * emulator's first run of this test: every try lost) - at most 511 rows */
            while (nNow / rowNow > 511) nNow -= l;
            uint32_t ends = 0;
            for (uint32_t w = rowNow; w <= nNow; w += rowNow) if (w % bsNow == 0) ends++;
            uint32_t pLo = 0xFFFF, qLo = 0xFFFF, qHi = 0, qSum = 0, qN = 0, lost = 0;
            for (int t = 0; t < TRIES; t++) {
                setup(); atLine(40u + (uint32_t)t * 131u);
                const uint16_t p = pollOne(); settle();
                if (p < pLo) pLo = p;
            }
            for (int t = 0; t < TRIES; t++) {
                const uint32_t off = 40u + (uint32_t)t * 131u;
                uint32_t want = pLo > 400 ? pLo - 400 : 0;
                bool found = false;
                for (int k = 0; k < 200 && !found; k++, want += 4) {
                    bool d = false;
                    setup(); atLine(off);
                    const uint16_t e = quietOne(want / 3 + 1, &d); settle();
                    if (d) { found = true; if (e < qLo) qLo = e; if (e > qHi) qHi = e; qSum += e; qN++; }
                }
                if (!found) lost++;
            }
            const unsigned qm = qN ? (unsigned)(qSum * 10 / qN) : 0, blocks = nNow / bsNow, pb = qN ? (unsigned)(qSum * 100 / qN / blocks) : 0;
            printf("%3u  %3u  %3u  %4u   %4u  %4u  %4u             %5u  %5u.%u  %5u   %3u.%02u         %5u   %4u\n", (unsigned)bsNow, (unsigned)rowNow,
                   (unsigned)(rowNow * 2), (unsigned)nNow, blocks, (unsigned)(nNow / rowNow), (unsigned)ends, (unsigned)qLo, qm / 10, qm % 10, (unsigned)qHi,
                   pb / 100, pb % 100, (unsigned)pLo, (unsigned)lost);
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rowlen: Done.\n");
    for (;;) {}   /* park after Done. */
}
