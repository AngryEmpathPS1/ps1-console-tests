/*
 * gpu/gpu-shape-time3 - gpu-shape-time's exact timing for polygons and lines: set-up (bit 28) and drawing (bit 26) by kind
 * and size (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-shape-time2 on the SCPH-1001: a polygon holds GPUSTAT bit 28 until its set-up is done and it starts
 * to draw (M175), so bit 28's D is the set-up - flat triangle 27-30 against this core's 40 (gpu-cmd-ready's legs-of-8 31
 * against 40 on the new GPU's consoles too), gouraud 169 against 119, textured 116 against 79, a gouraud line 79 against
 * a flat one's 17 - and bit 26's the set-up plus the drawing: textured 16 +37 (all set-up), gouraud +14, flat 64 +24. One
 * shape each. Sizes and kinds separate the set-up from the rows and pixels.
 *
 * WHAT IT DOES. As gpu-shape-time2 (the words before the last four written just before; D cycles; one GPUSTAT read; D
 * binary-searched for bit 26 and bit 28 at eight start points a line, in the blanking). Shapes from (192,128), textures
 * at (512,256) cached (15-bit within 32x32 texels, 4-bit within 64x64), the texture page set first:
 *   flat triangles, legs 4, 8, 16, 32, 64; thin 2x64 and 64x2; gouraud 8, 16, 32, 64; textured 15-bit 8, 16, 32 and
 *   4-bit 16, 64; gouraud-textured 15-bit 16, 32; semi flat 16, 64, semi gouraud 16, semi textured 16;
 *   quads: flat 16x16, 64x64, gouraud 16x16, textured 15-bit 16x16;
 *   lines: flat level 4, 32, 128, 255; diagonal 32; steep 32, 128; gouraud level 16, 128, steep 64; semi level 64.
 *
 * READ IT LIKE THIS. Per shape, bit 26's D and bit 28's, fastest..slowest over the eight start points, against this core's
 * run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time3 --ntsc --frames=40000 --quiet=40000`.
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
static void tri(const char* nm, int c, int L, int depth) {   /* a right triangle, legs L: c = 20h flat, 30h gouraud, 24h textured, 34h gouraud-textured, +2 semi */
    Shape& s = add(nm, depth);
    const bool g = (c & 0x10) != 0, t = (c & 4) != 0;
    const uint32_t tp = tpage(depth);
    const int um = depth == 2 ? 31 : 63;
    const int U = L - 1 < um ? L - 1 : um;
    W(s, ((uint32_t)c << 24) | (t ? T : F)); W(s, P(0, 0)); if (t) W(s, uv(0, 0, CLUT));
    if (g) W(s, G2); W(s, P(L, 0)); if (t) W(s, uv(U, 0, tp));
    if (g) W(s, G3); W(s, P(0, L)); if (t) W(s, uv(0, U, 0));
}
static void quad(const char* nm, int c, int L, int depth) {
    Shape& s = add(nm, depth);
    const bool g = (c & 0x10) != 0, t = (c & 4) != 0;
    const uint32_t tp = tpage(depth);
    const int U = L - 1 < 31 ? L - 1 : 31;
    W(s, ((uint32_t)c << 24) | (t ? T : F)); W(s, P(0, 0)); if (t) W(s, uv(0, 0, CLUT));
    if (g) W(s, G2); W(s, P(L, 0)); if (t) W(s, uv(U, 0, tp));
    if (g) W(s, G3); W(s, P(0, L)); if (t) W(s, uv(0, U, 0));
    if (g) W(s, G2); W(s, P(L, L)); if (t) W(s, uv(U, U, 0));
}
static void line(const char* nm, int c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F); W(s, P(0, 0)); if (c & 0x10) W(s, G2); W(s, P(dx, dy));
}
static void defs() {
    tri("tri flat 4", 0x20, 4, 2); tri("tri flat 8", 0x20, 8, 2); tri("tri flat 16", 0x20, 16, 2);
    tri("tri flat 32", 0x20, 32, 2); tri("tri flat 64", 0x20, 64, 2);
    { Shape& s = add("tri flat 2x64", 2); W(s, 0x20000000u | F); W(s, P(0, 0)); W(s, P(2, 0)); W(s, P(0, 64)); }
    { Shape& s = add("tri flat 64x2", 2); W(s, 0x20000000u | F); W(s, P(0, 0)); W(s, P(64, 0)); W(s, P(0, 2)); }
    tri("tri gour 8", 0x30, 8, 2); tri("tri gour 16", 0x30, 16, 2); tri("tri gour 32", 0x30, 32, 2); tri("tri gour 64", 0x30, 64, 2);
    tri("tri tex15 8", 0x24, 8, 2); tri("tri tex15 16", 0x24, 16, 2); tri("tri tex15 32", 0x24, 32, 2);
    tri("tri tex4 16", 0x24, 16, 0); tri("tri tex4 64", 0x24, 64, 0);
    tri("tri gtex15 16", 0x34, 16, 2); tri("tri gtex15 32", 0x34, 32, 2);
    tri("tri semi 16", 0x22, 16, 2); tri("tri semi 64", 0x22, 64, 2); tri("tri semi gour 16", 0x32, 16, 2);
    tri("tri semi tex 16", 0x26, 16, 2);
    quad("quad flat 16", 0x28, 16, 2); quad("quad flat 64", 0x28, 64, 2); quad("quad gour 16", 0x38, 16, 2);
    quad("quad tex15 16", 0x2C, 16, 2);
    line("line level 4", 0x40, 3, 0); line("line level 32", 0x40, 31, 0); line("line level 128", 0x40, 127, 0);
    line("line level 255", 0x40, 254, 0); line("line diag 32", 0x40, 31, 31); line("line steep 32", 0x40, 8, 31);
    line("line steep 128", 0x40, 16, 127); line("line gour 16", 0x50, 15, 0); line("line gour 128", 0x50, 127, 0);
    line("line gour steep 64", 0x50, 8, 63); line("line semi 64", 0x42, 63, 0);
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
    printf("\ngpu/gpu-shape-time3\n");
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
    printf("\ngpu/gpu-shape-time3: Done.\n");
    for (;;) VSync(0);
}
