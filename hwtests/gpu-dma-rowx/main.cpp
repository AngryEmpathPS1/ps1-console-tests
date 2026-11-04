/*
 * gpu/gpu-dma-rowx - does a GPU DMA upload's row cost move with WHERE its rectangle sits in VRAM: its x by 2 to 16 pixels, its y
 * (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-rowlen on the SCPH-5501, every rectangle at (512, 0): at 4-word blocks a row that ends with a block costs
 * ~0.84 cycles at rows of 4 words, ~1.6 at 8, ~1.0 at 12, ~0.36 at 16, nothing at 32, and the cost is the same run to run over 16
 * line offsets (spreads of 10-34 cycles over hundreds of row ends: not the scan-out's windows, which would scatter it). A cost that
 * falls from 8 to 16 to 32 words is not a per-word write. psx-spx has the GPU's VRAM copies and fills going by 16-pixel units; if its
 * CPU->VRAM writes go by aligned units too, a row's cost follows where its ends fall against them, and moving x moves it (STATUS
 * "gpu-dma-rowlen").
 *
 * WHAT IT DOES. gpu-dma-rowlen's cells with the rectangle moved. Interrupts off, no disc. Per cell, N words (the first multiple of
 * both the block and the row of at least 2,000, at most 511 rows) by GP0(A0h) into a rectangle of R words a row at (X, Y), GP1(04h)
 * = 2, SyncMode 1 at blocks of B. 16 tries at hblank-locked offsets (40 + 131 t), timed as gpu-dma-rows2: a CHCR2 poll for a floor,
 * then QUIET - a cached spin from 400 under the poll's fewest, 4 cycles more each time, then Timer 2 and one CHCR2 read, until it
 * reads done. Cells: blocks of 4, rows of 4, 8, 12, 16 at X 512 + 0, 2, 4, 8, 12 (and 16 for rows of 8); rows of 8 at Y 1, 2, 256;
 * rows of 32 at X 512 and 520 (the floor); blocks of 16, rows of 8, 16, 32 at X 512 and 520.
 *
 * READ IT LIKE THIS. B R X Y N blocks rows ends: the block size, the row's words, the rectangle's place, the words, the blocks, the
 * rows, the row ends that fall on a block's end. Then the quiet first-done's fewest, mean and most, and its mean a block: down one
 * R, the cost a block by where the rectangle is. No assertions. `node hwtests/run.js gpu-dma-rowx --ntsc --frames=20000 --quiet=3000`.
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
static uint32_t bsNow, rowNow, nNow, xNow, yNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer */
    settle();
    write32(GP0, 0xA0000000u); write32(GP0, (yNow << 16) | xNow); write32(GP0, ((nNow / rowNow) << 16) | (rowNow * 2));
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
struct Cell { uint16_t b, r, x, y; };
static const Cell CELLS[] = {
    { 4, 4, 512, 0 }, { 4, 4, 514, 0 }, { 4, 4, 516, 0 }, { 4, 4, 520, 0 }, { 4, 4, 524, 0 },
    { 4, 8, 512, 0 }, { 4, 8, 514, 0 }, { 4, 8, 516, 0 }, { 4, 8, 520, 0 }, { 4, 8, 524, 0 }, { 4, 8, 528, 0 },
    { 4, 12, 512, 0 }, { 4, 12, 516, 0 }, { 4, 12, 520, 0 }, { 4, 12, 524, 0 },
    { 4, 16, 512, 0 }, { 4, 16, 516, 0 }, { 4, 16, 520, 0 }, { 4, 16, 524, 0 },
    { 4, 8, 512, 1 }, { 4, 8, 512, 2 }, { 4, 8, 512, 256 },
    { 4, 32, 512, 0 }, { 4, 32, 520, 0 },
    { 16, 8, 512, 0 }, { 16, 8, 520, 0 }, { 16, 16, 512, 0 }, { 16, 16, 520, 0 }, { 16, 32, 512, 0 }, { 16, 32, 520, 0 },
};
int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-rowx\n");
    printf("SyncMode 1 GPU DMA uploads by where the rectangle sits in VRAM (its x by 2-16 pixels, its y), timed QUIET. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);   /* Timer 1 counts hblanks */
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 4224; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 16; rowNow = 32; nNow = 2048; xNow = 512; yNow = 0; setup(); pollOne(); settle(); setup(); { bool d; quietOne(10, &d); } settle();   /* cache them */
    printf("  B    R    X    Y     N blocks  rows  ends    quiet done: fewest     mean   most   a block   poll fewest   lost\n");
    for (unsigned c = 0; c < sizeof CELLS / sizeof CELLS[0]; c++) {
        bsNow = CELLS[c].b; rowNow = CELLS[c].r; xNow = CELLS[c].x; yNow = CELLS[c].y;
        const uint32_t l = bsNow / gcd(bsNow, rowNow) * rowNow;
        nNow = (2000 + l - 1) / l * l;
        while (nNow / rowNow > 511 - yNow) nNow -= l;   /* GP0(A0h)'s height is 9 bits (gpu-dma-rowlen), and no row past VRAM's last */
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
        printf("%3u  %3u  %3u  %3u  %4u   %4u  %4u  %4u             %5u  %5u.%u  %5u   %3u.%02u         %5u   %4u\n", (unsigned)bsNow, (unsigned)rowNow,
               (unsigned)xNow, (unsigned)yNow, (unsigned)nNow, blocks, (unsigned)(nNow / rowNow), (unsigned)ends, (unsigned)qLo, qm / 10, qm % 10, (unsigned)qHi,
               pb / 100, pb % 100, (unsigned)pLo, (unsigned)lost);
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rowx: Done.\n");
    for (;;) {}   /* park after Done. */
}
