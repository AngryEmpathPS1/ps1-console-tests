/*
 * gpu/gpu-scanout2 - the picture going out to the TV against the drawing, by WHERE in the frame and WHERE in VRAM the
 * drawing is (2026-10-12 night).
 *
 * WHY. hwtests/gpu-scanout timed five 256x256 primitives under four display modes and found (every run of every console
 * alike): the OLD GPU's drawing is the same under every mode to ~50 cycles (its VRAM dual-ported, the picture read through
 * its own port); the NEW GPU's moves by mode, the same on all ten runs, and not monotonically - 320x240 FASTER than the
 * display off for semi rectangles, copies and sprites, slower for flat rectangles. Each of those primitives spans ~20-120
 * scan lines from wherever the poll left the frame, so the rows mix lines that fetch with lines that do not. A model of
 * the scan-out's VRAM fetches against the drawing needs the cost of a FEW lines at a KNOWN line: this.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting HBLANKS and reset at VBlank (mode 0103h).
 * For each display mode - off (GP1(03h) = 1), 256x240, 320x240 and 640x480 interlaced 15-bit, 640x480 interlaced 24-bit,
 * the display at (0,0), vertical range 16..256 - and each start line L (Timer 1 = 4, 24, 64, 128, 200 after VBlank), four
 * short primitives written to GP0 the moment Timer 1 reaches L, then GPUSTAT polled for bit 26, cycles from the first
 * word; four tries each (one a frame), fastest..slowest, at three places: (0,16) inside every picture, (0,256) inside the
 * 640x480 picture only, (512,256) outside both:
 *   F  flat rectangle 256x32 (60h)      S  semi rectangle 256x32 (62h)
 *   T  15-bit sprite 64x64 (64h, texels at (768,0))      C  copy 128x32 (80h, from (768,256))
 * A row prints the place, then for each L its fastest..slowest.
 *
 * READ IT LIKE THIS. Against the display off: a line inside the vertical range that costs more is the scan-out taking the
 * bus; one in VBlank (L = 4) that costs the same is not. Place by place: whether the fetch's bank or page matters.
 * No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scanout2 --frames=6000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static uint32_t tpage(int depth) { return 12u | ((uint32_t)depth << 7); }   /* texture page X 768, Y 0 */

/* VBlank, then Timer 1 (hblanks since it) up to L; the words go out at once; bit 26 timed. Bounded everywhere. */
static uint32_t drawAt(const uint32_t* w, int n, int L) {
    waitIdle();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (read16(T1_COUNT) < (uint16_t)L && now() - t0 < 2000000u) {} }
    const uint32_t t0 = now();
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if (s & 0x04000000u) return t;
        if (t > 4000000u) return 0xFFFFFFFFu;
    }
}

static const int LINES[5] = { 4, 24, 64, 128, 200 };
static uint32_t w[8];

static void row(const char* name, int px, int py, int n) {
    printf("  %s %3d,%-3d", name, px, py);
    for (int li = 0; li < 5; li++) {
        uint32_t lo = 0xFFFFFFFFu, hi = 0;
        for (int k = 0; k < 4; k++) { const uint32_t t = drawAt(w, n, LINES[li]); if (t < lo) lo = t; if (t > hi) hi = t; }
        printf("  %6u..%-6u", (unsigned)lo, (unsigned)hi);
    }
    printf("\n");
}

static void prims() {
    static const int P[3][2] = { { 0, 16 }, { 0, 256 }, { 512, 256 } };
    for (int p = 0; p < 3; p++) {
        const int x = P[p][0], y = P[p][1];
        w[0] = 0x60204080u; w[1] = xy(x, y); w[2] = xy(256, 32);                       row("F", x, y, 3);
        w[0] = 0x62204080u;                                                             row("S", x, y, 3);
        w[0] = 0x64808080u; w[1] = xy(x, y); w[2] = uv(0, 0, 0); w[3] = xy(64, 64);    row("T", x, y, 4);
        w[0] = 0x80000000u; w[1] = xy(768, 256); w[2] = xy(x, y); w[3] = xy(128, 32);  row("C", x, y, 4);
    }
}
static void settle() { const uint32_t t0 = now(); while (now() - t0 < 600000u) {} }
static void mode(const char* name, uint32_t gp1_08, bool on) {
    write32(GP1, gp1_08);
    write32(GP1, on ? 0x03000000u : 0x03000001u);
    settle();
    printf("%s (L = 4 / 24 / 64 / 128 / 200):\n", name);
    prims();
}

int main() {
    printf("\ngpu/gpu-scanout2\n");
    printf("Short primitives started at a known scan line (Timer 1 in hblanks after VBlank) under five display modes, at\n");
    printf("three places; cycles from the first word to GPUSTAT bit 26, fastest..slowest of four. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t stat0 = read32(GP1);
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(T1_MODE, 0x0103);                                   /* hblank source, reset at VBlank */
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x01000000u);                                  /* reset the command buffer */
    write32(GP1, 0x04000000u);                                  /* no DMA */
    write32(GP0, 0xE1000000u | (1u << 10) | tpage(2));         /* drawing to the displayed area allowed; 15-bit texture page */
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP1, 0x05000000u);                                  /* display start (0,0) */
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);    /* vertical range 16..256 */

    mode("DISPLAY OFF", 0x08000001u, false);
    mode("256x240 15-bit", 0x08000000u, true);
    mode("320x240 15-bit", 0x08000001u, true);
    mode("640x480 interlaced 15-bit", 0x08000027u, true);
    mode("640x480 interlaced 24-bit", 0x08000037u, true);

    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-scanout2: Done.\n");
    for (;;) VSync(0);
}
