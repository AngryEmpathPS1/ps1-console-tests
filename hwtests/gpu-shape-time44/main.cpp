/*
 * gpu/gpu-shape-time44 - three loose ends of gpu-shape-time42/43 on the new GPU (2026-10-16).
 *
 * WHY. (1) A quad 1x16's second triangle draws no pixel and still costs 23 cycles (gouraud, textured; flat ~36) where
 * this core charges an empty triangle nothing: what does a triangle of empty rows cost, by its height? (2) The semi
 * sprite's premium is ~5.1 a row + 3.5 per 16 pixels when every texel is cached, and the texture fills add 0.5 / 1.25 /
 * 0.9 each at 4 / 8 / 15-bit - with u = x everywhere, so each fill fell at the same place in each 16 pixels: is it the
 * fill's place against the 16-pixel block (move u, move x)? (3) The semi gouraud premium: a lone TL 64x16 pays 15, the
 * same triangle as a quad's FIRST half pays ~0 (the quad's bit 28 the opaque one's) - a cost at a polygon's END that the
 * next one hides? Two semi triangles sent apart against the quad.
 *
 * WHAT IT DOES. As gpu-shape-time43 (in the blanking; the search to 65,000):
 *   A  empty triangles (1,0),(0,H),(1,H) - no pixel drawn - flat (20h), gouraud (30h), raw textured 4-bit (25h), H 4 / 16 / 64
 *   B  raw 15-bit sprites 64x8 semi and opaque at (x, u) = (0,1), (0,2), (0,4), (1,0), (4,0), (8,0)
 *   C  raw sprites semi and opaque 4-bit 32x8 and 128x8, 8-bit 128x8
 *   D  gouraud TL W x 16 semi and opaque at W 4 / 8 / 32
 *   E  two gouraud triangles sent one after the other, TL (0,0),(64,0),(0,16) then (64,0),(0,16),(64,16): semi and opaque
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest. Predictions in PREDICTED.txt, written before any
 * console ran it. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time44 --ntsc --frames=40000 --quiet=40000`.
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
/* a quad (x0,y0) (x1,y1) (x2,y2) (x3,y3) of kind c: 28h flat, 38h gouraud (colours c0..c3), 2Dh raw textured (u = x,
   v = y of the page the depth names), 3Ch gouraud-textured */
static void quadK(const char* nm, uint32_t c, int d, const int* V, const uint32_t* C) {
    Shape& s = add(nm, d);
    const bool g = (c & 0x10) != 0, t = (c & 0x04) != 0;
    W(s, (c << 24) | (g ? C[0] : (t ? T : F)));
    for (int k = 0; k < 4; k++) {
        if (g && k) W(s, C[k]);
        W(s, Q(0, V[2 * k], V[2 * k + 1]));
        if (t) W(s, uv(V[2 * k] & 0xFF, V[2 * k + 1] & 0xFF, k == 0 ? CLUT : k == 1 ? tpageS(d) : 0));
    }
}
/* a raw-textured rectangle W x H at (x,128), texels from (u,0): c = 65h opaque, 67h semi */
static void trectXU(const char* nm, uint32_t c, int d, int x, int u, int w, int h) {
    Shape& s = add(nm, d);
    W(s, (c << 24) | T); W(s, Q(0, x, 0)); W(s, uv(u, 0, CLUT)); W(s, ((uint32_t)h << 16) | (uint32_t)w);
}
/* a triangle of kind c: 20h flat, 30h gouraud, 25h raw textured (u = x, v = y of the page the depth names) */
static void triK(Shape& s, uint32_t c, int d, const int* V, const uint32_t* C) {
    const bool g = (c & 0x10) != 0, t = (c & 0x04) != 0;
    W(s, (c << 24) | (g ? C[0] : (t ? T : F)));
    for (int k = 0; k < 3; k++) {
        if (g && k) W(s, C[k]);
        W(s, Q(0, V[2 * k], V[2 * k + 1]));
        if (t) W(s, uv(V[2 * k] & 0xFF, V[2 * k + 1] & 0xFF, k == 0 ? CLUT : k == 1 ? tpageS(d) : 0));
    }
}
static void defs() {
    static char nm[64][28];
    int k = 0;
    static const uint32_t C3[3] = { 0x408020u, 0x00204080u, 0x00802040u }, C3b[3] = { 0x00204080u, 0x00802040u, 0x00408020u };
    static const uint32_t KC[3] = { 0x20, 0x30, 0x25 }; static const char* KN[3] = { "flat", "gour", "tex4" };
    static const int HE[3] = { 4, 16, 64 };
    for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
        const int V[6] = { 1, 0, 0, HE[i], 1, HE[i] }; sprintf(nm[k], "%s empty 1x%d", KN[j], HE[i]);
        Shape& s = add(nm[k++], j == 2 ? 0 : 2); triK(s, KC[j], j == 2 ? 0 : 2, V, C3);
    }
    static const int XU[6][2] = { { 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 0 }, { 4, 0 }, { 8, 0 } };
    for (int c = 0; c < 2; c++) for (int i = 0; i < 6; i++) { sprintf(nm[k], "%s spr15 64x8 x%d u%d", c ? "opaq" : "semi", XU[i][0], XU[i][1]); trectXU(nm[k++], c ? 0x65 : 0x67, 2, XU[i][0], XU[i][1], 64, 8); }
    static const int CD[3][2] = { { 0, 32 }, { 0, 128 }, { 1, 128 } };
    for (int c = 0; c < 2; c++) for (int i = 0; i < 3; i++) { sprintf(nm[k], "%s spr%d %dx8", c ? "opaq" : "semi", CD[i][0] ? 8 : 4, CD[i][1]); trectXU(nm[k++], c ? 0x65 : 0x67, CD[i][0], 0, 0, CD[i][1], 8); }
    static const int WD[3] = { 4, 8, 32 };
    for (int c = 0; c < 2; c++) for (int i = 0; i < 3; i++) {
        const int V[6] = { 0, 0, WD[i], 0, 0, 16 }; sprintf(nm[k], "%s gour TL %dx16", c ? "opaq" : "semi", WD[i]);
        Shape& s = add(nm[k++], 2); triK(s, c ? 0x30 : 0x32, 2, V, C3);
    }
    for (int c = 0; c < 2; c++) {
        const int V1[6] = { 0, 0, 64, 0, 0, 16 }, V2[6] = { 64, 0, 0, 16, 64, 16 }; sprintf(nm[k], "%s gour 2 tris 64x16", c ? "opaq" : "semi");
        Shape& s = add(nm[k++], 2); triK(s, c ? 0x30 : 0x32, 2, V1, C3); triK(s, c ? 0x30 : 0x32, 2, V2, C3b);
    }
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
    printf("\ngpu/gpu-shape-time44\n");
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
    printf("\ngpu/gpu-shape-time44: Done.\n");
    for (;;) VSync(0);
}
