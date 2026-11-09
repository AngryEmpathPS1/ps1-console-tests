/*
 * gpu/gpu-tex-stride - what a texture-cache fill costs by how many pixels came since the last one, at each depth.
 *
 * WHY (2026-11-07). hwtests/gpu-tex-scale on the second SCPH-5501: minified 4- and 8-bit quads (a fill every 1-3 drawn
 * pixels) read ~0.3 cycles a fill CHEAPER than this core, an 8-bit 1:1 quad (a fill every 8) ~0.2 DEARER, 15-bit ones a
 * per-pixel ~0.02 over everywhere. This core prices a fill by three classes (GPU_FILL_CLASS, v0.377.108: at a row's first
 * pixel, right after another fill, after drawn pixels) the same at every depth. psx-spx: a 4- or 8-bit miss "10.00 clks", a
 * 15-bit one 8.25.
 *
 * WHAT IT DOES. gpu-tex-scale's harness (the opaque texture at (512,0), the CLUT at (768,500); interrupts off; the words from
 * the CPU; bits 28 / 26 polled with Timer 2 unwrapped, fastest..slowest of four; each try 53 lines after the VBlank; the
 * display on at 320x240). At 4, 8 and 15 bits, a 2Ch quad 64 pixels wide and 64 tall over 64 texel rows (v 1:1), its u span
 * U = 16, 32, 64, 96, 128, 192, 255 texels - a fill every 64 / U lines' worth of pixels: at 8 bits every 32 / 16 / 8 / 5.3 / 4
 * / 2.7 / 2 pixels. The same pixels and rows on every row of a depth; only the fills and their spacing change. A flat 64x64
 * quad first, the control.
 *
 * READ IT LIKE THIS. Within a depth, the rows' differences are their fills' cost; against this core, the spacing at which
 * its classes go wrong.
 *
 * ON THIS CORE (v0.377.302 + the CLUT proposal, `node hwtests/run.js gpu-tex-stride --frames=20000 --quiet=15000`): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define ISTAT 0x1F801070u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}
static void waitVblank() { write32(ISTAT, ~1u); for (int i = 0; i < 2000000 && !(read32(ISTAT) & 1u); i++) { now(); } }
static void waitLines(int n) { for (int l = 0; l < n; l++) { const uint16_t t = read16(T1_COUNT); for (int i = 0; i < 100000 && read16(T1_COUNT) == t; i++) { now(); } } }

struct Tm { uint32_t t28, t26; };
static Tm drawOnce(const uint32_t* w, int n) {
    waitIdle();
    waitVblank(); waitLines(53);
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
static const uint32_t CLUT = (500u << 6) | (768u >> 4);         /* (768, 500) */
static uint32_t tpage(int depth) { return 8u | ((uint32_t)depth << 7); }   /* X 512, Y 0 */
static void drawMode(int depth) { write32(GP0, 0xE1000000u | (1u << 10) | tpage(depth)); }

static void fill15(int x, int y, int w, int h, uint32_t v) {
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    waitIdle();
    write32(GP0, 0x02000000u | (b << 16) | (g << 8) | r);
    write32(GP0, xy(x, y));
    write32(GP0, xy(w, h));
    waitIdle();
}

static uint32_t w[16];
static const int Y0 = 256;

static void rect(uint32_t cmd, int s) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    w[n++] = xy(0, Y0);
    if (cmd & 4) w[n++] = uv(0, 0, CLUT);
    w[n++] = xy(s, s);
    row(w, n);
}
static void quad2(uint32_t cmd, int pw, int ph, int tw, int th, int depth);
static void quad(uint32_t cmd, int s, int depth) { quad2(cmd, s, s, s > 255 ? 255 : s, s > 255 ? 255 : s, depth); }
static void quad2(uint32_t cmd, int pw, int ph, int tw, int th, int depth) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? pw : 0, y = (i & 2) ? ph : 0;
        w[n++] = xy(x, Y0 + y);
        const int u = (i & 1) ? tw : 0, v = (i & 2) ? th : 0;
        if (cmd & 4) w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0);
    }
    row(w, n);
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-tex-stride\n");
    printf("Textured 64x64 quads over 16-255 texels in u (64 rows 1:1), the display on at 320x240: cycles from the first word\n");
    printf("to bits 28 and 26, fastest..slowest of four, each try 53 lines after the VBlank. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(T1_MODE, 0x0100);                                   /* Timer 1 counts hblanks */
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x04000000u);                                  /* no DMA */
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    fill15(768, 500, 256, 1, 0x1234u);                          /* the CLUT */

    static const char* const dn[3] = { " 4-bit", " 8-bit", "15-bit" };
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write32(GP1, 0x08000001u);                                  /* 320 wide, 240 lines, NTSC, 15-bit */
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    drawMode(2);
    printf("  flat quad 64x64                    "); quad2(0x28, 64, 64, 0, 0, 2);
    static const int US[7] = { 16, 32, 64, 96, 128, 192, 255 };
    for (int d = 0; d < 3; d++) {
        fill15(512, 0, 256, 256, d == 2 ? 0x1234u : 0x1111u);
        drawMode(d);
        for (int i = 0; i < 7; i++) { printf("  %s 64x64 over U %3d x 64      ", dn[d], US[i]); quad2(0x2C, 64, 64, US[i], 64, d); }
    }
    drawMode(2);
    initVideo(320, 240);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tex-stride: Done.\n");
    for (;;) {}
}
