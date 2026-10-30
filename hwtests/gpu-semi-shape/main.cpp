/*
 * gpu/gpu-semi-shape - what semi-transparency costs a TEXTURED primitive: by the pixel, or by the row (2026-10-10).
 *
 * WHY. v0.377.69 charges a textured primitive's semi-transparency on every texel drawn (GPU_TEX_SEMI, GPU_TEXR_SEMI),
 * fitted to gpu-tex-opaque's 256x256 quads and rectangles - the only size it times. hwtests/gpu-irq-chain (the SCPH-7501,
 * 2026-10-10) then timed amidog's IRQ demo quad, 704x512 semi gouraud 4-bit with every UV 0 (one texel): +19,246 cycles
 * over its opaque twin, where this model charges +76,173 - a quarter. Taken with gpu-tex-opaque's 4-bit 256x256 (+15,084
 * over 65,536 texels) the two fit a cost per ROW (~56 ticks a triangle row) and none per pixel; one size each cannot say.
 * This times semi and opaque twins at shapes whose rows and pixels part: 16 / 64 / 256 wide x 256 tall and 256 wide x 64 /
 * 16 tall, quads (2Eh / 2Ch) and sprites (66h / 64h), 15-bit and 4-bit texels from a full texture, and 15-bit quads on
 * one texel (every UV 0: no texture-cache line after the first).
 *
 * WHAT IT DOES. gpu-tex-opaque's method: interrupts off, Timer 2 at the system clock unwrapped, each primitive's words
 * to GP0 from the CPU, GPUSTAT polled for bits 28 and 26; four runs each, the fastest and slowest printed. Drawing area
 * all of VRAM, no offset, no mask, semi mode 0. The texture at (512,0) is 15-bit 1234h (drawn, STP clear) or 4-bit index
 * 1 through a CLUT whose entry 1 is 1234h - the CLUT read fresh: a 1x1 sprite through another CLUT first, so the cache
 * holds no stale palette (gpu-tex-opaque's lesson: a fill does not refresh it).
 *
 * READ IT LIKE THIS. Semi less opaque per shape: across 16 / 64 / 256 wide at 256 tall the rows are the same and the
 * pixels part; across 256 / 64 / 16 tall at 256 wide both part. Cycles from the first word. No assertions.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.70 (semi charged by the drawn texel) semi less opaque: 4-bit quads
 * 16x256 / 64x256 / 256x256 / 256x64 / 256x16 +861 / +3,472 / +13,337 / +3,472 / +871 cycles - by the pixel, rows or not;
 * 15-bit quads +1,978 / +7,688 / +31,291 / +7,824 / +1,949; one texel +1,953 / +7,839 / +31,313 / +7,817 / +1,972.
 * `node hwtests/run.js gpu-semi-shape --frames=4000 --quiet=3000`
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
static uint32_t tpage(int depth) { return 8u | ((uint32_t)depth << 7); }   /* X 512, Y 0 */
static void drawMode(int depth) { write32(GP0, 0xE1000000u | (1u << 10) | tpage(depth)); }

/* GP0(02h) with the 24-bit colour whose 15-bit truncation is v (bit 15 stays clear) */
static void fill15(int x, int y, int w, int h, uint32_t v) {
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    waitIdle();
    write32(GP0, 0x02000000u | (b << 16) | (g << 8) | r);
    write32(GP0, xy(x, y));
    write32(GP0, xy(w, h));
    waitIdle();
}

static uint32_t w[16];

static void quad(uint32_t cmd, int W, int H, bool oneTexel, int depth) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? W : 0, y = (i & 2) ? H : 0;
        w[n++] = xy(x, y);
        int u = oneTexel ? 0 : (x > 255 ? 255 : x), v = oneTexel ? 0 : (y > 255 ? 255 : y);
        w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0);
    }
    row(w, n);
}
static void sprite(uint32_t cmd, int W, int H) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u; w[n++] = xy(0, 0); w[n++] = uv(0, 0, CLUT); w[n++] = xy(W, H);
    row(w, n);
}
/* a 1x1 sprite through the CLUT at (16,480), then nothing: the next CLUT (0,480) is loaded fresh */
static void freshClut() {
    waitIdle();
    write32(GP0, 0x64808080u); write32(GP0, xy(1000, 500)); write32(GP0, uv(0, 0, (480u << 6) | 1u)); write32(GP0, xy(1, 1));
    waitIdle();
}

int main() {
    printf("\ngpu/gpu-semi-shape\n");
    printf("Semi-transparent textured primitives and their opaque twins at shapes whose rows and pixels part, written to GP0\n");
    printf("by the CPU, then GPUSTAT polled: when bit 28 and bit 26 read set, cycles from the first word, fastest..slowest\n");
    printf("of four. No assertions.\n\n");
    EnterCriticalSection();
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

    static const int S[5][2] = { { 16, 256 }, { 64, 256 }, { 256, 256 }, { 256, 64 }, { 256, 16 } };
    for (int d = 0; d < 3; d += 2) {
        fill15(512, 0, 256, 256, d == 2 ? 0x1234u : 0x1111u);  /* the texture: 15-bit 1234h, or 4-bit index 1 */
        fill15(0, 480, 16, 1, 0x1234u);                         /* the CLUT (0,480): every entry 1234h */
        drawMode(d);
        printf("QUAD %s (2Eh semi / 2Ch opaque):\n", d == 2 ? "15-bit" : "4-bit");
        for (int k = 0; k < 5; k++) {
            freshClut(); drawMode(d);
            printf("  semi   %3dx%-3d", S[k][0], S[k][1]); quad(0x2E, S[k][0], S[k][1], false, d);
            printf("  opaque %3dx%-3d", S[k][0], S[k][1]); quad(0x2C, S[k][0], S[k][1], false, d);
        }
        printf("SPRITE %s (66h semi / 64h opaque):\n", d == 2 ? "15-bit" : "4-bit");
        for (int k = 0; k < 5; k++) {
            freshClut(); drawMode(d);
            printf("  semi   %3dx%-3d", S[k][0], S[k][1]); sprite(0x66, S[k][0], S[k][1]);
            printf("  opaque %3dx%-3d", S[k][0], S[k][1]); sprite(0x64, S[k][0], S[k][1]);
        }
    }
    drawMode(2);
    printf("ONE TEXEL 15-bit (every UV 0):\n");
    for (int k = 0; k < 5; k++) {
        printf("  semi   %3dx%-3d", S[k][0], S[k][1]); quad(0x2E, S[k][0], S[k][1], true, 2);
        printf("  opaque %3dx%-3d", S[k][0], S[k][1]); quad(0x2C, S[k][0], S[k][1], true, 2);
    }

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-semi-shape: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
