/*
 * gpu/gpu-scanout - does the picture going out to the TV slow the drawing? The same primitives timed under four display
 * modes (2026-10-10).
 *
 * WHY. Two kinds of test disagree by ~5% about what a pixel costs on the same consoles: those that time ONE primitive
 * from the CPU and poll GPUSTAT (gpu-cost-sweep, gpu-tex-opaque, gpu-thin - run under whatever display Unirom left on)
 * and those that time a BATCH of 100-400 full-screen primitives against Timer 1's hblanks after setting a 320x240 15-bit
 * display (ps1-tests gpu/bandwidth, semi-cost). On this model, flat rectangles read +2.4% and +1.9% in the first kind
 * and -3.0% and -2.9% in the second; semi ones +6.2% / +6.7% against +1.6% / +1.0%; copies +2.7% against -3.4%. The GPU's
 * frame buffer is also read by the video output a line at a time - more pixels a line in a wider or 24-bit mode - and if
 * those reads take the VRAM's time from the drawing, the two kinds measure different machines. This model's drawing
 * does not depend on the display at all.
 *
 * WHAT IT DOES. gpu-cost-sweep's method (one primitive from the CPU, GPUSTAT polled, four runs, the fastest and slowest)
 * for a flat 256x256 rectangle, the same semi-transparent, a 256x256 fill, a 256x256 copy and a 15-bit textured 256x256
 * sprite, each drawn at (0,256) (the copy from (512,0), the sprite's texels at (512,0)) - drawing to the displayed area
 * allowed (E1h bit 10), so no mode skips a line - under: the display OFF (GP1(03h) = 1),
 * 320x240 15-bit, 640x480 interlaced 15-bit, 640x480 interlaced 24-bit, each set by GP1(08h) / (06h) / (07h) / (05h)
 * and given a frame to settle. The modes are put back as they were at the end.
 *
 * READ IT LIKE THIS. A row that grows from OFF to 320 to 640 to 24-bit is the scan-out taking VRAM time; a row that does
 * not is not. No assertions.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.70, every mode the same: flat rectangle 34,348, semi 54,109, fill
 * ~5,930, copy 86,709, sprite 177,593 cycles - its drawing does not see the display.
 * `node hwtests/run.js gpu-scanout`
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}

struct Tm { uint32_t t28, t26; };
static Tm drawOnce(const uint32_t* w, int n) {
    waitIdle();
    const uint32_t t0 = now();
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    Tm r = { 0, 0 };
    bool s28 = false;
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if (!s28 && (s & 0x10000000u)) { s28 = true; r.t28 = t; }
        if ((s & 0x04000000u) && s28) { r.t26 = t; break; }
        if (t > 40000000u) { r.t26 = 0xFFFFFFFFu; break; }
    }
    return r;
}

static void row(const uint32_t* w, int n) {
    uint32_t lo28 = 0xFFFFFFFFu, hi28 = 0, lo26 = 0xFFFFFFFFu, hi26 = 0;
    for (int k = 0; k < 4; k++) {
        const Tm t = drawOnce(w, n);
        lo28 = t.t28 < lo28 ? t.t28 : lo28; hi28 = t.t28 > hi28 ? t.t28 : hi28;
        lo26 = t.t26 < lo26 ? t.t26 : lo26; hi26 = t.t26 > hi26 ? t.t26 : hi26;
    }
    printf("   %2d words   bit 28 %7u..%-7u  bit 26 %8u..%u\n", n, (unsigned)lo28, (unsigned)hi28, (unsigned)lo26, (unsigned)hi26);
}

static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;                  /* (0, 480) */
static uint32_t tpage(int depth, int semi) { return 8u | ((uint32_t)semi << 5) | ((uint32_t)depth << 7); }   /* X 512 */
static void drawMode(int depth) { write32(GP0, 0xE1000000u | (1u << 10) | tpage(depth, 0)); }

static uint32_t w[16];

static void prims(const char* mode) {
    printf("%s:\n", mode);
    w[0] = 0x60204080u; w[1] = xy(0, 256); w[2] = xy(256, 256);
    printf("  flat rectangle 256x256 "); row(w, 3);
    w[0] = 0x62204080u;
    printf("  semi rectangle 256x256 "); row(w, 3);
    w[0] = 0x02203040u; w[1] = xy(0, 256); w[2] = xy(256, 256);
    printf("  fill 256x256           "); row(w, 3);
    w[0] = 0x80000000u; w[1] = xy(512, 0); w[2] = xy(0, 256); w[3] = xy(256, 256);
    printf("  copy 256x256           "); row(w, 4);
    w[0] = 0x64808080u; w[1] = xy(0, 256); w[2] = uv(0, 0, CLUT); w[3] = xy(256, 256);
    printf("  sprite 15-bit 256x256  "); row(w, 4);
}
static void settle() { const uint32_t t0 = now(); while (now() - t0 < 600000u) {} }

int main() {
    printf("\ngpu/gpu-scanout\n");
    printf("The same primitives under four display modes, written to GP0 by the CPU, then GPUSTAT polled: when bit 28 and\n");
    printf("bit 26 read set, cycles from the first word, fastest..slowest of four. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t stat0 = read32(GP1);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x01000000u);                                  /* reset the command buffer */
    write32(GP1, 0x04000000u);                                  /* no DMA */
    write32(GP0, 0xE2000000u);                                  /* no texture window */
    write32(GP0, 0xE3000000u);                                  /* drawing area (0, 0) .. */
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);           /* .. (1023, 511) */
    write32(GP0, 0xE5000000u);                                  /* no offset */
    write32(GP0, 0xE6000000u);                                  /* no mask */
    drawMode(2);
    write32(GP1, 0x05000000u);                                  /* display start (0,0) */
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);   /* horizontal range: 2560 dots of clock */
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);    /* vertical range: 240 lines */

    write32(GP1, 0x03000001u); settle(); prims("DISPLAY OFF");
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u); settle(); prims("320x240 15-bit");
    write32(GP1, 0x08000027u); write32(GP1, 0x03000000u); settle(); prims("640x480 interlaced 15-bit");
    write32(GP1, 0x08000037u); write32(GP1, 0x03000000u); settle(); prims("640x480 interlaced 24-bit");

    /* the display as it was: mode bits from GPUSTAT 16-22 and 14, enable from 23 */
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-scanout: Done.\n");
    for (;;) VSync(0);
}
