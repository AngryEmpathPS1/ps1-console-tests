/*
 * gpu/gpu-dma-rows - a GPU DMA upload's time against the rectangle's row length and the block size (2026-11-03 night, for the
 * SCPH-5501).
 *
 * WHY. experiments/intakesim.js fits hwtests/gpu-dma-quiet's eight block sizes (2,048 words into 64 x 64: rows of 32 words) with
 * 12-13 words of intake room and a 10-cycle buffer write, to 22 cycles RMS, and what is left goes with the row: blocks that straddle
 * a row's end (24, 48) read fast against the console, 32 slow. This core charges a row change A0_ROW (1 cycle on the new GPU),
 * fitted by gpu-a0row with the poll leak in (STATUS "gpu-dma-progress"). This times the row's cost directly.
 *
 * WHAT IT DOES. 2,048 words by GP0(A0h) into rectangles 32, 64, 128 and 256 pixels wide (rows of 16, 32, 64 and 128 words; 128, 64,
 * 32 and 16 rows), GP1(04h) = 2, SyncMode 1 at blocks of 16, 24, 32 and 48 (chopping's set-up: GP1(01h) and the GPU idle first),
 * 16 tries each at hblank-locked offsets, interrupts off, timed by a CHCR2 poll (gpu-dma-quiet: on the console a poll costs a
 * non-busy upload nothing and sees the end within a few cycles). Per width and block size: the fewest, mean and most cycles.
 *
 * READ IT LIKE THIS. Down a column (one block size), the row's cost: 128 rows against 16 at the same words. Each machine's table
 * against the other's. No assertions. `node hwtests/run.js gpu-dma-rows --ntsc --frames=6000 --quiet=3000`.
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

static uint32_t buf[2048] __attribute__((aligned(16)));
static uint32_t bsNow, wNow;

static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);
    settle();
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 512u); write32(GP0, ((4096u / wNow) << 16) | wNow);
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, ((2048u / bsNow) << 16) | bsNow);
}
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

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-rows\n");
    printf("2,048 words by SyncMode 1 GPU DMA into rectangles of four widths at four block sizes, timed by a CHCR2 poll. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 2048; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 16; wNow = 64; setup(); pollOne(); settle();   /* cache it */
    static const uint32_t WIDTHS[4] = { 32, 64, 128, 256 }, SIZES[4] = { 16, 24, 32, 48 };
    printf("width words/row  bs  blocks   fewest    mean    most\n");
    for (int w = 0; w < 4; w++) {
        wNow = WIDTHS[w];
        for (int s = 0; s < 4; s++) {
            bsNow = SIZES[s];
            uint32_t lo = 0xFFFF, hi = 0, sum = 0;
            for (int t = 0; t < 16; t++) {
                setup(); atLine(40u + (uint32_t)t * 131u);
                const uint16_t p = pollOne(); settle();
                if (p < lo) lo = p; if (p > hi) hi = p; sum += p;
            }
            const unsigned m = (unsigned)(sum * 10 / 16);
            printf("%5u  %8u %3u  %5u    %5u  %5u.%u  %5u\n", (unsigned)wNow, (unsigned)(wNow / 2), (unsigned)bsNow, (unsigned)(2048 / bsNow), (unsigned)lo, m / 10, m % 10, (unsigned)hi);
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rows: Done.\n");
    for (;;) {}
}
