/*
 * gpu/gpu-copy2 - a VRAM-to-VRAM copy's cost by width, height and where its source and destination lie, in the blanking
 * (2026-10-13 night).
 *
 * WHY. This core's copy (GPU_COPY2_ROW 32.46 a row + max(5, 1.98 a pixel), v0.377.72's thin-copy fit) reads gpu-scan3's
 * and gpu-scanout2's 128x32 copies ~10% dear on the SCPH-7501 and the 5501 / 9001 (5,805-5,829 against 5,282-5,306, both
 * in the vertical blanking) while gpu/bandwidth's 320x240 copies (0,0 -> 320,0: the same VRAM rows) sit on the consoles,
 * and gpu-cost-sweep's 16x16 ... 1024x16 scatter either way. A sum of a row and a pixel term cannot do both: the cost is
 * shaped by something else - the width, or whether the source's and the destination's VRAM rows are the same (one DRAM
 * page) or not.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; every copy four times 20,000 cycles after the VBlank IRQ
 * (the blanking: no scan-out toll, gpu-vbl2), the fastest printed, timed from its first GP0 word to GPUSTAT bits 28+26.
 * Widths 16, 32, 64, 128, 256, 320, 512 at height 32, and heights 8, 64, 128 at width 128, each two ways: SAME rows -
 * source (0,256), destination (512,256); OTHER rows - source (0,256), destination (512,0). Then 128x32 from (768,256) to
 * (0,16) (gpu-scanout2's), from (512,0) to (0,16) (gpu-scan3's), and 320x32 from (0,0) to (320,0) (gpu/bandwidth's).
 *
 * READ IT LIKE THIS. Cycles by width at a fixed height: the pixel term and anything stepwise (DRAM pages); by height: the
 * row term; SAME against OTHER: whether the destination's rows sharing the source's page matter. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-copy2 --frames=4000 --quiet=4000`.
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

static uint32_t copy(int sx, int sy, int dx, int dy, int w, int h) {
    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < 4; r++) {
        waitReady();
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
        const uint32_t t0 = now();
        write32(GP0, 0x80000000u); write32(GP0, xy(sx, sy)); write32(GP0, xy(dx, dy)); write32(GP0, xy(w, h));
        uint32_t t;
        for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
        best = t < best ? t : best;
    }
    return best;
}

int main() {
    printf("\ngpu/gpu-copy2\n");
    printf("VRAM-to-VRAM copies in the vertical blanking, fastest of four, cycles from the first word. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE6000000u);
    static const int W[7] = { 16, 32, 64, 128, 256, 320, 512 };
    static const int H[3] = { 8, 64, 128 };
    ExitCriticalSection();
    printf("by width, height 32:   w    same rows   other rows\n");
    EnterCriticalSection();
    for (int i = 0; i < 7; i++) {
        const uint32_t s = copy(0, 256, 512, 256, W[i], 32), o = copy(0, 256, 512, 0, W[i], 32);
        ExitCriticalSection();
        printf("                     %4d   %8u   %8u\n", W[i], (unsigned)s, (unsigned)o);
        EnterCriticalSection();
    }
    ExitCriticalSection();
    printf("\nby height, width 128:  h    same rows   other rows\n");
    EnterCriticalSection();
    for (int i = 0; i < 3; i++) {
        const uint32_t s = copy(0, 256, 512, 256, 128, H[i]), o = copy(0, 256, 512, 0, 128, H[i]);
        ExitCriticalSection();
        printf("                     %4d   %8u   %8u\n", H[i], (unsigned)s, (unsigned)o);
        EnterCriticalSection();
    }
    const uint32_t a = copy(768, 256, 0, 16, 128, 32), b = copy(512, 0, 0, 16, 128, 32), c = copy(0, 0, 320, 0, 320, 32);
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu-scanout2's 128x32 (768,256)->(0,16): %u\ngpu-scan3's 128x32 (512,0)->(0,16): %u\ngpu/bandwidth's 320x32 (0,0)->(320,0): %u\n", (unsigned)a, (unsigned)b, (unsigned)c);
    printf("\ngpu/gpu-copy2: Done.\n");
    for (;;) VSync(0);
}
