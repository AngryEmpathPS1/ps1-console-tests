/*
 * gpu/gpu-tex-opaque - does a textured primitive cost the same over a texture whose texels are all transparent
 * (0000h, never written) as over one whose texels are all drawn?
 *
 * WHY (the GPU cost refit, v0.377.24). hwtests/gpu-cost-sweep textures from whatever VRAM holds at X 512, Y 0, with a
 * CLUT at (0,480): it uploads nothing. On this core its 15-bit texture is zero below row 16 (all transparent) and its
 * CLUT is all set (every 4- and 8-bit texel drawn); on a console it is whatever Unirom left there. hwtests/semi-cost,
 * whose texture is all 1234h (every texel drawn), times a 256x240 15-bit sprite 12% slower per row than the sweep's
 * 256x256 one on the same consoles, and a quad 4.5% slower. If a transparent texel skips its VRAM write, and that write
 * costs something, the sweep's textured rows measured a mix this core cannot reproduce, and semi-cost's rows are the
 * drawn case.
 *
 * WHAT IT DOES. The sweep's own timing (interrupts off; the primitive's words written by the CPU, then GPUSTAT polled
 * for bits 28 and 26; fastest..slowest of four, cycles from the first word). The texture at (512,0) and the CLUT at
 * (0,480) are FILLED first (GP0(02h)), so their content is known: OPAQUE - 15-bit texels 1234h, paletted texels 1111h
 * (index 1 at both depths) with every CLUT entry 1234h; TRANSPARENT - 15-bit texels 0000h, the same indices with every
 * CLUT entry 0000h. Over each, at 4, 8 and 15 bits: a textured rectangle (64h) and a 1:1 textured quad (2Ch), 64x64 and
 * 256x256, and the semi-transparent ones (66h, 2Eh) at 256x256 (texels with the STP bit clear: they draw as opaque).
 * A flat 256x256 rectangle and quad first, as the control.
 *
 * READ IT LIKE THIS. OPAQUE against TRANSPARENT on the same row is the cost of the writes; the semi rows against the
 * opaque ones are semi-cost's question again at these sizes.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.377.24, which charges a textured pixel the same drawn or not: every textured row identical over
 * the two textures (15-bit rectangle 256x256 176,399, quad 215,661; 8-bit 105,876 / 141,318; 4-bit 70,638 / 102,613), the
 * semi rows the opaque ones; CONTROL flat rectangle 34,340, semi 54,122 (1.58x), quad 35,885, semi 55,100 (1.54x),
 * triangle L 256 18,005, semi 27,633 (1.53x), quad 64x64 2,449, semi 3,896.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tex-opaque --frames=4000 --quiet=3000`.
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

static void rect(uint32_t cmd, int s) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    w[n++] = xy(0, 0);
    if (cmd & 4) w[n++] = uv(0, 0, CLUT);
    w[n++] = xy(s, s);
    row(w, n);
}
static void tri(uint32_t cmd, int L) {             /* untextured, legs L, as the sweep draws them */
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    w[n++] = xy(0, 0); w[n++] = xy(L, 0); w[n++] = xy(0, L);
    row(w, n);
}
static void quad(uint32_t cmd, int s, int depth) {
    const bool t = (cmd & 4) != 0;
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? s : 0, y = (i & 2) ? s : 0;
        w[n++] = xy(x, y);
        if (t) {
            const int u = x > 255 ? 255 : x, v = y > 255 ? 255 : y;
            w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0);
        }
    }
    row(w, n);
}

int main() {
    printf("\ngpu/gpu-tex-opaque\n");
    printf("Textured primitives over an all-drawn and an all-transparent texture, written to GP0 by the CPU, then GPUSTAT\n");
    printf("polled: when bit 28 and bit 26 read set, cycles from the first word, fastest..slowest of four. No assertions.\n\n");
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
    drawMode(2);

    printf("CONTROL (flat, 256x256):\n");
    printf("  rectangle 60h        "); rect(0x60, 256);
    printf("  quad 28h             "); quad(0x28, 256, 2);
    /* untextured semi-transparency on a quad and on a triangle: the sweep has semi triangles and rectangles only, and
       gpu/bandwidth's semi quad (ps1-tests' console) reads 1.51 times its flat one */
    printf("  semi rectangle 62h   "); rect(0x62, 256);
    printf("  semi quad 2Ah        "); quad(0x2A, 256, 2);
    printf("  triangle 20h L 256   "); tri(0x20, 256);
    printf("  semi triangle 22h    "); tri(0x22, 256);
    printf("  quad 28h 64x64       "); quad(0x28, 64, 2);
    printf("  semi quad 2Ah 64x64  "); quad(0x2A, 64, 2);

    static const char* const dn[3] = { " 4-bit", " 8-bit", "15-bit" };
    for (int pass = 0; pass < 2; pass++) {
        const uint32_t c15 = pass == 0 ? 0x1234u : 0x0000u;
        printf("%s (15-bit texels %04x; paletted texels 1111h, every CLUT entry %04x):\n",
               pass == 0 ? "OPAQUE" : "TRANSPARENT", (unsigned)c15, (unsigned)c15);
        for (int d = 0; d < 3; d++) {
            fill15(512, 0, 256, 256, d == 2 ? c15 : 0x1111u);   /* the texture */
            fill15(0, 480, 256, 1, c15);                        /* the CLUT (16 or 256 entries used) */
            drawMode(d);
            printf("  %s rectangle  64x64  ", dn[d]); rect(0x64, 64);
            printf("  %s rectangle 256x256 ", dn[d]); rect(0x64, 256);
            printf("  %s quad       64x64  ", dn[d]); quad(0x2C, 64, d);
            printf("  %s quad      256x256 ", dn[d]); quad(0x2C, 256, d);
            printf("  %s semi rect 256x256 ", dn[d]); rect(0x66, 256);
            printf("  %s semi quad 256x256 ", dn[d]); quad(0x2E, 256, d);
        }
    }
    drawMode(2);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
