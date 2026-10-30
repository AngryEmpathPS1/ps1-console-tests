/*
 * gpu/gpu-shape-time64 - SHORT LINES, and a steep poly-line's early vertex by its run length (2026-10-25 night).
 *
 * WHY. Two KNOWN-OPENs of src/psx.js GPU_LINE2. (1) A short level line: gpu-shape-time3's 4-pixel one reads 33 cycles where
 * this core has 39, gst60's 17-pixel one 49 / 52 - the level fit (34 + 1.0 a pixel) is from 32-255 pixels. (2) A steep
 * segment's next vertex is taken before it ends (gst61-63, the SCPH-7501): diagonals 6 / 14 / 30 / 48 cycles early at 17 /
 * 33 / 65 / 129 rows, 16 x 32 30 and then 49, 8 x 64 45, 8 x 128 50 - read as a write backlog growing ~0.455 a row on a
 * diagonal and ~0.9 where x steps less often, capped near 49 and carried into the next segment. How does it grow with the
 * x run, and does a vertical segment build one?
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat level lines (40h) from (192,128) of 1-8, 10, 12, 14, 16, 20, 24 and 28 pixels; flat
 * lines rising 1 row 3, 4, 5, 7, 9 and 13 pixels across; gouraud level lines (50h) of 4 and 16 pixels. Flat poly-lines (48h,
 * zigzag across dx and back, 32 down a segment, ended by 55555555h) of 1 and 2 segments at dx 32, 16, 11, 8, 4, 1 and 0
 * (x every 1, 2, 3, 4, 8 and 32 rows, and upright). The 2-segment poly's bit-28 D against the 1-segment poly's bit-26 D is
 * how early the next vertex is taken.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time64 --ntsc --frames=60000 --quiet=60000`.
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
/* a line from (192,128) by (dx,dy): c 40h flat, 42h semi, 50h gouraud (colours 408020h, 204080h) */
static void line11(const char* nm, int c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F);
    W(s, Q(192, 0, 0));
    if (c & 0x10) W(s, G2);
    W(s, Q(192, dx, dy));
}
/* a flat poly-line from (192,128) of n segments 32 down, alternately dx across and back (48h, ended by 55555555h) */
static void polySteep(const char* nm, int n, int dx) {
    Shape& s = add(nm, 2);
    W(s, 0x48000000u | F);
    for (int i = 0; i <= n; i++) W(s, Q(192, (i & 1) ? dx : 0, 32 * i));
    W(s, 0x55555555u);
}
static void defs() {
    line11("level 1", 0x40, 0, 0);
    line11("level 2", 0x40, 1, 0);
    line11("level 3", 0x40, 2, 0);
    line11("level 4", 0x40, 3, 0);
    line11("level 5", 0x40, 4, 0);
    line11("level 6", 0x40, 5, 0);
    line11("level 7", 0x40, 6, 0);
    line11("level 8", 0x40, 7, 0);
    line11("level 10", 0x40, 9, 0);
    line11("level 12", 0x40, 11, 0);
    line11("level 14", 0x40, 13, 0);
    line11("level 16", 0x40, 15, 0);
    line11("level 20", 0x40, 19, 0);
    line11("level 24", 0x40, 23, 0);
    line11("level 28", 0x40, 27, 0);
    line11("rise1 3", 0x40, 2, 1);
    line11("rise1 4", 0x40, 3, 1);
    line11("rise1 5", 0x40, 4, 1);
    line11("rise1 7", 0x40, 6, 1);
    line11("rise1 9", 0x40, 8, 1);
    line11("rise1 13", 0x40, 12, 1);
    line11("gour level 4", 0x50, 3, 0);
    line11("gour level 16", 0x50, 15, 0);
    polySteep("steep 1x32,32", 1, 32);
    polySteep("steep 2x32,32", 2, 32);
    polySteep("steep 1x16,32", 1, 16);
    polySteep("steep 2x16,32", 2, 16);
    polySteep("steep 1x11,32", 1, 11);
    polySteep("steep 2x11,32", 2, 11);
    polySteep("steep 1x8,32", 1, 8);
    polySteep("steep 2x8,32", 2, 8);
    polySteep("steep 1x4,32", 1, 4);
    polySteep("steep 2x4,32", 2, 4);
    polySteep("steep 1x1,32", 1, 1);
    polySteep("steep 2x1,32", 2, 1);
    polySteep("steep 1x0,32", 1, 0);
    polySteep("steep 2x0,32", 2, 0);
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
    printf("\ngpu/gpu-shape-time64\n");
    printf("A shape's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set\n");
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
    printf("\ngpu/gpu-shape-time64: Done.\n");
    for (;;) VSync(0);
}
