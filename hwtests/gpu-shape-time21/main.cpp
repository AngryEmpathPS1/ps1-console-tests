/*
 * gpu/gpu-shape-time21 - the left edge's walk of FLAT right triangles at 6 to 16 pixels a row, unclipped, beside semi
 * and gouraud ones at the edges of their free walk (2026-10-14 night).
 *
 * WHY. gpu-shape-time20 on the SCPH-1001: a gouraud row's walk costs max(0, 1 cycle x pixels walked - 5) - 1, 2 and 4
 * a row free, 8 a row 3 cycles, 16 a row 11 - and a semi one's free to 8 a row, 4 cycles at 16. If a flat walk is the
 * same at its own 0.5 a pixel, it is free to 10 pixels a row: gpu-shape-time5's TR 64x16 (4 a row) = its TL and TR
 * 320x16 (20 a row) 75 dearer fit that; but gpu-shape-time6's clip TR 128x16 at x -64 (8 a row, its first rows starting
 * left of the drawing area) reads ~25 dearer than free. Unclipped flat walks between 5 and 19 a row have never been
 * timed: they decide between a free walk and a clipped row's own cost.
 *
 * WHAT IT DOES. As gpu-shape-time20 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking), at x 0, Y 128: TL (0,0),(W,0),(0,16) and TR (0,0),(W,0),(W,16) flat (20h) at W
 * 96, 128, 160, 192, 256 (TR walks 6, 8, 10, 12, 16 a row); semi flat (22h) at 160 and 192 (10, 12); gouraud (30h) at
 * 80 and 96 (5, 6); and the flat TR 128x16 at x -64 and -32 (clipped) beside it at 0.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest; TR less TL is the walk. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time21 --ntsc --frames=40000 --quiet=40000`.
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
/* a gouraud triangle, opaque (30h) or semi-transparent (32h) */
static void gtri3c(const char* nm, uint32_t c, int x0, int ax, int ay, int bx, int by, int cx, int cy) {
    Shape& s = add(nm, 2);
    W(s, (c << 24) | F); W(s, Q(x0, ax, ay)); W(s, G2); W(s, Q(x0, bx, by)); W(s, G3); W(s, Q(x0, cx, cy));
}
/* a flat triangle, semi-transparent (22h) */
static void stri3(const char* nm, int x0, int ax, int ay, int bx, int by, int cx, int cy) {
    Shape& s = add(nm, 2);
    W(s, 0x22000000u | F); W(s, Q(x0, ax, ay)); W(s, Q(x0, bx, by)); W(s, Q(x0, cx, cy));
}
/* a gouraud quad (38h) */
static void gquad(const char* nm, int w, int h) {
    Shape& s = add(nm, 2);
    W(s, 0x38000000u | F); W(s, Q(0, 0, 0)); W(s, G2); W(s, Q(0, w, 0)); W(s, G3); W(s, Q(0, 0, h)); W(s, G2); W(s, Q(0, w, h));
}
static void defs() {
    static const int WF[5] = { 96, 128, 160, 192, 256 };
    static char nm[64][24];
    int k = 0;
    for (int i = 0; i < 5; i++) {
        const int w = WF[i];
        sprintf(nm[k], "flat TL %dx16", w); tri3(nm[k++], 0, 0, 0, w, 0, 0, 16);
        sprintf(nm[k], "flat TR %dx16", w); tri3(nm[k++], 0, 0, 0, w, 0, w, 16);
    }
    sprintf(nm[k], "semi TL 160x16"); stri3(nm[k++], 0, 0, 0, 160, 0, 0, 16);
    sprintf(nm[k], "semi TR 160x16"); stri3(nm[k++], 0, 0, 0, 160, 0, 160, 16);
    sprintf(nm[k], "semi TL 192x16"); stri3(nm[k++], 0, 0, 0, 192, 0, 0, 16);
    sprintf(nm[k], "semi TR 192x16"); stri3(nm[k++], 0, 0, 0, 192, 0, 192, 16);
    sprintf(nm[k], "gour TL 80x16"); gtri3c(nm[k++], 0x30, 0, 0, 0, 80, 0, 0, 16);
    sprintf(nm[k], "gour TR 80x16"); gtri3c(nm[k++], 0x30, 0, 0, 0, 80, 0, 80, 16);
    sprintf(nm[k], "gour TL 96x16"); gtri3c(nm[k++], 0x30, 0, 0, 0, 96, 0, 0, 16);
    sprintf(nm[k], "gour TR 96x16"); gtri3c(nm[k++], 0x30, 0, 0, 0, 96, 0, 96, 16);
    sprintf(nm[k], "clip TR 128x16 -64"); tri3(nm[k++], -64, 0, 0, 128, 0, 128, 16);
    sprintf(nm[k], "clip TR 128x16 -32"); tri3(nm[k++], -32, 0, 0, 128, 0, 128, 16);
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
    printf("\ngpu/gpu-shape-time21\n");
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
    printf("\ngpu/gpu-shape-time21: Done.\n");
    for (;;) VSync(0);
}
