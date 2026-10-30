/*
 * gpu/gpu-cost-sweep - how long the GPU takes to draw each kind of primitive,
 * at several sizes and texture depths, timed from the CPU.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, GPU rows 30-36). The GPU's drawing time
 * decides when GPUSTAT reads ready, when a DMA may deliver the next packet and
 * how much of a frame a scene takes. This core's costs are DuckStation's
 * shapes (AddDrawTriangleTicks, AddDrawRectangleTicks, AddDrawLineTicks, the
 * polygon set-up table, fill 46 + ((w/8) + 9) x h, copy 2 a pixel), scaled
 * to ps1-tests' gpu/bandwidth: ONE console row per shape, each a full screen
 * (fills 0.826, rectangles 0.837, everything else 0.855, copies 2.12 a
 * pixel). A textured polygon costs what its rasteriser walked and its texture
 * cache loaded (M175), terms fitted to Sony's bus capture from a dev board;
 * whether they hold for a retail GPU at 4 and 8 bits, minified or magnified,
 * is KNOWN-OPEN in src. So is every SIZE the bandwidth rows did not draw:
 * the set-up table (only the gouraud-textured row checked), flat against
 * gouraud (free here), lines (no row at all), sprites below full screen, and
 * the mask check's cost (DuckStation's 1.5 a pixel).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock unwrapped in
 * software. For each primitive: wait for GPUSTAT bits 26 and 28, write its
 * words to GP0 from the CPU, then poll GPUSTAT: when bit 28 (ready for a DMA
 * block) is first seen set and when bit 26 (ready for a command: the drawing
 * done) is. Four runs each: the fastest and slowest printed. The drawing area
 * is all of VRAM with no offset; textures are the VRAM at X 512, Y 0, with a
 * CLUT at Y 480; the mask bit is neither set nor checked unless a row says so.
 *   FILL      02h at 16x16, 64x64, 256x256, 1008x16 and 16x256 (a fill's width
 *             field is 10 bits, so 1024 would draw nothing)
 *   COPY      80h VRAM to VRAM at 16x16, 64x64, 256x256 and 1024x16
 *   RECTANGLE 60h at 1x1, 16x16, 64x64 and 256x256; semi-transparent (62h)
 *             and with the mask check (E6h bit 1) at 64x64 and 256x256;
 *             textured (64h) at 4, 8 and 15 bits, 16x16 to 256x256
 *   LINE      40h across, down and diagonal at 16, 64 and 256 pixels; the
 *             gouraud 50h across at 256
 *   POLYGON   triangles with legs of 8, 32, 128 and 256: flat (20h), gouraud
 *             (30h), textured (24h) at 4, 8 and 15 bits, gouraud-textured
 *             (34h), flat semi-transparent (22h); quads 64x64 and 256x256:
 *             flat (28h), textured (2Ch) at each depth, gouraud-textured (3Ch)
 *   TEXTURE SCALE  a 64x64 quad showing 256x256 texels (minified, a texel
 *             line every few pixels) and a 256x256 quad showing 64x64
 *             (magnified), at each depth
 *
 * READ IT LIKE THIS. Cycles at 33,868,800 a second (the GPU runs at 11/7 of
 * the CPU clock on NTSC consoles). Differences between sizes of one kind give
 * its cost per pixel; the smallest size gives its fixed cost plus the CPU's
 * own writes and polls, the same on every console. Below about 150 cycles the
 * poll loop cannot see the drawing at all (a 16- and a 64-pixel line read the
 * same here).
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js gpu-cost-sweep`, seconds), bit
 * 26, fastest run:
 *   FILL      256x256 5,969; 1008x16 1,318; 16x256 1,681.
 *   COPY      256x256 88,973 (2.12 GPU ticks a pixel); 1024x16 22,318.
 *   RECTANGLE 256x256 flat 35,380, semi 52,997, mask check 53,000 (both x1.5);
 *             textured 4-bit 70,616, 8-bit 105,867, 15-bit 176,378 (x2, x3, x5).
 *   LINE      256 long 233 whatever its direction (16 and 64: under the poll).
 *   TRIANGLE  L 256: flat 17,965, gouraud 18,149, semi 26,906; textured 4-bit
 *             52,975, 8-bit 71,377, 15-bit 108,179, gouraud-textured 108,359.
 *   QUAD      256: flat 35,840; textured 104,913 / 142,001 / 215,668;
 *             gouraud-textured 215,853. Bit 28 comes back as the second
 *             triangle starts (M175): about half of bit 26's time.
 *   SCALE     a 64-pixel quad of 256 texels costs 3.0x its 1:1 quad at
 *             15 bits (41,118 against 13,643) and a 256-pixel quad of 64
 *             texels 0.43x (93,417 against 215,668): texture-cache line
 *             fills, charged 14.17 GPU ticks each (M175, a dev board's fit).
 *
 * THE CONSOLES (SCPH-5501, 7501, 9001, two runs each; read each row by the median of the six fastest-of-four - one run
 * in six can read 5% fast): lines by direction (256 long: across 377, down ~1,330, diagonal ~1,300), gouraud 1.9x flat
 * (L 256: 33,952 against 17,989), thin copies far slower than a rate a pixel (16x16: 745), rectangles with a cost a row.
 * AT v0.377.24 (the untextured costs fitted to them, GPU_LINE_SETUP's block in src): 8.9% RMS against them over the 63
 * rows of 400 cycles or more (40.9% before), the textured rows as they were; lines 0.4%, untextured triangles and flat
 * quads 3.6%, rectangles 2.2%, copies 1.3%, fills 2.0%.
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

static void row(const char* name, const uint32_t* w, int n) {
    uint32_t lo28 = 0xFFFFFFFFu, hi28 = 0, lo26 = 0xFFFFFFFFu, hi26 = 0;
    for (int k = 0; k < 4; k++) {
        const Tm t = drawOnce(w, n);
        lo28 = t.t28 < lo28 ? t.t28 : lo28; hi28 = t.t28 > hi28 ? t.t28 : hi28;
        lo26 = t.t26 < lo26 ? t.t26 : lo26; hi26 = t.t26 > hi26 ? t.t26 : hi26;
    }
    (void)name;
    printf("   %2d words   bit 28 %7u..%-7u  bit 26 %8u..%u\n", n, (unsigned)lo28, (unsigned)hi28, (unsigned)lo26, (unsigned)hi26);
}

static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;                  /* (0, 480) */
static uint32_t tpage(int depth, int semi) { return 8u | ((uint32_t)semi << 5) | ((uint32_t)depth << 7); }   /* X 512 */
static void drawMode(int depth) { write32(GP0, 0xE1000000u | (1u << 10) | tpage(depth, 0)); }
static void mask(int check) { write32(GP0, 0xE6000000u | (check ? 2u : 0u)); }

static uint32_t w[16];

static void tri(const char* name, uint32_t cmd, int L, int depth) {
    const bool g = (cmd & 0x10) != 0, t = (cmd & 4) != 0;
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 3; i++) {
        const int x = i == 1 ? L : 0, y = i == 2 ? L : 0;
        if (g && i > 0) w[n++] = 0x404040u + (uint32_t)i * 0x202020u;
        w[n++] = xy(x, y);
        if (t) {
            const int u = x > 255 ? 255 : x, v = y > 255 ? 255 : y;
            w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth, 0) : 0);
        }
    }
    row(name, w, n);
}
/* A quad of S pixels showing T texels a side (T = S unless scaled). */
static void quad(const char* name, uint32_t cmd, int S, int T, int depth) {
    const bool g = (cmd & 0x10) != 0, t = (cmd & 4) != 0;
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? S : 0, y = (i & 2) ? S : 0;
        if (g && i > 0) w[n++] = 0x404040u + (uint32_t)i * 0x202020u;
        w[n++] = xy(x, y);
        if (t) {
            int u = (i & 1) ? T : 0, v = (i & 2) ? T : 0;
            if (u > 255) u = 255;
            if (v > 255) v = 255;
            w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth, 0) : 0);
        }
    }
    row(name, w, n);
}

int main() {
    printf("\ngpu/gpu-cost-sweep\n");
    printf("Each primitive written to GP0 by the CPU, then GPUSTAT polled: when bit 28 and bit 26 read set,\n");
    printf("cycles from the first word, fastest..slowest of four. No assertions.\n\n");
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
    mask(0);
    drawMode(2);

    printf("FILL (02h):\n");
    static const int fs[][2] = { { 16, 16 }, { 64, 64 }, { 256, 256 }, { 1008, 16 }, { 16, 256 } };   /* a fill's width field is 10 bits: 1024 reads 0 */
    for (int i = 0; i < 5; i++) {
        w[0] = 0x02203040u; w[1] = xy(0, 0); w[2] = xy(fs[i][0], fs[i][1]);
        printf("  %4dx%-4d", fs[i][0], fs[i][1]); row("", w, 3);
    }
    printf("COPY (80h), from (0,0) to (0,256):\n");
    static const int cs[][2] = { { 16, 16 }, { 64, 64 }, { 256, 256 }, { 1024, 16 } };
    for (int i = 0; i < 4; i++) {
        w[0] = 0x80000000u; w[1] = xy(0, 0); w[2] = xy(0, 256); w[3] = xy(cs[i][0], cs[i][1]);
        printf("  %4dx%-4d", cs[i][0], cs[i][1]); row("", w, 4);
    }

    printf("RECTANGLE (60h flat, 62h semi-transparent, 64h textured):\n");
    static const int rs[] = { 1, 16, 64, 256 };
    for (int i = 0; i < 4; i++) {
        w[0] = 0x60204080u; w[1] = xy(0, 0); w[2] = xy(rs[i], rs[i]);
        printf("  flat %3dx%-3d", rs[i], rs[i]); row("", w, 3);
    }
    for (int i = 2; i < 4; i++) {
        w[0] = 0x62204080u; w[1] = xy(0, 0); w[2] = xy(rs[i], rs[i]);
        printf("  semi %3dx%-3d", rs[i], rs[i]); row("", w, 3);
    }
    mask(1);
    for (int i = 2; i < 4; i++) {
        w[0] = 0x60204080u; w[1] = xy(0, 0); w[2] = xy(rs[i], rs[i]);
        printf("  mask check %3dx%-3d", rs[i], rs[i]); row("", w, 3);
    }
    mask(0);
    for (int depth = 0; depth < 3; depth++) {
        drawMode(depth);
        for (int i = 1; i < 4; i++) {
            w[0] = 0x64808080u; w[1] = xy(0, 0); w[2] = uv(0, 0, CLUT); w[3] = xy(rs[i], rs[i]);
            printf("  textured %2d-bit %3dx%-3d", depth == 0 ? 4 : depth == 1 ? 8 : 15, rs[i], rs[i]); row("", w, 4);
        }
    }
    drawMode(2);

    printf("LINE (40h flat, 50h gouraud):\n");
    static const int ls[] = { 16, 64, 256 };
    for (int i = 0; i < 3; i++) {
        const int L = ls[i];
        w[0] = 0x40FFFFFFu; w[1] = xy(0, 300); w[2] = xy(L, 300);
        printf("  across %3d", L); row("", w, 3);
        w[1] = xy(600, 0); w[2] = xy(600, L);
        printf("  down   %3d", L); row("", w, 3);
        w[1] = xy(0, 0); w[2] = xy(L, L);
        printf("  diagonal %3d", L); row("", w, 3);
    }
    w[0] = 0x50FF0000u; w[1] = xy(0, 300); w[2] = 0x0000FFu; w[3] = xy(256, 300);
    printf("  gouraud across 256"); row("", w, 4);

    printf("TRIANGLE (legs L; 20h flat, 30h gouraud, 24h textured, 34h gouraud-textured, 22h semi):\n");
    static const int ts[] = { 8, 32, 128, 256 };
    for (int i = 0; i < 4; i++) {
        const int L = ts[i];
        printf("  flat          L %3d", L); tri("", 0x20, L, 0);
        printf("  gouraud       L %3d", L); tri("", 0x30, L, 0);
        printf("  semi          L %3d", L); tri("", 0x22, L, 0);
        for (int depth = 0; depth < 3; depth++) {
            printf("  textured %2d-bit L %3d", depth == 0 ? 4 : depth == 1 ? 8 : 15, L); tri("", 0x24, L, depth);
        }
        printf("  gouraud-textured 15-bit L %3d", L); tri("", 0x34, L, 2);
    }

    printf("QUAD (28h flat, 2Ch textured, 3Ch gouraud-textured):\n");
    static const int qs[] = { 64, 256 };
    for (int i = 0; i < 2; i++) {
        const int S = qs[i];
        printf("  flat %3d", S); quad("", 0x28, S, S, 0);
        for (int depth = 0; depth < 3; depth++) {
            printf("  textured %2d-bit %3d", depth == 0 ? 4 : depth == 1 ? 8 : 15, S); quad("", 0x2C, S, S, depth);
        }
        printf("  gouraud-textured 15-bit %3d", S); quad("", 0x3C, S, S, 2);
    }
    printf("TEXTURE SCALE (textured quads):\n");
    for (int depth = 0; depth < 3; depth++) {
        printf("  %2d-bit  64 px, 256 texels (minified)", depth == 0 ? 4 : depth == 1 ? 8 : 15); quad("", 0x2C, 64, 256, depth);
        printf("  %2d-bit 256 px,  64 texels (magnified)", depth == 0 ? 4 : depth == 1 ? 8 : 15); quad("", 0x2C, 256, 64, depth);
    }

    waitIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-cost-sweep: Done.\n");
    for (;;) VSync(0);
}
