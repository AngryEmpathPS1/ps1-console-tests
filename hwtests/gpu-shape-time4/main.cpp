/*
 * gpu/gpu-shape-time4 - gpu-shape-time's exact timing for the shapes the small ones could not separate: polygons by WIDTH,
 * semi flat ones by size, thin tall quads, quads' second triangles, gouraud lines (2026-10-13 night).
 *
 * WHY. gpu-shape-time3 on the SCPH-7501 and 9001 (identical) fitted the new GPU's shaded polygons exactly (v0.377.102)
 * but left KNOWN-OPEN: a flat-polygon fit good to 3 cycles on right triangles up to 64 wide read gpu-thin's thin tall
 * quads 7-20% dear and gpu/bandwidth's full-screen 320-wide flat and semi quads 0.6-1.3% cheap - a cost growing with the
 * span's width (VRAM pages?) no shape up to 64 wide shows; semi flat triangles 24 / 102 cycles short at legs 16 / 64; a
 * quad's second triangle 16-30 dearer than its first; a 64x2 sliver; gouraud lines 17-51 dearer here than on the console.
 *
 * WHAT IT DOES. As gpu-shape-time3 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a line in the blanking). Shapes, flat 408020h, at X 0 for the wide ones (Y 128):
 *   flat right triangles W x H: 128x16, 256x16, 320x16, 128x64, 16x128, 32x8, 8x32;
 *   flat quads (rectangles) 16x16, 64x16, 128x16, 256x16, 320x16, 64x64, 4x64, 8x64, 16x64, 2x128;
 *   semi flat triangles legs 8, 32, and 128x16, 256x16; semi flat quads 64x16, 256x16;
 *   gouraud quads 32, 64; textured 15-bit quads 32 (cached); gouraud triangles 128x16, 256x16;
 *   gouraud lines level 32, 64, 255, diagonal 32, steep 32.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time4 --ntsc --frames=40000 --quiet=40000`.
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
/* a right triangle W wide and H tall from (x0,128): c = 20h flat, 30h gouraud, +2 semi */
static void tri(const char* nm, int c, int W0, int H, int x0) {
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0;
    W(s, ((uint32_t)c << 24) | F); W(s, Q(x0, 0, 0));
    if (g) W(s, G2); W(s, Q(x0, W0, 0));
    if (g) W(s, G3); W(s, Q(x0, 0, H));
}
/* a quad W x H from (x0,128) - a rectangle as two triangles: c = 28h flat, 38h gouraud, 2Ch textured, +2 semi */
static void quad(const char* nm, int c, int W0, int H, int x0) {
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0, t = (c & 4) != 0;
    const int U = W0 - 1 < 31 ? W0 - 1 : 31, V = H - 1 < 31 ? H - 1 : 31;
    W(s, ((uint32_t)c << 24) | (t ? T : F)); W(s, Q(x0, 0, 0)); if (t) W(s, uv(0, 0, CLUT));
    if (g) W(s, G2); W(s, Q(x0, W0, 0)); if (t) W(s, uv(U, 0, tpage(2)));
    if (g) W(s, G3); W(s, Q(x0, 0, H)); if (t) W(s, uv(0, V, 0));
    if (g) W(s, G2); W(s, Q(x0, W0, H)); if (t) W(s, uv(U, V, 0));
}
static void line(const char* nm, int c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F); W(s, Q(192, 0, 0)); if (c & 0x10) W(s, G2); W(s, Q(192, dx, dy));
}
static void defs() {
    tri("tri flat 128x16", 0x20, 128, 16, 0); tri("tri flat 256x16", 0x20, 256, 16, 0); tri("tri flat 320x16", 0x20, 320, 16, 0);
    tri("tri flat 128x64", 0x20, 128, 64, 0); tri("tri flat 16x128", 0x20, 16, 128, 192); tri("tri flat 32x8", 0x20, 32, 8, 192);
    tri("tri flat 8x32", 0x20, 8, 32, 192);
    quad("quad flat 16x16", 0x28, 16, 16, 192); quad("quad flat 64x16", 0x28, 64, 16, 0); quad("quad flat 128x16", 0x28, 128, 16, 0);
    quad("quad flat 256x16", 0x28, 256, 16, 0); quad("quad flat 320x16", 0x28, 320, 16, 0); quad("quad flat 64x64", 0x28, 64, 64, 0);
    quad("quad flat 4x64", 0x28, 4, 64, 192); quad("quad flat 8x64", 0x28, 8, 64, 192); quad("quad flat 16x64", 0x28, 16, 64, 192);
    quad("quad flat 2x128", 0x28, 2, 128, 192);
    tri("tri semi 8", 0x22, 8, 8, 192); tri("tri semi 32", 0x22, 32, 32, 192); tri("tri semi 128x16", 0x22, 128, 16, 0);
    tri("tri semi 256x16", 0x22, 256, 16, 0);
    quad("quad semi 64x16", 0x2A, 64, 16, 0); quad("quad semi 256x16", 0x2A, 256, 16, 0);
    quad("quad gour 32", 0x38, 32, 32, 192); quad("quad gour 64", 0x38, 64, 64, 0); quad("quad tex15 32", 0x2C, 32, 32, 192);
    tri("tri gour 128x16", 0x30, 128, 16, 0); tri("tri gour 256x16", 0x30, 256, 16, 0);
    line("line gour 32", 0x50, 31, 0); line("line gour 64", 0x50, 63, 0); line("line gour 255", 0x50, 254, 0);
    line("line gour diag 32", 0x50, 31, 31); line("line gour steep 32", 0x50, 8, 31);
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
    printf("\ngpu/gpu-shape-time4\n");
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
    printf("\ngpu/gpu-shape-time4: Done.\n");
    for (;;) VSync(0);
}
