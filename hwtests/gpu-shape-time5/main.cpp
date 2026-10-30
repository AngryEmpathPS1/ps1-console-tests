/*
 * gpu/gpu-shape-time5 - gpu-shape-time's exact timing for a flat triangle in each of its four orientations alone, and a
 * quad split along either diagonal (2026-10-13 night).
 *
 * WHY. gpu-shape-time4 on the SCPH-9001: a flat quad's second triangle (W,0),(0,H),(W,H) - bit 26 less bit 28 - takes as
 * long as its first (0,0),(W,0),(0,H) though it has W fewer pixels (320x16: 1,401 against 1,404 cycles, the first one's
 * 2,720 pixels at 0.5 a cycle), so the quad costs ~W/2 cycles more than its pixels; but a thin tall quad's first half
 * reads 26-63 cycles CHEAPER than a lone triangle of its shape (4x64: 237 against 2x64's 295). Is it the orientation (a
 * row walked from the sloped edge's far end - a row's walk W/H wider when the slope is on its left?) or the quad (its
 * fourth vertex read before the first half has finished drawing)? A triangle alone in each orientation, beside the quad
 * in both splits, separates them.
 *
 * WHAT IT DOES. As gpu-shape-time3/4 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking). Flat 408020h, Y 128, at X 0 for the wide ones and 192 for the narrow. Sizes
 * 16x16, 64x16, 320x16, 64x64, 4x64, 16x64, 2x128, each as four lone right triangles:
 *   TL (0,0),(W,0),(0,H)   the quad's first half (gpu-shape-time4's)
 *   BR (W,0),(0,H),(W,H)   its second half
 *   TR (0,0),(W,0),(W,H)   BL (0,0),(0,H),(W,H)   the other diagonal's halves
 * and two more orders of the same pixels. mednafen (gpu_polygon.cpp DrawTriangle) draws a triangle from its leftmost - core -
 * vertex, a part above it bottom-up, and breaks a tie between two leftmost vertices by their order: TL and TR draw
 * top-down, BR and BL bottom-up, and
 *   TLu (0,H),(W,0),(0,0)  TL's pixels, bottom-up       BLd (0,0),(W,H),(0,H)  BL's pixels, top-down (the quad's order)
 * separate the direction from the pixels.
 * and quads split the other way, (W,0),(0,0),(W,H),(0,H) = TR then BL: 64x16, 320x16, 64x64, 4x64, 2x128. And a steep
 * line 128 across and 255 down, flat and gouraud: gpu-shape-time2-4 read a gouraud line drawing as a flat one to the
 * cycle up to 63 rows (only its set-up longer), where gpu-lines' poll read 255-row ones ~44 cycles dearer still.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time5 --ntsc --frames=40000 --quiet=40000`.
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
/* a flat triangle (opaque, 20h) with its vertices in this order, from (x0,128) */
static void tri3(const char* nm, int x0, int ax, int ay, int bx, int by, int cx, int cy) {
    Shape& s = add(nm, 2);
    W(s, 0x20000000u | F); W(s, Q(x0, ax, ay)); W(s, Q(x0, bx, by)); W(s, Q(x0, cx, cy));
}
/* a flat quad (opaque, 28h) with its vertices in this order: (v0,v1,v2) then (v1,v2,v3) */
static void quad4(const char* nm, int x0, int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, 0x28000000u | F); W(s, Q(x0, ax, ay)); W(s, Q(x0, bx, by)); W(s, Q(x0, cx, cy)); W(s, Q(x0, dx, dy));
}
/* a line from (192,128) by (dx,dy): 40h flat, 50h gouraud */
static void line2(const char* nm, int c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F); W(s, Q(192, 0, 0)); if (c & 0x10) W(s, G2); W(s, Q(192, dx, dy));
}
static void defs() {
    tri3("TL 16x16", 192, 0, 0, 16, 0, 0, 16); tri3("BR 16x16", 192, 16, 0, 0, 16, 16, 16);
    tri3("TR 16x16", 192, 0, 0, 16, 0, 16, 16); tri3("BL 16x16", 192, 0, 0, 0, 16, 16, 16);
    tri3("TLu 16x16", 192, 0, 16, 16, 0, 0, 0); tri3("BLd 16x16", 192, 0, 0, 16, 16, 0, 16);
    tri3("TL 64x16", 0, 0, 0, 64, 0, 0, 16); tri3("BR 64x16", 0, 64, 0, 0, 16, 64, 16);
    tri3("TR 64x16", 0, 0, 0, 64, 0, 64, 16); tri3("BL 64x16", 0, 0, 0, 0, 16, 64, 16);
    tri3("TLu 64x16", 0, 0, 16, 64, 0, 0, 0); tri3("BLd 64x16", 0, 0, 0, 64, 16, 0, 16);
    tri3("TL 320x16", 0, 0, 0, 320, 0, 0, 16); tri3("BR 320x16", 0, 320, 0, 0, 16, 320, 16);
    tri3("TR 320x16", 0, 0, 0, 320, 0, 320, 16); tri3("BL 320x16", 0, 0, 0, 0, 16, 320, 16);
    tri3("TLu 320x16", 0, 0, 16, 320, 0, 0, 0); tri3("BLd 320x16", 0, 0, 0, 320, 16, 0, 16);
    tri3("TL 64x64", 0, 0, 0, 64, 0, 0, 64); tri3("BR 64x64", 0, 64, 0, 0, 64, 64, 64);
    tri3("TR 64x64", 0, 0, 0, 64, 0, 64, 64); tri3("BL 64x64", 0, 0, 0, 0, 64, 64, 64);
    tri3("TLu 64x64", 0, 0, 64, 64, 0, 0, 0); tri3("BLd 64x64", 0, 0, 0, 64, 64, 0, 64);
    tri3("TL 4x64", 192, 0, 0, 4, 0, 0, 64); tri3("BR 4x64", 192, 4, 0, 0, 64, 4, 64);
    tri3("TR 4x64", 192, 0, 0, 4, 0, 4, 64); tri3("BL 4x64", 192, 0, 0, 0, 64, 4, 64);
    tri3("TLu 4x64", 192, 0, 64, 4, 0, 0, 0); tri3("BLd 4x64", 192, 0, 0, 4, 64, 0, 64);
    tri3("TL 16x64", 192, 0, 0, 16, 0, 0, 64); tri3("BR 16x64", 192, 16, 0, 0, 64, 16, 64);
    tri3("TR 16x64", 192, 0, 0, 16, 0, 16, 64); tri3("BL 16x64", 192, 0, 0, 0, 64, 16, 64);
    tri3("TLu 16x64", 192, 0, 64, 16, 0, 0, 0); tri3("BLd 16x64", 192, 0, 0, 16, 64, 0, 64);
    tri3("TL 2x128", 192, 0, 0, 2, 0, 0, 128); tri3("BR 2x128", 192, 2, 0, 0, 128, 2, 128);
    tri3("TR 2x128", 192, 0, 0, 2, 0, 2, 128); tri3("BL 2x128", 192, 0, 0, 0, 128, 2, 128);
    tri3("TLu 2x128", 192, 0, 128, 2, 0, 0, 0); tri3("BLd 2x128", 192, 0, 0, 2, 128, 0, 128);
    quad4("quad2 64x16", 0, 64, 0, 0, 0, 64, 16, 0, 16);
    quad4("quad2 320x16", 0, 320, 0, 0, 0, 320, 16, 0, 16);
    quad4("quad2 64x64", 0, 64, 0, 0, 0, 64, 64, 0, 64);
    quad4("quad2 4x64", 192, 4, 0, 0, 0, 4, 64, 0, 64);
    quad4("quad2 2x128", 192, 2, 0, 0, 0, 2, 128, 0, 128);
    line2("line flat steep 255", 0x40, 128, 255); line2("line gour steep 255", 0x50, 128, 255);
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
    printf("\ngpu/gpu-shape-time5\n");
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
    printf("\ngpu/gpu-shape-time5: Done.\n");
    for (;;) VSync(0);
}
