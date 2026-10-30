/*
 * gpu/gpu-shape-time50 - the semi gouraud premium: narrow rows without an upright left edge, a nearly upright one, a
 * diamond quad, and the upright edge a pixel over (2026-10-16).
 *
 * WHY. gpu-shape-time49 on the SCPH-9001: semi less opaque gouraud is ~0.45 a row plus ~10-13 a triangle whose LEFT edge
 * is upright (TL and BL 15 / 41 against TR and BR 5 / 28-30 at 64x16 / 256x64) - and narrow rows cost more again, but
 * every narrow shape measured so far (TL 4 / 8 / 16 / 32 x 64: 194 / 150 / 80 / 44) had the upright edge.
 *
 * WHAT IT DOES. As gpu-shape-time49 (in the blanking; the search to 65,000), gouraud semi (32h / 3Ah) and opaque (30h /
 * 38h):
 *   A  BR (W,0),(0,64),(W,64) at W 4 / 8 / 16 / 32 and TR (0,0),(W,0),(W,64) at W 4 / 16 - narrow rows, the left edge
 *      sloped
 *   B  a left edge leaning a little: (0,0),(64,0),(1,16) and (0,0),(64,0),(4,16)
 *   C  a diamond quad (32,0),(64,16),(0,16),(32,32) - no edge upright
 *   D  TL 64x16 with its upright edge at x 1 (the pair's odd pixel)
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest. Predictions in PREDICTED.txt, written before any
 * console ran it. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time50 --ntsc --frames=40000 --quiet=40000`.
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
/* a gouraud quad of four vertices in this order: c = 38h opaque, 3Ah semi */
static void gquad4(const char* nm, uint32_t c, const int* V) {
    Shape& s = add(nm, 2);
    W(s, (c << 24) | F); W(s, Q(0, V[0], V[1])); W(s, G2); W(s, Q(0, V[2], V[3])); W(s, G3); W(s, Q(0, V[4], V[5])); W(s, 0x00408020u); W(s, Q(0, V[6], V[7]));
}
static void defs() {
    static char nm[64][28];
    int k = 0;
    static const int WB[4] = { 4, 8, 16, 32 }, WT[2] = { 4, 16 }, LEAN[2] = { 1, 4 };
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 4; i++) { const int w = WB[i], V[6] = { w, 0, 0, 64, w, 64 }; sprintf(nm[k], "%s gour BR %dx64", c ? "opaq" : "semi", w); vtri(nm[k++], c ? 0x30 : 0x32, 0, V); }
        for (int i = 0; i < 2; i++) { const int w = WT[i], V[6] = { 0, 0, w, 0, w, 64 }; sprintf(nm[k], "%s gour TR %dx64", c ? "opaq" : "semi", w); vtri(nm[k++], c ? 0x30 : 0x32, 0, V); }
        for (int i = 0; i < 2; i++) { const int V[6] = { 0, 0, 64, 0, LEAN[i], 16 }; sprintf(nm[k], "%s gour lean%d 64x16", c ? "opaq" : "semi", LEAN[i]); vtri(nm[k++], c ? 0x30 : 0x32, 0, V); }
        { const int V[8] = { 32, 0, 64, 16, 0, 16, 32, 32 }; sprintf(nm[k], "%s gour diamond 64", c ? "opaq" : "semi"); gquad4(nm[k++], c ? 0x38 : 0x3A, V); }
        { const int V[6] = { 0, 0, 64, 0, 0, 16 }; sprintf(nm[k], "%s gour TL 64x16 X1", c ? "opaq" : "semi"); vtri(nm[k++], c ? 0x30 : 0x32, 1, V); }
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
    printf("\ngpu/gpu-shape-time50\n");
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
    printf("\ngpu/gpu-shape-time50: Done.\n");
    for (;;) VSync(0);
}
