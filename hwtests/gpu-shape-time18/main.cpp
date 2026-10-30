/*
 * gpu/gpu-shape-time18 - gpu-shape-time's exact timing for the same flat triangles drawn at an EVEN and an ODD x
 * (2026-10-14).
 *
 * WHY. Over gpu-shape-time3-6's 65 lone flat triangles (scratch/scratch_oldalign2.js, the core's own spans), a row whose
 * last pixel is at an EVEN x costs less - 0.74 cycle on the SCPH-9001 (RMS 7.8 -> 5.3 cycles beside the row floor) and
 * 1.16 on the SCPH-1001 (8.2 -> 5.3) - but every one of those shapes sits at an even x, where that is the same as "an
 * odd-width row 0.7 cheaper and an odd-START row 0.7 dearer". Only a row starting odd with an even width tells them
 * apart: "end even" says cheaper, the other dearer. And gpu-thin's flat 8x256 RECTANGLE costs the same at X 13 as at
 * X 0 (1,816 / 1,815 on the 9001), every row of it ending even at 13: whatever it is, rectangles do not have it.
 *
 * WHAT IT DOES. As gpu-shape-time5 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking), flat 408020h at Y 128, each shape at x0, x0+1 (odd) and some at x0+2:
 *   TR (0,0),(W,0),(W,H) - its rows all end at x0+W-1: odd at x0 192, EVEN at 193 ("end even": every row cheaper; the
 *     other reading: no change, its odd starts and odd widths cancel);  TL (0,0),(W,0),(0,H) - its rows start at x0:
 *     at 193 every start odd ("end even": no change; the other: dearer);  BR and BL the same pairs bottom-up;
 *   16x16, 16x64 and 64x64, gouraud TR/TL 16x64 (shaded rows walk too), a flat quad 16x16, and RECTANGLES (60h) 15x64 and
 *   16x64 at X 0 and 1 (whether a polygon's rule is a rectangle's).
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest; bit 26 less bit 28 at 193 against 192.
 * No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time18 --ntsc --frames=40000 --quiet=40000`.
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
/* a gouraud triangle (opaque, 30h) */
static void gtri3(const char* nm, int x0, int ax, int ay, int bx, int by, int cx, int cy) {
    Shape& s = add(nm, 2);
    W(s, 0x30000000u | F); W(s, Q(x0, ax, ay)); W(s, G2); W(s, Q(x0, bx, by)); W(s, G3); W(s, Q(x0, cx, cy));
}
/* a flat rectangle (opaque, 60h) W x H at (x0,128) */
static void rect(const char* nm, int x0, int w, int h) {
    Shape& s = add(nm, 2);
    W(s, 0x60000000u | F); W(s, Q(x0, 0, 0)); W(s, ((uint32_t)h << 16) | (uint32_t)w);
}
static void defs() {
    tri3("TR 16x16 @192", 192, 0, 0, 16, 0, 16, 16); tri3("TR 16x16 @193", 193, 0, 0, 16, 0, 16, 16); tri3("TR 16x16 @194", 194, 0, 0, 16, 0, 16, 16);
    tri3("TL 16x16 @192", 192, 0, 0, 16, 0, 0, 16); tri3("TL 16x16 @193", 193, 0, 0, 16, 0, 0, 16); tri3("TL 16x16 @194", 194, 0, 0, 16, 0, 0, 16);
    tri3("BR 16x16 @192", 192, 16, 0, 0, 16, 16, 16); tri3("BR 16x16 @193", 193, 16, 0, 0, 16, 16, 16);
    tri3("BL 16x16 @192", 192, 0, 0, 0, 16, 16, 16); tri3("BL 16x16 @193", 193, 0, 0, 0, 16, 16, 16);
    tri3("TR 16x64 @192", 192, 0, 0, 16, 0, 16, 64); tri3("TR 16x64 @193", 193, 0, 0, 16, 0, 16, 64);
    tri3("TL 16x64 @192", 192, 0, 0, 16, 0, 0, 64); tri3("TL 16x64 @193", 193, 0, 0, 16, 0, 0, 64);
    tri3("TR 64x64 @0", 0, 0, 0, 64, 0, 64, 64); tri3("TR 64x64 @1", 1, 0, 0, 64, 0, 64, 64);
    tri3("TL 64x64 @0", 0, 0, 0, 64, 0, 0, 64); tri3("TL 64x64 @1", 1, 0, 0, 64, 0, 0, 64);
    gtri3("gour TR 16x64 @192", 192, 0, 0, 16, 0, 16, 64); gtri3("gour TR 16x64 @193", 193, 0, 0, 16, 0, 16, 64);
    gtri3("gour TL 16x64 @192", 192, 0, 0, 16, 0, 0, 64); gtri3("gour TL 16x64 @193", 193, 0, 0, 16, 0, 0, 64);
    quad4("quad 16x16 @192", 192, 0, 0, 16, 0, 0, 16, 16, 16); quad4("quad 16x16 @193", 193, 0, 0, 16, 0, 0, 16, 16, 16);
    rect("rect 16x64 @0", 0, 16, 64); rect("rect 16x64 @1", 1, 16, 64); rect("rect 15x64 @0", 0, 15, 64); rect("rect 15x64 @1", 1, 15, 64);
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
    printf("\ngpu/gpu-shape-time18\n");
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
    printf("\ngpu/gpu-shape-time18: Done.\n");
    for (;;) VSync(0);
}
