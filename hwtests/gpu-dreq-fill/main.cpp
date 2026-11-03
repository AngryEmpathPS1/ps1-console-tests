/*
 * gpu/gpu-dreq-fill - the GPU's DMA request in mode 2 against the FIFO's fill (2026-11-03 night, for the SCPH-5501).
 *
 * WHY. psx-spx: in GP1(04h) mode 2 the DMA request (GPUSTAT.25) is "drq when FIFO empty", the same as GPUSTAT.28 ("Write FIFO
 * empty"), with its own doubt: "XXX basically: Bit28 seems to be just 'FIFO Empty'". This core follows it, and parks a 4-word-block
 * upload at every line-start fetch where the SCPH-5501's never waits (hwtests/gpu-dma-progress); a request that holds with a word or
 * two in the FIFO fits most of the DMA tests better (experiments/_mkdreq2.js, STATUS "gpu-dma-progress"). This reads it.
 *
 * WHAT IT DOES. For n = 0..18: GP1(01h), the GPU idle, GP1(04h) = 2, then a fill (GP0(02h), 1024 x 511: hundreds of thousands of
 * cycles busy), then n more words to GP0 - GP0(A0h) and its two parameters, then data words - so n words wait in the FIFO behind the
 * fill; then GPUSTAT read twice, 40 cycles apart. Printed per n: bits 25, 26, 27 and 28 of each read. Then GP1(01h) ends the fill.
 *
 * READ IT LIKE THIS. Bit 25 (mode 2) at n = 1, 2, 3... says how many queued words the request tolerates; bit 28 beside it says
 * whether it is bit 28. No assertions. `node hwtests/run.js gpu-dreq-fill --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static void idle() { for (unsigned k = 0; k < 200000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {} }
static unsigned bits(uint32_t s) { return ((s >> 25) & 1) * 1000 + ((s >> 26) & 1) * 100 + ((s >> 27) & 1) * 10 + ((s >> 28) & 1); }

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dreq-fill\n");
    printf("GP1(04h) = 2, a long fill, then n words queued behind it: GPUSTAT bits 25 26 27 28, read twice 40 cycles apart. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    printf("  n   read 1 (25 26 27 28)   read 2\n");
    for (unsigned n = 0; n <= 18; n++) {
        write32(GP1, 0x01000000u);
        idle();
        write32(GP1, 0x04000002u);
        waitCycles(200);
        write32(GP0, 0x02101010u); write32(GP0, 0x00000000u); write32(GP0, (511u << 16) | 1024u);   /* the fill */
        waitCycles(100);
        for (unsigned k = 0; k < n; k++) {
            const uint32_t w = k == 0 ? 0xA0000000u : k == 1 ? ((256u << 16) | 512u) : k == 2 ? ((64u << 16) | 64u) : 0x7FFF7FFFu ^ k;
            write32(GP0, w);
        }
        const uint32_t s1 = read32(GP1);
        waitCycles(40);
        const uint32_t s2 = read32(GP1);
        printf(" %2u   %04u                   %04u\n", n, bits(s1), bits(s2));
        write32(GP1, 0x01000000u);   /* end the fill and drop the queued words */
        write32(GP1, 0x04000000u);
    }
    idle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dreq-fill: Done.\n");
    for (;;) {}
}
