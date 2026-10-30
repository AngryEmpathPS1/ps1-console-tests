/*
 * gpu/gpu-scan6 - the scan-out's toll on CPU->VRAM and VRAM->CPU transfers, by DMA and by the CPU (2026-10-13 night).
 *
 * WHY. gpu-scan3 / gpu-scan4 / gpu-scan5 on the SCPH-7501 gave every drawing primitive's toll on active lines (a semi
 * rectangle all of the fetches' steal, a copy ~0.45 of it, lines nothing). Uploads (GP0(A0h)) and downloads (GP0(C0h))
 * are not drawing: their pace may be the DMA's or the CPU's, not the frame buffer's - psx-spx: "the timings do mainly rely
 * on the transfer rate on CPU side". A model leaves them alone unless measured.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. In 320x240 (two refills a line) and 640x240 24-bit (seven),
 * range 16..256: each transfer four times 20,000 cycles after the VBlank IRQ (the blanking) and four 200,000 after
 * (active lines), the fastest of each, timed from the first GP0 word to the end (DMA2's CHCR bit 24 clear, or the CPU's
 * last word, then GPUSTAT bits 28+26 for an upload). An upload of 128x64 (4,096 words) by DMA2 in 16-word blocks, and of
 * 64x32 (1,024 words) by the CPU; a download of 128x64 by DMA2 (16-word blocks, after GPUSTAT bit 27) and of 64x32 by
 * the CPU. Everything at (512,300), outside the picture.
 *
 * READ IT LIKE THIS. Toll in tenths of a percent: 0 - the transfer is paced by the DMA or the CPU, not the VRAM; near a
 * drawing primitive's - the VRAM. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scan6 --frames=3000 --quiet=3000`.
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
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t buf[4096];

static void toTime(uint32_t x) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < x) {} }
}
/* kind 0: DMA upload 128x64, 1: CPU upload 64x32, 2: DMA download 128x64, 3: CPU download 64x32 */
static uint32_t one(int kind, uint32_t x) {
    const int w = kind & 1 ? 64 : 128, h = kind & 1 ? 32 : 64, words = w * h / 2;
    write32(GP1, 0x04000000u | (kind == 0 ? 2u : kind == 2 ? 3u : 0u));
    toTime(x);
    const uint32_t t0 = now();
    if (kind < 2) {
        write32(GP0, 0xA0000000u); write32(GP0, xy(512, 300)); write32(GP0, xy(w, h));
        if (kind == 0) {
            write32(D2_MADR, (uint32_t)buf & 0x1FFFFF);
            write32(D2_BCR, ((uint32_t)(words / 16) << 16) | 16u);
            write32(D2_CHCR, 0x01000201u);
            while ((read32(D2_CHCR) & 0x01000000u) && now() - t0 < 40000000u) {}
        } else for (int i = 0; i < words; i++) write32(GP0, buf[i]);
        for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
    }
    write32(GP0, 0xC0000000u); write32(GP0, xy(512, 300)); write32(GP0, xy(w, h));
    while (!(read32(GP1) & 0x08000000u) && now() - t0 < 40000000u) {}
    if (kind == 2) {
        write32(D2_MADR, (uint32_t)buf & 0x1FFFFF);
        write32(D2_BCR, ((uint32_t)(words / 16) << 16) | 16u);
        write32(D2_CHCR, 0x01000200u);
        while ((read32(D2_CHCR) & 0x01000000u) && now() - t0 < 40000000u) {}
    } else for (int i = 0; i < words; i++) buf[i] = read32(GP0);
    return now() - t0;
}

int main() {
    printf("\ngpu/gpu-scan6\n");
    printf("Uploads and downloads in the vertical blanking (20,000 cycles after the VBlank IRQ) and on active lines (200,000):\n");
    printf("fastest of four each, and the active lines' toll in tenths of a percent. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 4096; i++) buf[i] = 0x7FFF7FFFu;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    static const char* const KN[4] = { "upload DMA 128x64", "upload CPU 64x32", "download DMA 128x64", "download CPU 64x32" };
    static const char* const MN[2] = { "320x240", "640x240 24-bit" };
    static const uint32_t MODE[2] = { 0x01, 0x13 };
    for (int m = 0; m < 2; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int k = 0; k < 4; k++) {
            uint32_t b = 0xFFFFFFFFu, a = 0xFFFFFFFFu;
            for (int r = 0; r < 4; r++) { const uint32_t t1 = one(k, 20000); b = t1 < b ? t1 : b; const uint32_t t2 = one(k, 200000); a = t2 < a ? t2 : a; }
            write32(GP1, 0x04000000u);
            ExitCriticalSection();
            printf("  %-15s %-20s blank %6u  active %6u  toll %d\n", MN[m], KN[k], (unsigned)b, (unsigned)a, b ? ((int)a - (int)b) * 1000 / (int)b : 0);
            EnterCriticalSection();
        }
    }
    write32(GP1, 0x04000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-scan6: Done.\n");
    for (;;) VSync(0);
}
