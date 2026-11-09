/*
 * gpu/gpu-tex-stride3 - a texture fill's cost by its spacing with NO scan-out (the display one line tall), and by where the
 * rows start.
 *
 * WHY (2026-11-07). hwtests/gpu-tex-stride2 on the second SCPH-5501, the display on at 320x240: console minus this core, a
 * fill, by the pixels between fills N - 15-bit N = 2 -0.29, 3 -0.16, 4 +0.03, 5 +0.21, 6 +0.31, 7 +0.15, 8 +0.25, 9 +0.38,
 * 10 +0.31, 12 +0.48, 16 -0.01 - and 8-bit the same to ~0.05-0.1 at the same N. Not smooth. Two readings: the new GPU's
 * 18-halfword write buffer, drained in 16-pixel chunks counted from the row's start (psx-spx "Write Buffer", and its
 * "16-pixel Buffer": the first chunk 15-17 pixels, odd and even X apart), meeting the fills; or the scan-out's bursts meeting
 * them at a period the fills alias against - those draws ran under the display. psx-spx: "setting Display Area Y2=Y1+1
 * will cause the GPU to be (almost) always in Vblank" (Y1 = Y2, used by gpu-clut-ready and gpu-tex-noscan, reads as a
 * 320x240 display on the console and here). This asks the curve again with the display one line tall, and moves the rows'
 * start 1, 2, 5 and 8 pixels: chunks counted from the row start (odd starts differ, +8 does not) against chunks fixed in VRAM
 * (+8 differs).
 *
 * WHAT IT DOES. gpu-tex-stride2's harness and quad: the opaque texture at (512,0) (15-bit texels 1234h, paletted 1111h,
 * every CLUT entry 1234h), the words from the CPU, bits 28 / 26 polled with Timer 2, fastest..slowest of four, each try 53
 * lines after the VBlank; a 2Ch quad 64x64 over 64 texel rows (v 1:1), its u span U = 256 / N at 15 bits, 512 / N at 8.
 * Each row draws once untimed first: nothing flushes the texture cache between rows, so a row's first draw meets the row
 * before's lines (gpu-tex-stride2's +5 rows' fastest try was that first one, on the console and here alike).
 *   V (GP1(07h) Y1 = 16, Y2 = 17): a flat 64x64 quad; 15-bit N = 1..12, 14, 16, 32; 8-bit N = 2..8, 10, 12, 16, 32; 15-bit
 *     N = 4, 8, 16 at x 1, 2, 5, 8 ("+1" ...); 8-bit N = 8 at x 1 and 8.
 *   D (320x240 as gpu-tex-stride2): a flat quad, 15-bit N = 2, 4, 7, 8, 12, 16 and 8-bit N = 8, 32 - its rows again, the tie.
 *
 * READ IT LIKE THIS. V against D at the same N is the scan-out's part of the curve; V's +1 / +2 / +5 / +8 rows against +0
 * say whether the chunks run from the row's start or sit in VRAM.
 *
 * ON THIS CORE (v0.377.303, `node hwtests/run.js gpu-tex-stride3 --ntsc --frames=30000 --quiet=25000`): PREDICTED.txt.
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
    drawOnce(w, n);   /* untimed: the texture cache holds the row before's lines (this core: 837 fills, then 1,048 a try) */
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
static int X0 = 0;
static void quad2(uint32_t cmd, int pw, int ph, int tw, int th, int depth) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? pw : 0, y = (i & 2) ? ph : 0;
        w[n++] = xy(X0 + x, Y0 + y);
        const int u = (i & 1) ? tw : 0, v = (i & 2) ? th : 0;
        if (cmd & 4) w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0);
    }
    row(w, n);
}

static const char* const dn[3] = { " 4-bit", " 8-bit", "15-bit" };
static void texture(int d) { fill15(512, 0, 256, 256, d == 2 ? 0x1234u : 0x1111u); drawMode(d); }
static void tex(int d, int N, int x0) {
    int U = (d == 2 ? 256 : 512) / N; if (U > 255) U = 255;
    X0 = x0;
    printf("  %s N %2d  U %3d  +%d   ", dn[d], N, U, x0); quad2(0x2C, 64, 64, U, 64, d);
    X0 = 0;
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-tex-stride3\n");
    printf("Textured 64x64 quads, a fill every N pixels, the display ONE LINE tall (V) and 320x240 (D), rows started at x 0..8:\n");
    printf("cycles from the first word to bits 28 and 26, fastest..slowest of four, each try 53 lines after the VBlank. No assertions.\n\n");
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
    write32(GP1, 0x08000001u);                                  /* 320 wide, 240 lines, NTSC, 15-bit */
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);

    /* V: the display one line tall (Y1 = 16, Y2 = 17), every draw in the vertical blanking */
    write32(GP1, 0x07000000u | (17u << 10) | 16u);
    printf("V (display one line):\n");
    drawMode(2);
    printf("  flat quad 64x64                    "); quad2(0x28, 64, 64, 0, 0, 2);
    static const int N15[15] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 32 };
    static const int N8[11] = { 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32 };
    static const int XS[4] = { 1, 2, 5, 8 };
    texture(2);
    for (int i = 0; i < 15; i++) tex(2, N15[i], 0);
    for (int n = 4; n <= 16; n *= 2) for (int j = 0; j < 4; j++) tex(2, n, XS[j]);
    texture(1);
    for (int i = 0; i < 11; i++) tex(1, N8[i], 0);
    tex(1, 8, 1); tex(1, 8, 8);

    /* D: the display on at 320x240, gpu-tex-stride2's rows again */
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    printf("D (display 320x240):\n");
    drawMode(2);
    printf("  flat quad 64x64                    "); quad2(0x28, 64, 64, 0, 0, 2);
    static const int ND[6] = { 2, 4, 7, 8, 12, 16 };
    texture(2);
    for (int i = 0; i < 6; i++) tex(2, ND[i], 0);
    texture(1);
    tex(1, 8, 0); tex(1, 32, 0);

    drawMode(2);
    initVideo(320, 240);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tex-stride3: Done.\n");
    for (;;) {}
}
