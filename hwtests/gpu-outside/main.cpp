/*
 * gpu/gpu-outside - what a primitive wholly outside the drawing area costs, on each side of it.
 *
 * WHY (2026-10-30). psx-spx's dev text: "A primitive entirely outside the drawing area still costs its setup plus 0.7 to 1.3
 * cycles for each row it would have covered. A rectangle of width 0 costs the same; one of height 0 costs only its setup."
 * It names no side. hwtests/gpu-shape-time6's "clip TR 128x16 -192", a triangle wholly LEFT of the area, reads 117 on the
 * SCPH-9001 and 115 on the 1001 against 37 for a one-pixel triangle: about 5 a row, on two GPUs. This core charges a triangle
 * 4.6 a row beside the area on both GPUs, a rectangle 4.1 on the new and 6.0 on the old, and nothing above or below it. So
 * either psx-spx measured above or below, which would answer the core's open question there, or it conflicts with two consoles.
 *
 * WHAT IT DOES. gpu-shape-time's exact timing, its probe unchanged, so D reads as in those logs: interrupts off, the last
 * words stored back to back, D cycles in a 3-cycle cached loop, ONE GPUSTAT read, D binary-searched for bit 26, at eight
 * start points an eighth of a line apart in the vertical blanking, one probe a frame (each row about 60 frames: the first
 * start searched whole, the rest from a bracket round it, searched whole only if they leave it). The drawing area is
 * (256,136)-(511,375), so the tallest shape above or below it still ends clear of it: a triangle whose last vertex lands on
 * the area's first row is not wholly outside (on this core it cost 7 more). Flat primitives 128 wide, H = 16, 64 and 128 rows:
 *   triangle   (x,y) (x+128,y) (x+128,y+H), gpu-shape-time6's "TR": its first edge, so its set-up, does not change with H
 *   rectangle  GP0(60h) at (x,y), 128 x H
 * at x,y = LEFT (64,136), RIGHT (576,136), ABOVE (320,0), BELOW (320,384), CORNER (64,0), above and to the left at once.
 * Controls: the same two INSIDE, a pixel of each read back to show it drew and the outside ones did not;
 * a triangle of no height inside and at LEFT, its set-up alone (psx-spx: a flat degenerate polygon costs its setup); the anchor,
 * gpu-shape-time6's own shape, area (0,0)-(1023,511), from X -192. Then a rectangle of width 0, H = 16, 64, 128, and one of height
 * 0, 128 wide, both inside.
 *
 * READ IT LIKE THIS. D fastest..slowest for each H, then cycles a row from H = 16 to 128 by the fastest (and 16 to 64, which
 * must agree or it is not a rate). The anchor must read as in the gpu-shape-time6 logs (117 new GPU, 115 old) or this run is
 * not that measurement. About 5 a row at LEFT and RIGHT is the logs again; psx-spx's 0.7-1.3 at ABOVE or BELOW is the side it
 * measured, and a near 0 there is this core's rule. A row's cost is D less the no-height triangle's. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js gpu-outside --raw --frames=4000`. PREDICTED.txt
 * has what this core read before any console ran it.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define I_MASK   0x1F801074u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define B26 0x04000000u
#define B27 0x08000000u
#define MAXD 2047u                      /* a ceiling for what draws nothing, and said so when reached: 128 rows at 15 a row */
#define MAXD_DRAWN 16383u               /* for what draws: 128x128 at 1.5 a pixel, well inside the blanking */
#define SENTINEL 0x7C1Fu
#define INK 0x7FFFu

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

/* gpu-shape-time6's probe, unchanged: the same D is the same cycle */
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

struct Shape { int n; uint32_t w[4]; };
static uint32_t probe(const Shape& s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return s.n == 3 ? probe3(s.w, n, k) : probe4(s.w, n, k);
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
/* the first D in lo..hi-1 at which bit 26 reads set, hi if none */
static uint32_t search(const Shape& s, int j, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(j);
        const uint32_t st = probe(s, mid);
        if (st & B26) hi = mid; else lo = mid + 1;
    }
    return lo;
}
struct Range { uint32_t lo, hi; };
static Range measure(const Shape& s, uint32_t maxd) {
    waitReady(); probe(s, maxd); waitReady(); probe(s, maxd); waitReady();   /* the code warm */
    const uint32_t r0 = search(s, 0, 0, maxd + 1);
    Range r = { r0, r0 };
    for (int j = 1; j < 8; j++) {
        const uint32_t lo = r0 > 16 ? r0 - 16 : 0, hi = r0 + 112;
        uint32_t x = search(s, j, lo, hi);
        if (x == lo || x >= hi) x = search(s, j, 0, maxd + 1);
        r.lo = x < r.lo ? x : r.lo; r.hi = x > r.hi ? x : r.hi;
    }
    return r;
}
static Shape tri(int x, int y, int w, int h) {
    Shape s; s.n = 4;
    s.w[0] = 0x20FFFFFFu; s.w[1] = xy(x, y); s.w[2] = xy(x + w, y); s.w[3] = xy(x + w, y + h);
    return s;
}
static Shape rect(int x, int y, int w, int h) {
    Shape s; s.n = 3;
    s.w[0] = 0x60FFFFFFu; s.w[1] = xy(x, y); s.w[2] = xy(w, h);
    return s;
}
static void area(int x0, int y0, int x1, int y1) {
    waitReady();
    write32(GP0, 0xE3000000u | ((uint32_t)y0 << 10) | (uint32_t)x0);
    write32(GP0, 0xE4000000u | ((uint32_t)y1 << 10) | (uint32_t)x1);
}
/* one pixel by the CPU, bounded: FFFFFFFF if the read never came ready */
static uint32_t pixel(int x, int y) {
    waitReady();
    write32(GP0, 0xC0000000u); write32(GP0, xy(x, y)); write32(GP0, xy(1, 1));
    const uint32_t t0 = now();
    while (!(read32(GP1) & B27)) if (now() - t0 > 400000u) return 0xFFFFFFFFu;
    return read32(GP0) & 0xFFFFu;
}
/* the fastest's cycles a row, printed as a.bb, between two heights */
static void rate(uint32_t d0, uint32_t d1, int rows) {
    if (d1 < d0) { printf(" -%u.%02u", (unsigned)((d0 - d1) / rows), (unsigned)(((d0 - d1) % rows) * 100 / rows)); return; }
    printf(" %u.%02u", (unsigned)((d1 - d0) / rows), (unsigned)(((d1 - d0) % rows) * 100 / rows));
}
static void one(const char* name, const Shape& s) {
    const Range r = measure(s, MAXD);
    printf("%-26s %5u..%-5u%s\n", name, (unsigned)r.lo, (unsigned)r.hi, r.hi > MAXD ? "  CEILING" : "");
}
static void three(const char* place, const char* prim, int x, int y, int isTri, uint32_t maxd) {
    static const int H[3] = { 16, 64, 128 };
    Range r[3];
    for (int i = 0; i < 3; i++) { const Shape s = isTri ? tri(x, y, 128, H[i]) : rect(x, y, 128, H[i]); r[i] = measure(s, maxd); }
    const uint32_t px = pixel(x + 100, y + 8);
    printf("%-6s %-4s %5u..%-5u %5u..%-5u %5u..%-5u  a row", place, prim, (unsigned)r[0].lo, (unsigned)r[0].hi, (unsigned)r[1].lo, (unsigned)r[1].hi, (unsigned)r[2].lo, (unsigned)r[2].hi);
    rate(r[0].lo, r[2].lo, 112); rate(r[0].lo, r[1].lo, 48);
    printf("  pixel %s%s\n", px == SENTINEL ? "untouched" : px == INK ? "DRAWN" : "OTHER", r[2].hi > maxd ? "  CEILING" : "");
}

int main() {
    printf("\ngpu/gpu-outside\n");
    printf("Flat primitives, 128 wide, H rows, wholly outside the drawing area (256,136)-(511,375). D as gpu-shape-time: the first D with\n");
    printf("bit 26 set, fastest..slowest over eight start points in the blanking. A row: 16 to 128, then 16 to 64. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    area(0, 0, 1023, 511);
    /* every place a primitive can reach, the sentinel (a fill ignores the area) */
    write32(GP0, 0x02F800F8u); write32(GP0, xy(0, 0)); write32(GP0, xy(1008, 511));
    waitReady();

    printf("anchor, area (0,0)-(1023,511):\n");
    one("clip TR 128x16 -192", tri(-192, 128, 128, 16));
    printf("  gpu-shape-time6 logs: 117 on the new GPU, 115 on the old\n\n");

    area(256, 136, 511, 375);
    printf("                  H 16          H 64          H 128\n");
    three("inside", "tri", 320, 136, 1, MAXD_DRAWN);      /* these two must read DRAWN */
    three("inside", "rect", 320, 200, 0, MAXD_DRAWN);
    three("left", "tri", 64, 136, 1, MAXD);
    three("left", "rect", 64, 136, 0, MAXD);
    three("right", "tri", 576, 136, 1, MAXD);
    three("right", "rect", 576, 136, 0, MAXD);
    three("above", "tri", 320, 0, 1, MAXD);
    three("above", "rect", 320, 0, 0, MAXD);
    three("below", "tri", 320, 384, 1, MAXD);
    three("below", "rect", 320, 384, 0, MAXD);
    three("corner", "tri", 64, 0, 1, MAXD);
    three("corner", "rect", 64, 0, 0, MAXD);

    /* LEFT and the anchor read 64 cycles over RIGHT, at a distance of 192 both times (2026-10-30): a constant, or the distance? */
    printf("\nleft again, by the left edge's distance from the area (x 120, 0, -64, -256):\n");
    three("d136", "tri", 120, 136, 1, MAXD);
    three("d256", "tri", 0, 136, 1, MAXD);
    three("d320", "tri", -64, 136, 1, MAXD);
    three("d512", "tri", -256, 136, 1, MAXD);
    three("d136", "rect", 120, 136, 0, MAXD);
    three("d256", "rect", 0, 136, 0, MAXD);
    three("d512", "rect", -256, 136, 0, MAXD);
    three("right far", "tri", 900, 136, 1, MAXD);

    printf("\nset-up alone and empty rectangles, inside the area:\n");
    one("triangle no height inside", tri(320, 136, 128, 0));
    one("triangle no height left", tri(64, 136, 128, 0));
    one("rectangle 128 x 0", rect(320, 136, 128, 0));
    one("rectangle 0 x 16", rect(320, 136, 0, 16));
    one("rectangle 0 x 64", rect(320, 136, 0, 64));
    one("rectangle 0 x 128", rect(320, 136, 0, 128));

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-outside: Done.\n");
    for (;;) {}
}
