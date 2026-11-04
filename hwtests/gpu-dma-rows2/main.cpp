/*
 * gpu/gpu-dma-rows2 - gpu-dma-rows timed QUIET: a GPU DMA upload's own length against the rectangle's row length and the block size,
 * with the CPU off the bus (2026-11-03 night, for the SCPH-5501).
 *
 * WHY. gpu-dma-rows times its sixteen cells by a CHCR2 poll, and a poll's reading moves with where the program sits: the same upload
 * (64 x 64, blocks of 32) reads 2,882 by gpu-dma-quiet's poll on the SCPH-5501 and 2,930 by gpu-dma-rows' - and on this core 2,892
 * and 2,829, 50-60 cycles the other way (STATUS "The intake model in the core"). So its row costs are mixed with the poll's. This
 * times the same uploads the quiet way gpu-dma-quiet does, which there agrees with the poll on the console within its own spread.
 *
 * WHAT IT DOES. gpu-dma-rows' set-up and cells: 2,048 words by GP0(A0h) into rectangles 32, 64, 128 and 256 pixels wide, GP1(04h) = 2,
 * SyncMode 1 at blocks of 16, 24, 32 and 48, 16 tries each at hblank-locked offsets (40 + 131 t), interrupts off. POLL: CHCR2 read in
 * a loop. QUIET: from 400 under the poll's fewest, 4 cycles more each time, a cached spin (no bus access) and then Timer 2 and one
 * CHCR2 read, until it reads done. Per cell: the poll's fewest, mean and most; the quiet first-done's fewest, mean and most; the
 * last not-done's mean; tries never done.
 *
 * READ IT LIKE THIS. Down a column (one block size), the row's cost: 128 rows against 16 at the same words, by the quiet mean. Each
 * machine's table against the other's. No assertions. `node hwtests/run.js gpu-dma-rows2 --ntsc --frames=9000 --quiet=3000`.
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

static uint32_t buf[2048] __attribute__((aligned(16)));
static uint32_t bsNow, wNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer */
    settle();
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 512u); write32(GP0, ((4096u / wNow) << 16) | wNow);
    write32(GP1, 0x04000002u);   /* DMA direction CPU->VRAM */
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, ((2048u / bsNow) << 16) | bsNow);
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

#define TRIES 16
int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-rows2\n");
    printf("2,048 words by SyncMode 1 GPU DMA into rectangles of four widths at four block sizes, timed by a CHCR2 poll and QUIET. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);   /* Timer 1 counts hblanks */
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 2048; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 16; wNow = 64; setup(); pollOne(); settle(); setup(); { bool d; quietOne(10, &d); } settle();   /* cache them */
    static const uint32_t WIDTHS[4] = { 32, 64, 128, 256 }, SIZES[4] = { 16, 24, 32, 48 };
    printf("width words/row  bs  blocks   poll: fewest   mean   most    quiet done: fewest   mean   most   not done: mean   lost\n");
    for (int w = 0; w < 4; w++) {
        wNow = WIDTHS[w];
        for (int s = 0; s < 4; s++) {
            bsNow = SIZES[s];
            uint32_t pLo = 0xFFFF, pHi = 0, pSum = 0, qLo = 0xFFFF, qHi = 0, qSum = 0, nSum = 0, qN = 0, lost = 0;
            for (int t = 0; t < TRIES; t++) {
                setup(); atLine(40u + (uint32_t)t * 131u);
                const uint16_t p = pollOne(); settle();
                if (p < pLo) pLo = p; if (p > pHi) pHi = p; pSum += p;
            }
            for (int t = 0; t < TRIES; t++) {
                const uint32_t off = 40u + (uint32_t)t * 131u;
                uint32_t want = pLo > 400 ? pLo - 400 : 0, last = 0;
                bool found = false;
                for (int k = 0; k < 200 && !found; k++, want += 4) {
                    bool d = false;
                    setup(); atLine(off);
                    const uint16_t e = quietOne(want / 3 + 1, &d); settle();
                    if (d) { found = true; if (e < qLo) qLo = e; if (e > qHi) qHi = e; qSum += e; nSum += last; qN++; }
                    else last = e;
                }
                if (!found) lost++;
            }
            const unsigned pm = (unsigned)(pSum * 10 / TRIES), qm = qN ? (unsigned)(qSum * 10 / qN) : 0, nm = qN ? (unsigned)(nSum * 10 / qN) : 0;
            printf("%5u  %8u %3u  %5u        %5u %5u.%u  %5u              %5u %5u.%u  %5u            %5u.%u  %5u\n", (unsigned)wNow, (unsigned)(wNow / 2), (unsigned)bsNow,
                   (unsigned)(2048 / bsNow), (unsigned)pLo, pm / 10, pm % 10, (unsigned)pHi, (unsigned)qLo, qm / 10, qm % 10, (unsigned)qHi, nm / 10, nm % 10, (unsigned)lost);
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rows2: Done.\n");
    for (;;) {}   /* park after Done. */
}
