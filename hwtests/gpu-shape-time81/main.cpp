/*
 * gpu/gpu-shape-time81 - 8- AND 4-BIT TEXTURED ROWS BY WIDTH, in steps of 16: where a paletted row's cost falls short (2026-11-11)
 *
 * WHY. hwtests/gpu-shape-time80 on the second SCPH-5501 against v0.377.324: 15-bit textured triangles read within 2 cycles RMS
 * (every row from x 0) and 13 (rows from the diagonal), but the 8-bit ones read short - T1 (rows from x 0) +1 / -10 / -17 / -35 at
 * W 64 / 128 / 160 / 256, T2 0 / -21 / -20 / -22, the 4-bit ones -19 / -14 at 256 - and gpu-shape-time79's 8-bit quads are not even
 * monotonic in W (-51 at 128, +28 at 160, -48 at 256 on v0.377.317). Four widths cannot say whether that is a cost per fill, per
 * row, per frame-buffer write, or the texture cache holding different texels than this emulator's at some widths.
 *
 * WHAT IT DOES. As gpu-shape-time80 (a lone polygon redrawn, bit 26's first D, eight start points in the blanking; the texels
 * and CLUT as there), W x 16 over texels 0..W-1 x 0..15:
 *   8-bit  T1 (0,0),(W,0),(0,16) and Q, W = 80, 96 .. 256 (12 widths)
 *   4-bit  T1 and Q, W = 128, 160, 192, 224, 256
 *   8-bit  T1 W x 32 at W 128 and 256 (twice the rows: does the shortfall follow the rows?)
 *   15-bit T1 at W 96, 192, 224 (controls), and semi T1 at W 64, 96, 128, 192, 224 (where the semi premium of
 *   gpu-shape-time80's W 160 / 256, +16 / +34 here, begins)
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest. Predictions in PREDICTED.txt, written before any
 * console ran it. No assertions. Built on gpu-shape-time80's source (its probes and search unchanged).
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time81 --ntsc --frames=60000 --quiet=60000`.
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
/* a triangle from three vertices in the order given: c = 20h flat, 30h gouraud */
static void vtri(const char* nm, uint32_t c, int x0, const int* P3) {
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0;
    W(s, (c << 24) | F);
    for (int k = 0; k < 3; k++) { if (g && k) W(s, k == 1 ? G2 : G3); W(s, Q(x0, P3[2 * k], P3[2 * k + 1])); }
}
/* a texture page word: depth 0 / 1 / 2 (4-bit / 8-bit / 15-bit), + 16 for the page at (768,256) whose texels have bit 15 set */
static uint32_t tpageS(int d) { return ((d & 16) ? 12u : 8u) | (1u << 4) | ((uint32_t)(d & 3) << 7); }
static const uint32_t CLUTS = (482u << 6) | 0u;
/* a textured TL triangle W x H over texels (0,0),(W,0),(0,H): c = 25h opaque, 27h semi */
static void ttriD(const char* nm, uint32_t c, int d, int w, int h) {
    Shape& s = add(nm, d);
    W(s, (c << 24) | T);
    W(s, Q(0, 0, 0)); W(s, uv(0, 0, (d & 16) ? CLUTS : CLUT));
    W(s, Q(0, w, 0)); W(s, uv(w & 0xFF, 0, tpageS(d)));
    W(s, Q(0, 0, h)); W(s, uv(0, h & 0xFF, 0));
}
/* a textured rectangle W x H at (0,128): c = 65h opaque, 67h semi; texels from (0,0) of the page E1 names */
static void trectD(const char* nm, uint32_t c, int d, int w, int h) {
    Shape& s = add(nm, d);
    W(s, (c << 24) | T); W(s, Q(0, 0, 0)); W(s, uv(0, 0, (d & 16) ? CLUTS : CLUT)); W(s, ((uint32_t)h << 16) | (uint32_t)w);
}
/* a gouraud quad (0,0),(W,0),(0,H),(W,H): c = 38h opaque, 3Ah semi - every row W wide */
static void gquad(const char* nm, uint32_t c, int w, int h) {
    Shape& s = add(nm, 2);
    W(s, (c << 24) | F); W(s, Q(0, 0, 0)); W(s, G2); W(s, Q(0, w, 0)); W(s, G3); W(s, Q(0, 0, h)); W(s, 0x00408020u); W(s, Q(0, w, h));
}
/* a textured quad (0,0),(W,0),(0,H),(W,H) over texels 0..W-1 x 0..H-1: c = 2Dh opaque, 2Fh semi - every row W wide */
static void tquadD(const char* nm, uint32_t c, int d, int w, int h) {
    Shape& s = add(nm, d);
    const int u1 = w > 255 ? 255 : w - 1;
    W(s, (c << 24) | T);
    W(s, Q(0, 0, 0)); W(s, uv(0, 0, CLUT));
    W(s, Q(0, w, 0)); W(s, uv(u1, 0, tpageS(d)));
    W(s, Q(0, 0, h)); W(s, uv(0, h - 1, 0));
    W(s, Q(0, w, h)); W(s, uv(u1, h - 1, 0));
}
/* the quad's corners as tquadD has them: A (0,0), B (W,0), C (0,H), D (W,H), each with its texel; a polygon of three or four of
   them in the order given (c: 25h / 27h triangle, 2Dh / 2Fh quad), the CLUT on the first texel's word, the page on the second's */
static void tpolyD(const char* nm, uint32_t c, int d, int w, int h, const char* order) {
    Shape& s = add(nm, d);
    const int u1 = w > 255 ? 255 : w - 1;
    const int vx[4] = { 0, w, 0, w }, vy[4] = { 0, 0, h, h }, tu[4] = { 0, u1, 0, u1 }, tv[4] = { 0, 0, h - 1, h - 1 };
    W(s, (c << 24) | T);
    for (int k = 0; order[k]; k++) { const int v = order[k] - 'A'; W(s, Q(0, vx[v], vy[v])); W(s, uv(tu[v], tv[v], k == 0 ? CLUT : k == 1 ? tpageS(d) : 0)); }
}
static void one(char (*nm)[28], int& k, bool semi, int d, int w, int h, bool quad) {
    sprintf(nm[k], "%s tex%s %s %dx%d", semi ? "semi" : "opaq", d == 2 ? "15" : d == 1 ? "8" : "4", quad ? "Q" : "T1", w, h);
    tpolyD(nm[k], (quad ? 0x2D : 0x25) | (semi ? 2 : 0), d, w, h, quad ? "ABCD" : "ABC"); k++;
}
static void defs() {
    static char nm[64][28];
    int k = 0;
    for (int w = 80; w <= 256; w += 16) { one(nm, k, false, 1, w, 16, false); one(nm, k, false, 1, w, 16, true); }
    for (int w = 128; w <= 256; w += 32) { one(nm, k, false, 0, w, 16, false); one(nm, k, false, 0, w, 16, true); }
    one(nm, k, false, 1, 128, 32, false); one(nm, k, false, 1, 256, 32, false);
    one(nm, k, false, 2, 96, 16, false); one(nm, k, false, 2, 192, 16, false); one(nm, k, false, 2, 224, 16, false);
    { static const int WS[5] = { 64, 96, 128, 192, 224 }; for (int i = 0; i < 5; i++) one(nm, k, true, 2, WS[i], 16, false); }
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
static const uint32_t MAXD = 65000;
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
    printf("\ngpu/gpu-shape-time81\n");
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
    /* the second page and CLUT: the same colour with bit 15 SET - a fill writes it clear, so a rectangle over them with
       GP0(E6h)'s set-mask bit on */
    write32(GP0, 0xE6000001u);
    write32(GP0, 0x60FFFFFFu); write32(GP0, xy(768, 256)); write32(GP0, xy(256, 256));
    waitReady();
    write32(GP0, 0x60FFFFFFu); write32(GP0, xy(0, 482)); write32(GP0, xy(256, 2));
    waitReady();
    write32(GP0, 0xE6000000u);
    waitReady();
    static uint32_t r26[64][2], r28[64][2];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpageS(S[i].depth)); waitReady();
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
    printf("shape                  bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-21s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-shape-time81: Done.\n");
    for (;;) VSync(0);
}
