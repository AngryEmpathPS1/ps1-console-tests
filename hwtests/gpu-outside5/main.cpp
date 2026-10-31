/*
 * gpu/gpu-outside5 - what a primitive clipped by the left and right edges of the drawing area pays for the part it does not draw.
 *
 * WHY (2026-10-30 night). hwtests/gpu-outside3 on the second SCPH-5501: wholly left of the area a primitive pays once for its width, W / 2 or W, a walk over pixels it will not
 * draw, and right of the area nothing. Every sprite a 2D game scrolls off the left of the screen is the case in between, and this core charges it the pixels it draws and nothing for the
 * rest. Does the hidden left part cost once, or by the row, and is the right part free?
 *
 * WHAT IT DOES. gpu-outside3's timing, area (256,136)-(511,375). Rectangles 128 x 64: flat 60h, raw textured 65h cached (a 16 x 16 window), semi 62h; triangles 128 wide, 64 tall, flat 20h,
 * shaded 30h, textured 24h, flat-topped (x,y) (x+128,y) (x+128,y+64) and (x,y) (x+128,y) (x,y+64). INSIDE at x 320; LEFT with d = 8, 32, 64, 96, 120 pixels left of the edge (x = 256 - d);
 * RIGHT with d pixels past it (x = 512 - 128 + d).
 *
 * READ IT LIKE THIS. A row of D by d. Inside less the part not drawn (128 - d of 128 for a rectangle) is what the drawn part costs: the excess at LEFT is what the hidden part pays, against d:
 * a constant at each d is once, H x that is by the row. RIGHT's excess should be none. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-outside5 --ntsc --raw --frames=9000`. PREDICTED.txt
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
#define MAXD 2047u                      /* a ceiling for what draws nothing, and said so when reached */
#define MAXD_DRAWN 16383u               /* for what draws: 128x128 at 1.5 a pixel, well inside the blanking */
#define SENTINEL 0x7C1Fu

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

struct Shape { int n; uint32_t w[12]; };
/* count words from memory, the last one's store then gpu-shape-time6's wait and read */
static uint32_t probe(const Shape& s, uint32_t d) {
    uint32_t n = d / 3, st;
    const uint32_t k = d - 3 * n;
    uint32_t c = (uint32_t)s.n;
    const uint32_t* p = s.w;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "la $t1, 2f\n"
        "sll $t2, %[k], 2\n"
        "subu $t1, $t1, $t2\n"
        "1:\n"
        "lw $t3, 0(%[p])\n"
        "addiu %[c], %[c], -1\n"
        "addiu %[p], %[p], 4\n"
        "bnez %[c], 1b\n"
        "sw $t3, 0x1810($t0)\n"
        "beqz %[n], 3f\n"
        "nop\n"
        "4:\n"
        "addiu %[n], %[n], -1\n"
        "bnez %[n], 4b\n"
        "nop\n"
        "3:\n"
        "jr $t1\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "2:\n"
        "lw %[st], 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : [st] "=r"(st), [n] "+r"(n), [c] "+r"(c), [p] "+r"(p) : [k] "r"(k) : "t0", "t1", "t2", "t3", "memory");
    return st;
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
static Range measure(const Shape& s, uint32_t maxd = MAXD) {
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

static const uint32_t COL = 0xFFFFFFu & 0x808080u;
static const uint32_t CLUT = (480u << 6) | 0u;
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static void W(Shape& s, uint32_t v) { s.w[s.n++] = v; }

enum { RF, RT, RS, TF, TG, TT, LF, LG, LT, N_KIND };
static const char* const KN[N_KIND] = { "rect 60h", "rect 65h", "rect 62h", "TR flat", "TR shaded", "TR tex", "TL flat", "TL shaded", "TL tex" };
static Shape shapeOf(int k, int x, int y) {
    Shape s; s.n = 0;
    const int x1 = x + 128, y1 = y + 64;
    if (k == RF) { W(s, 0x60000000u | COL); W(s, xy(x, y)); W(s, xy(128, 64)); }
    else if (k == RT) { W(s, 0x65000000u | COL); W(s, xy(x, y)); W(s, uv(0, 0, CLUT)); W(s, xy(128, 64)); }
    else if (k == RS) { W(s, 0x62000000u | COL); W(s, xy(x, y)); W(s, xy(128, 64)); }
    else {
        const int tl = k >= LF, g = k == TG || k == LG, t = k == TT || k == LT;
        const int xc = tl ? x : x1;
        W(s, (g ? 0x30000000u : t ? 0x24000000u : 0x20000000u) | COL); W(s, xy(x, y)); if (t) W(s, uv(0, 0, CLUT));
        if (g) W(s, COL);
        W(s, xy(x1, y)); if (t) W(s, uv(8, 0, TPAGE));
        if (g) W(s, COL);
        W(s, xy(xc, y1)); if (t) W(s, uv(8, 8, 0));
    }
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

int main() {
    printf("\ngpu/gpu-outside5\n");
    printf("128 x 64 primitives clipped by the left and right edges of the drawing area (256,136)-(511,375). D as gpu-shape-time, the fastest of eight start points. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    area(0, 0, 1023, 511);
    write32(GP0, 0x02F800F8u); write32(GP0, xy(0, 0)); write32(GP0, xy(1008, 511));   /* every place a primitive can reach, the sentinel */
    waitReady();
    area(256, 136, 511, 375);

    /* 16 x 16 of white texels at the page's corner, the window on them, so texels are in the cache, as gpu-rect-cached */
    write32(GP0, 0x02FFFFFFu); write32(GP0, xy(512, 256)); write32(GP0, xy(16, 16)); waitReady();
    write32(GP0, 0xE2000000u | 0x1Eu | (0x1Eu << 5));
    static const int DS[5] = { 8, 32, 64, 96, 120 };
    printf("                  inside   left d 8     32     64     96    120   right d 8     32     64     96    120\n");
    for (int k = 0; k < N_KIND; k++) {
        printf("%-12s", KN[k]);
        { const Range r = measure(shapeOf(k, 320, 200), MAXD_DRAWN); printf(" %8u  ", (unsigned)r.lo); }
        for (int i = 0; i < 5; i++) { const Range r = measure(shapeOf(k, 256 - DS[i], 200), MAXD_DRAWN); printf(" %6u", (unsigned)r.lo); }
        printf("  ");
        for (int i = 0; i < 5; i++) { const Range r = measure(shapeOf(k, 512 - 128 + DS[i], 200), MAXD_DRAWN); printf(" %6u", (unsigned)r.lo); }
        printf("\n");
    }
    write32(GP0, 0xE2000000u);

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-outside5: Done.\n");
    for (;;) VSync(0);
}
