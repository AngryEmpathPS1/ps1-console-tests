/*
 * gpu/gpu-shape-time54 - the semi gouraud row's floor against the left edge's STEP a row: what a row shares with the row
 * before (2026-10-16 evening).
 *
 * WHY. gpu-shape-time52 on the SCPH-9001 (and the SCPH-5501 alike): parallelogram quads every row W wide, semi less
 * opaque - at W 4 370 / 373 / 285 / 20 cycles with the left edge stepping 0 / 1/4 / 1 / 4 pixels a row, at W 16 151 /
 * 143 / 97 / 9, at W 64 37 / 35 / 41 / 15. v0.377.121 models a semi row as the larger of its opaque cost and a FLOOR, the
 * walk counting toward the opaque side - RMS 9 over 59 shapes, but par 64 s0-s64 17-19 over and BR 8x64 25 under. Two
 * readings fit those four steps: the walk hides the floor (a cost in pixels walked: the premium falls with the step
 * until the walk outlasts the floor), or a row waits on what it shares with the row before (the premium falls with the
 * overlap of the two rows' pixels, or of their blocks: at W 4 a step of 1 shares 3 pixels of 4 - 285 / 370 is 0.77).
 * They part at steps of 2 and 3 (linear: ~0 at W 4 by a step of ~4; overlap: 1/2 and 1/4 of the premium), at a step of
 * 1/2, past 4 (8: nothing left either way), going LEFT (a walk the same length, the rows overlapping alike), and at W 8.
 * And whether a row's place in its pixel pair or 16-pixel block counts (the start at x 1, 2, 512).
 *
 * WHAT IT DOES. As gpu-shape-time52 (in the blanking; the search to 65,000), gouraud quads semi (3Ah) and opaque (38h),
 * parallelograms (X,0),(X+W,0),(X+S,64),(X+W+S,64) - every row W wide, the left edge stepping S/64 pixels a row - at
 * W 4 / 8 / 16 and S -256 / -64 / 0 / 32 / 64 / 128 / 192 / 256 / 512 (steps -4, -1, 0, 1/2, 1, 2, 3, 4, 8; X 0, or 512
 * where S is negative), and W 4 S 0 at X 1, 2 and 512.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest. Predictions in PREDICTED.txt, written before any
 * console ran it. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time54 --ntsc --frames=40000 --quiet=40000`.
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
    static const int WA[3] = { 4, 8, 16 }, SA[9] = { -256, -64, 0, 32, 64, 128, 192, 256, 512 };
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 3; i++) for (int j = 0; j < 9; j++) {
            const int w = WA[i], sh = SA[j], x = sh < 0 ? 512 : 0, V[8] = { x, 0, x + w, 0, x + sh, 64, x + w + sh, 64 };
            sprintf(nm[k], "%s par %d s%d", c ? "opaq" : "semi", w, sh); gquad4(nm[k++], c ? 0x38 : 0x3A, V);
        }
        static const int XA[3] = { 1, 2, 512 };
        for (int j = 0; j < 3; j++) {
            const int x = XA[j], V[8] = { x, 0, x + 4, 0, x, 64, x + 4, 64 };
            sprintf(nm[k], "%s par 4 s0 x%d", c ? "opaq" : "semi", x); gquad4(nm[k++], c ? 0x38 : 0x3A, V);
        }
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
    printf("\ngpu/gpu-shape-time54\n");
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
    printf("\ngpu/gpu-shape-time54: Done.\n");
    for (;;) VSync(0);
}
