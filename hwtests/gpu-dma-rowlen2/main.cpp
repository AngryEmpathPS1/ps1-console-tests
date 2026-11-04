/*
 * gpu/gpu-dma-rowlen2 - gpu-dma-rowlen's instrument at 8-word blocks into rows of 8, 16, 24, 32, 48 and 64 words, and 16-word blocks into
 * rows of 16, 32 and 64: QUIET and polled (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-a0rowdisp (CHCR2 polled, the poll adding Timer 2 up) on the second SCPH-5501: 8-word blocks into 32-word rows read
 * ~40 cycles over this emulator, 16-word blocks ~25 at rows of 16 and 32 - and hwtests/gpu-dma-gaps3 (quiet, in the steady state) shows
 * every 8-word block's gap the same on both machines at every row length. So the difference is outside the steady state, or it is the
 * poll: this times the same uploads both ways, as gpu-dma-rowlen does (STATUS "gpu-dma-gaps3").
 *
 * WHAT IT DOES. gpu-dma-rowlen's, with these cells. READ IT LIKE THIS: as gpu-dma-rowlen. No assertions.
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
    printf("\ngpu/gpu-dma-rowlen2\n");
    printf("SyncMode 1 GPU DMA uploads by the rectangle's row length, at blocks of 4 and 16, timed QUIET. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);   /* Timer 1 counts hblanks */
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 4224; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 16; rowNow = 32; nNow = 2048; setup(); pollOne(); settle(); setup(); { bool d; quietOne(10, &d); } settle();   /* cache them */
    static const uint32_t SIZES[2] = { 8, 16 };
    static const uint32_t ROWS4[6] = { 8, 16, 24, 32, 48, 64 }, ROWS16[3] = { 16, 32, 64 };
    printf("  B    R    W     N blocks  rows  ends    quiet done: fewest     mean   most   a block   poll fewest   lost\n");
    for (int s = 0; s < 2; s++) {
        bsNow = SIZES[s];
        const uint32_t* ROWS = s ? ROWS16 : ROWS4;
        for (int r = 0; r < (s ? 3 : 6); r++) {
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
    printf("\ngpu/gpu-dma-rowlen2: Done.\n");
    for (;;) {}   /* park after Done. */
}
