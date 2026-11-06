/*
 * gpu/gpu-dma-rdloop - gpu/bandwidth's vramToCpu loop with every phase timed (2026-11-06 small hours, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-rdlong on the second SCPH-5501: a 320x240 read at (0,0) by DMA2 in 16-word blocks takes 83,350
 * cycles (34.73 a block, whatever the place, width or display), but gpu/bandwidth's 400 of them read 15,754-15,760 hblanks
 * on every new-GPU console - 84,784 cycles a call - so its call spends ~1,430 cycles outside the transfer, where the code
 * (psn00bsdk's DrawPrim, ps1-tests' vramReadDMA) looks like a few hundred. DrawPrim starts with DrawSync(0), which after a
 * read waits for GPUSTAT bits 26 and 28: a GPU still busy after a read's last word would show only back to back, as
 * there, and never in gpu-dma-rdlong, which waits a frame between reads.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, the display 320x240 15-bit at (0,0). vramReadDMA as
 * bandwidth runs it, ten calls back to back (two untimed, eight timed), each phase timed with Timer 2: ds1 - DrawSync(0)
 * (the DMA direction set, so CHCR2's bit 24, then GPUSTAT bits 26 and 28); wr - GP1(04h)=0 and the three words of
 * GP0(C0h); ds2 - DrawSync(0) again (direction 0: GPUSTAT bit 28); b27 - GP1(04h)=3, then GPUSTAT bit 27; set - DPCR's
 * enable, CHCR2's bit 24, MADR2 and BCR2; dma - CHCR2's store to its bit 24 clear. And st - GPUSTAT straight after the
 * transfer. Three shapes: 320x240 by 16-word blocks (bandwidth's), 320x32 by 16, 320x240 by 8. A line per shape and phase:
 * R <shape> <phase> then the eight calls, and T <shape> the eight calls' total.
 *
 * READ IT LIKE THIS. The phases against each other and against your emulator's: where bandwidth's call spends what the
 * transfer does not. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdloop --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t big[38400] __attribute__((aligned(1024)));

#define LIM 40000000u
/* psn00bsdk's DrawSync(0): with a DMA direction set, CHCR2's bit 24 and then GPUSTAT bits 26 and 28; with none, bit 28 */
static void drawSync() {
    const uint32_t t0 = now();
    if ((read32(GP1) >> 29) & 3) {
        while ((read32(D2_CHCR) & 0x01000000u) && now() - t0 < LIM) {}
        while (((read32(GP1) >> 26) & 5) != 5 && now() - t0 < LIM) {}
    } else while (!((read32(GP1) >> 28) & 1) && now() - t0 < LIM) {}
}

struct Shape { const char* name; int h, bs; };
static const Shape SH[3] = { { "A 320x240 b16", 240, 16 }, { "B 320x32  b16", 32, 16 }, { "C 320x240 b8 ", 240, 8 } };
static uint32_t ph[3][7][8], tot[3];   /* [shape][ds1 wr ds2 b27 set dma st][call] */

static void call(const Shape& s, uint32_t* r) {
    const uint32_t t0 = now();
    drawSync();
    const uint32_t t1 = now();
    write32(GP1, 0x04000000u);
    write32(GP0, 0xC0000000u); write32(GP0, 0); write32(GP0, ((uint32_t)s.h << 16) | 320u);
    const uint32_t t2 = now();
    drawSync();
    const uint32_t t3 = now();
    write32(GP1, 0x04000003u);
    while (!(read32(GP1) & 0x08000000u) && now() - t3 < LIM) {}
    const uint32_t t4 = now();
    write32(DPCR, read32(DPCR) | 0x00000800u);
    while ((read32(D2_CHCR) & 0x01000000u) && now() - t4 < LIM) {}
    write32(D2_MADR, (uint32_t)big & 0xFFFFFCu);
    write32(D2_BCR, ((uint32_t)(320 * s.h / 2 / s.bs) << 16) | (uint32_t)s.bs);
    const uint32_t t5 = now();
    write32(D2_CHCR, 0x01000200u);
    while ((read32(D2_CHCR) & 0x01000000u) && now() - t5 < LIM) {}
    const uint32_t t6 = now();
    const uint32_t st = read32(GP1);
    if (r) { r[0] = t1 - t0; r[1] = t2 - t1; r[2] = t3 - t2; r[3] = t4 - t3; r[4] = t5 - t4; r[5] = t6 - t5; r[6] = st >> 24; }
}

int main() {
    printf("\ngpu/gpu-dma-rdloop\n");
    printf("R shape phase, eight calls back to back (cycles; st: GPUSTAT's top byte straight after the transfer, hex): ds1 DrawSync(0) after\n"
           "the last read, wr GP1(04h)=0 + GP0(C0h)'s three words, ds2 DrawSync(0), b27 GP1(04h)=3 to GPUSTAT bit 27, set DPCR + CHCR2 +\n"
           "MADR2 + BCR2, dma CHCR2's store to bit 24 clear. T shape: the eight calls' total. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 38400; i++) big[i] = 0;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int k = 0; k < 3; k++) {
        waitReady();
        call(SH[k], 0); call(SH[k], 0);
        const uint32_t a = now();
        for (int c = 0; c < 8; c++) { uint32_t r[7]; call(SH[k], r); for (int j = 0; j < 7; j++) ph[k][j][c] = r[j]; }
        tot[k] = now() - a;
        write32(GP1, 0x04000000u);
    }
    write32(GP1, 0x04000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    static const char* const PN[7] = { "ds1", "wr ", "ds2", "b27", "set", "dma", "st " };
    for (int k = 0; k < 3; k++) {
        for (int j = 0; j < 7; j++) {
            printf("R %s %s", SH[k].name, PN[j]);
            for (int c = 0; c < 8; c++) printf(j == 6 ? " %02x" : " %u", (unsigned)ph[k][j][c]);
            printf("\n");
        }
        printf("T %s %u\n", SH[k].name, (unsigned)tot[k]);
    }
    printf("\ngpu/gpu-dma-rdloop: Done.\n");
    for (;;) VSync(0);
}
