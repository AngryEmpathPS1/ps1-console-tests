/*
 * gpu/gpu-shape-time6 - gpu-shape-time's exact timing for what v0.377.103's walk model left open (2026-10-13 night).
 *
 * WHY. gpu-shape-time5 on the SCPH-9001: a flat triangle's row is walked from the row before's LEFT edge, every pixel of
 * the walk costing as a drawn one, an empty row nearly free; opaque shaded quads walk too (gpu-shape-time4's gouraud
 * quads), semi flat ones not. Left open: a 1- or 2-leg triangle (gpu-tri-ready: ~10 / 12 cycles, the model 18 / 22);
 * whether semi SHADED polygons walk; whether shaded ones walk on WIDE shapes and pay a shaded row for an empty one;
 * whether a triangle whose left edge starts outside the drawing area walks the hidden part (gpu/bandwidth's off-screen
 * quads read 0.7-1% fast).
 *
 * WHAT IT DOES. As gpu-shape-time3/4/5 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking). Y 128, X 192 for the small ones, 0 for the wide. Orientations as gpu-shape-time5:
 * TL (0,0),(W,0),(0,H) - the right edge sloped; TR (0,0),(W,0),(W,H) - the left edge sloped; BR (W,0),(0,H),(W,H).
 *   flat TL legs 1, 2, 3, 5; flat BR legs 2, 3
 *   semi flat TL / TR 64x16 and 320x16; semi gouraud TL / TR 64x16 (semi textured needs texels with bit 15 set, which a fill cannot make)
 *   gouraud TL / TR / BR 320x16, gouraud TL / TR 2x128 (TR: 63 empty rows); textured TL / TR 320x16
 *   flat TR 128x16 from X -64 and from X -192 (the drawing area's left edge at 0 - the walk's first 64 / 192 pixels hidden),
 *   flat TL 128x16 from X -64 (the right edge sloped, its left part clipped)
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time6 --ntsc --frames=60000 --quiet=60000`.
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
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;
static uint32_t tpage(int depth) { return 8u | (1u << 4) | ((uint32_t)depth << 7); }   /* X 512, Y 256 */

#define PROBE_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n"
#define PROBE_TAIL \
        "beqz %[n], 3f\n" \
        "nop\n" \
        "4:\n" \
        "addiu %[n], %[n], -1\n" \
        "bnez %[n], 4b\n" \
        "nop\n" \
        "3:\n" \
        "jr $t1\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "2:\n" \
        "lw %[st], 0x1814($t0)\n" \
        "nop\n" \
        ".set reorder\n"
static uint32_t probe2(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t probe3(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t probe4(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}

struct Shape { const char* name; int depth; int n; uint32_t w[12]; };
static Shape S[64];
static int NS = 0;
static Shape& add(const char* name, int depth) { Shape& s = S[NS++]; s.name = name; s.depth = depth; s.n = 0; return s; }
static void W(Shape& s, uint32_t v) { s.w[s.n++] = v; }
static uint32_t P(int x, int y) { return xy(192 + x, 128 + y); }
static const uint32_t F = 0x408020u, T = 0x808080u, G2 = 0x00204080u, G3 = 0x00802040u;
static uint32_t Q(int x0, int x, int y) { return xy(x0 + x, 128 + y); }
/* a triangle with its vertices in this order from (x0,128): c = 20h flat, 30h gouraud, 24h textured (15-bit, UVs kept
 * inside 32x32 so the cache holds them), +2 semi */
static void tri6(const char* nm, int c, int x0, int ax, int ay, int bx, int by, int cx, int cy) {
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0, t = (c & 4) != 0;
    const int vx[3] = { ax, bx, cx }, vy[3] = { ay, by, cy };
    W(s, ((uint32_t)c << 24) | (t ? T : F));
    for (int i = 0; i < 3; i++) {
        if (g && i > 0) W(s, i == 1 ? G2 : G3);
        W(s, Q(x0, vx[i], vy[i]));
        if (t) W(s, uv(vx[i] & 31, vy[i] & 31, i == 0 ? CLUT : i == 1 ? tpage(2) : 0));
    }
}
static void defs() {
    tri6("flat TL 1", 0x20, 192, 0, 0, 1, 0, 0, 1);
    tri6("flat TL 2", 0x20, 192, 0, 0, 2, 0, 0, 2);
    tri6("flat TL 3", 0x20, 192, 0, 0, 3, 0, 0, 3);
    tri6("flat TL 5", 0x20, 192, 0, 0, 5, 0, 0, 5);
    tri6("flat BR 2", 0x20, 192, 2, 0, 0, 2, 2, 2);
    tri6("flat BR 3", 0x20, 192, 3, 0, 0, 3, 3, 3);
    tri6("semi TL 64x16", 0x22, 0, 0, 0, 64, 0, 0, 16);
    tri6("semi TR 64x16", 0x22, 0, 0, 0, 64, 0, 64, 16);
    tri6("semi TL 320x16", 0x22, 0, 0, 0, 320, 0, 0, 16);
    tri6("semi TR 320x16", 0x22, 0, 0, 0, 320, 0, 320, 16);
    tri6("sgour TL 64x16", 0x32, 0, 0, 0, 64, 0, 0, 16);
    tri6("sgour TR 64x16", 0x32, 0, 0, 0, 64, 0, 64, 16);
    tri6("gour TL 320x16", 0x30, 0, 0, 0, 320, 0, 0, 16);
    tri6("gour TR 320x16", 0x30, 0, 0, 0, 320, 0, 320, 16);
    tri6("gour BR 320x16", 0x30, 0, 320, 0, 0, 16, 320, 16);
    tri6("gour TL 2x128", 0x30, 192, 0, 0, 2, 0, 0, 128);
    tri6("gour TR 2x128", 0x30, 192, 0, 0, 2, 0, 2, 128);
    tri6("tex TL 320x16", 0x24, 0, 0, 0, 320, 0, 0, 16);
    tri6("tex TR 320x16", 0x24, 0, 0, 0, 320, 0, 320, 16);
    tri6("clip TR 128x16 -64", 0x20, -64, 0, 0, 128, 0, 128, 16);
    tri6("clip TR 128x16 -192", 0x20, -192, 0, 0, 128, 0, 128, 16);
    tri6("clip TL 128x16 -64", 0x20, -64, 0, 0, 128, 0, 0, 16);
}

/* the words before the last four (if any) written just before, then the last 2-4 stored back to back and timed */
static uint32_t probe(const Shape& s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    if (s.n <= 4) return s.n == 2 ? probe2(s.w, n, k) : s.n == 3 ? probe3(s.w, n, k) : probe4(s.w, n, k);
    for (int i = 0; i < s.n - 4; i++) write32(GP0, s.w[i]);
    return probe4(s.w + s.n - 4, n, k);
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 8000;
static uint32_t search(const Shape& s, uint32_t bit, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(j);
        const uint32_t st = probe(s, mid);
        if (st & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-shape-time6\n");
    printf("A lone polygon or line's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set\n");
    printf("and with bit 28 set, fastest..slowest over eight start points in the blanking. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | tpage(2));
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    static uint32_t r26[64][2], r28[64][2];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        probe(S[i], 6000); waitReady(); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
        uint32_t a = 0xFFFFFFFFu, b = 0, c = 0xFFFFFFFFu, d = 0;
        for (int j = 0; j < 8; j++) {
            const uint32_t x = search(S[i], 0x04000000u, j), y = search(S[i], 0x10000000u, j);
            a = x < a ? x : a; b = x > b ? x : b; c = y < c ? y : c; d = y > d ? y : d;
        }
        r26[i][0] = a; r26[i][1] = b; r28[i][0] = c; r28[i][1] = d;
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("shape               bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-18s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-shape-time6: Done.\n");
    for (;;) VSync(0);
}
