/*
 * gpu/gpu-shape-time22 - gpu-shape-time's exact timing for SHADED right triangles in every orientation: gouraud, and
 * textured on one texel (2026-10-14 night).
 *
 * WHY. v0.377.109 fitted the SCPH-1001's opaque FLAT rows over 115 triangles in six orientations (pixel pairs, the free
 * walk, end rows); its gouraud and textured rows still stand on GPU_POLY_OLD3's area and a handful of shapes - gouraud
 * BR 320x16 21 under, gouraud TL 2x128 13 under, textured TR 320x16 11 under. The same orientations, shaded, give their
 * rows the flat ones' fit: whether a pair end, an end row, a clipped start cost them as they cost flat rows.
 *
 * WHAT IT DOES. As gpu-shape-time18 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking), Y 128, x 192 (0 for the 64-wide): TL (0,0),(W,0),(0,H), BR (W,0),(0,H),(W,H),
 * TR (0,0),(W,0),(W,H), BL (0,0),(0,H),(W,H), TLu (0,H),(W,0),(0,0), BLd (0,0),(W,H),(0,H) - gouraud (30h) at 16x16,
 * 64x16, 64x64, 16x64, 4x64, 2x128; textured (24h, 15-bit, raw, every vertex on texel (0,0): one cache line, no fills
 * past the first) at 16x16, 64x64, 2x128.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time22 --ntsc --frames=40000 --quiet=40000`.
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
/* a right triangle by orientation: c = 30h gouraud, 25h textured raw (15-bit, every vertex on texel (0,0)) */
static void otri(const char* nm, uint32_t c, int x0, int o, int w, int h) {
    static const int V[6][6] = { { 0,0, 1,0, 0,1 }, { 1,0, 0,1, 1,1 }, { 0,0, 1,0, 1,1 }, { 0,0, 0,1, 1,1 }, { 0,1, 1,0, 0,0 }, { 0,0, 1,1, 0,1 } };
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0, t = (c & 4) != 0;
    W(s, (c << 24) | (t ? T : F));
    for (int k = 0; k < 3; k++) {
        if (g && k) W(s, k == 1 ? G2 : G3);
        W(s, Q(x0, V[o][2 * k] * w, V[o][2 * k + 1] * h));
        if (t) W(s, uv(0, 0, k == 0 ? CLUT : k == 1 ? tpage(2) : 0u));
    }
}
static void defs() {
    static const char* ON[6] = { "TL", "BR", "TR", "BL", "TLu", "BLd" };
    static const int SZ[6][3] = { { 16, 16, 192 }, { 64, 16, 0 }, { 64, 64, 0 }, { 16, 64, 192 }, { 4, 64, 192 }, { 2, 128, 192 } };
    static char nm[64][24];
    int k = 0;
    for (int z = 0; z < 6; z++) for (int o = 0; o < 6; o++) {
        sprintf(nm[k], "gour %s %dx%d", ON[o], SZ[z][0], SZ[z][1]); otri(nm[k++], 0x30, SZ[z][2], o, SZ[z][0], SZ[z][1]);
    }
    for (int z = 0; z < 6; z++) {
        if (z != 0 && z != 2 && z != 5) continue;
        for (int o = 0; o < 6; o++) { sprintf(nm[k], "tex %s %dx%d", ON[o], SZ[z][0], SZ[z][1]); otri(nm[k++], 0x25, SZ[z][2], o, SZ[z][0], SZ[z][1]); }
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
    printf("\ngpu/gpu-shape-time22\n");
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
    printf("shape                  bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-21s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-shape-time22: Done.\n");
    for (;;) VSync(0);
}
