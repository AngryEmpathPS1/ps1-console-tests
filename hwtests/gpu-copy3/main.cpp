/*
 * gpu/gpu-copy3 - a copy's scan-out toll by its width (2026-10-13 before dawn).
 *
 * WHY. hwtests/gpu-copy2 on the SCPH-7501 gave a copy's row in the vertical blanking as a curve in its width; gpu-thin's
 * thin copies, drawn back to back on the runner's active lines, read ~8.7% over that curve at 16 wide, where gpu-scan3 /
 * gpu-scan5's 128- and 256-wide copies paid 5-6% - so a copy's toll seems to depend on its width, and this core charges one
 * share (GPU_SCAN_K.copy 0.46) for all.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. In 512x240 (the runner's mode, three refills a line) and
 * 320x240 (two), range 16..256: copies of width 16 / 32 / 64 / 128 / 256 / 512 at height 32, source (0,256), destination
 * (512,256), four times 20,000 cycles after the VBlank IRQ (the blanking) and four 200,000 after (active lines), the
 * fastest of each, timed from the first GP0 word to GPUSTAT bits 28+26; the toll in tenths of a percent.
 *
 * READ IT LIKE THIS. The toll against the width, beside the same mode's semi rectangle (gpu-scan3: 512x240 10.0%, 320x240
 * 7.9%). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-copy3 --frames=4000 --quiet=4000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

static uint32_t copy(int w, int h, uint32_t x) {
    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < 4; r++) {
        waitReady();
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        { const uint32_t t0 = now(); while (now() - t0 < x) {} }
        const uint32_t t0 = now();
        write32(GP0, 0x80000000u); write32(GP0, xy(0, 256)); write32(GP0, xy(512, 256)); write32(GP0, xy(w, h));
        uint32_t t;
        for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
        best = t < best ? t : best;
    }
    return best;
}

int main() {
    printf("\ngpu/gpu-copy3\n");
    printf("Copies in the blanking (20,000 cycles after the VBlank IRQ) and on active lines (200,000), fastest of four, and\n");
    printf("the active lines' toll in tenths of a percent. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE6000000u);
    static const int W[6] = { 16, 32, 64, 128, 256, 512 };
    static const char* const MN[2] = { "512x240", "320x240" };
    static const uint32_t MODE[2] = { 0x02, 0x01 };
    for (int m = 0; m < 2; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int i = 0; i < 6; i++) {
            const uint32_t b = copy(W[i], 32, 20000), a = copy(W[i], 32, 200000);
            ExitCriticalSection();
            printf("  %-8s copy %3dx32  blank %6u  active %6u  toll %d\n", MN[m], W[i], (unsigned)b, (unsigned)a, b ? ((int)a - (int)b) * 1000 / (int)b : 0);
            EnterCriticalSection();
        }
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-copy3: Done.\n");
    for (;;) VSync(0);
}
