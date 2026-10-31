/*
 * gpu/gpu-outside3 - the rows of a primitive outside the drawing area: which it pays, and what the left of the area costs.
 *
 * WHY (2026-10-30 night). hwtests/gpu-outside2 on the second SCPH-5501: of a flat-topped triangle wholly outside, a row costs 1 cycle
 * above the area and beside it and nothing below, and of a quad 0.92 above and 1.04 below, as if its second triangle, flat at the
 * bottom, paid the other way round. nocash's and mednafen's rasteriser walks out from the triangle's LEFTMOST vertex, up and down,
 * and a walk that has left the area in the direction it goes can stop. So a row would be paid if the walk has not yet passed the
 * area, and that is what the numbers look like, but one shape at four places cannot show it. And the left: wholly left of the area
 * costs a constant more at any distance, 64 flat, 128 shaded or textured, 64 for every rectangle, always at 128 wide. Does it go with
 * the width? And none of it says what the rows hidden above or below a triangle that DOES draw cost, which is every polygon a game
 * clips against the top of the screen.
 *
 * WHAT IT DOES. gpu-outside2's timing, area (256,136)-(511,375).
 *  A. WIDTH. Rectangle 60h, flat 20h, shaded 30h, textured 24h triangles (top flat, right angle at the top right), W = 16, 32, 64, 128,
 *     256 wide, 16 rows, 8 pixels left of the area, and the same at RIGHT.
 *  B. WALK. Flat triangles 128 wide, H = 16 and 128, at LEFT, RIGHT, ABOVE and BELOW, by which vertex is leftmost:
 *     TR (x,y) (x+W,y) (x+W,y+H), leftmost on top; BR (x+W,y) (x,y+H) (x+W,y+H), leftmost at the bottom; ML (x+W,y) (x,y+H/2) (x+W,y+H), in the
 *     middle; TL (x,y) (x+W,y) (x,y+H) and BL (x,y) (x,y+H) (x+W,y+H), two leftmost; RM (x,y) (x+W,y+H/2) (x,y+H), two leftmost with
 *     the right in the middle.
 *  C. CLIPPED. TR, BR, ML and a rectangle, 128 x 128, at x 320: wholly inside (y 200), crossing the top (y 72, 64 rows above), crossing the
 *     bottom (y 311, 64 rows below).
 *
 * READ IT LIKE THIS. Set-up is the H 16 cell less 16. A row's cost by shape and place is the rule: TR above 1, below 0; BR above 0,
 * below 1, if the walk is what it looks like. C: inside less the clipped is what the hidden rows' drawing was worth; a hidden row
 * paid shows as the clipped reading above half the inside one. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-outside3 --ntsc --raw --frames=9000`. PREDICTED.txt
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

enum { KR, KF, KG, KT };
static const char* const KN[4] = { "rect 60h", "flat 20h", "shaded 30h", "tex 24h" };
static Shape kindW(int k, int x, int y, int w, int h) {
    Shape s; s.n = 0;
    const int x1 = x + w, y1 = y + h;
    switch (k) {
    case KR: W(s, 0x60000000u | COL); W(s, xy(x, y)); W(s, xy(w, h)); break;
    case KF: W(s, 0x20000000u | COL); W(s, xy(x, y)); W(s, xy(x1, y)); W(s, xy(x1, y1)); break;
    case KG: W(s, 0x30000000u | COL); W(s, xy(x, y)); W(s, COL); W(s, xy(x1, y)); W(s, COL); W(s, xy(x1, y1)); break;
    default: W(s, 0x24000000u | COL); W(s, xy(x, y)); W(s, uv(0, 0, CLUT)); W(s, xy(x1, y)); W(s, uv(64, 0, TPAGE)); W(s, xy(x1, y1)); W(s, uv(64, 64, 0)); break;
    }
    return s;
}
enum { TR, BR, ML, TL, BL, RM, SRECT, N_SHAPE };
static const char* const SN[N_SHAPE] = { "TR", "BR", "ML", "TL", "BL", "RM", "rect" };
static Shape tri3(int a, int b, int c, int d, int e, int f) {
    Shape s; s.n = 0;
    W(s, 0x20000000u | COL); W(s, xy(a, b)); W(s, xy(c, d)); W(s, xy(e, f));
    return s;
}
static Shape shapeAt(int k, int x, int y, int h) {
    const int x1 = x + 128, y1 = y + h, ym = y + h / 2;
    switch (k) {
    case TR: return tri3(x, y, x1, y, x1, y1);
    case BR: return tri3(x1, y, x, y1, x1, y1);
    case ML: return tri3(x1, y, x, ym, x1, y1);
    case TL: return tri3(x, y, x1, y, x, y1);
    case BL: return tri3(x, y, x, y1, x1, y1);
    case RM: return tri3(x, y, x1, ym, x, y1);
    default: return kindW(KR, x, y, 128, h);
    }
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
    printf("\ngpu/gpu-outside3\n");
    printf("The rows of a primitive outside the drawing area (256,136)-(511,375). D as gpu-shape-time, the fastest of eight start points. No assertions.\n\n");
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

    printf("A. WIDTH: 16 rows, 8 pixels left of the area, then right of it\n");
    static const int WS[5] = { 16, 32, 64, 128, 256 };
    printf("             W 16   32   64  128  256\n");
    for (int k = 0; k < 4; k++) {
        printf("%-10s L", KN[k]);
        for (int i = 0; i < 5; i++) { const Range r = measure(kindW(k, 256 - WS[i] - 8, 200, WS[i], 16)); printf(" %4u", (unsigned)r.lo); }
        printf("\n%-10s R", KN[k]);
        for (int i = 0; i < 5; i++) { const Range r = measure(kindW(k, 576, 200, WS[i], 16)); printf(" %4u", (unsigned)r.lo); }
        printf("\n");
    }

    printf("\nB. WALK: flat, 128 wide; D at H 16, H 128, then cycles a row\n");
    static const struct { const char* name; int x, y; } PL[4] = { { "left", 64, 136 }, { "right", 576, 136 }, { "above", 320, 0 }, { "below", 320, 384 } };
    for (int k = 0; k < SRECT; k++)
        for (int p = 0; p < 4; p++) {
            const Range a = measure(shapeAt(k, PL[p].x, PL[p].y, 16)), b = measure(shapeAt(k, PL[p].x, PL[p].y, 128));
            const uint32_t d = b.lo > a.lo ? b.lo - a.lo : 0;
            printf("%-3s %-5s %5u..%-5u %5u..%-5u %u.%02u\n", SN[k], PL[p].name, (unsigned)a.lo, (unsigned)a.hi, (unsigned)b.lo, (unsigned)b.hi, (unsigned)(d / 112), (unsigned)((d % 112) * 100 / 112));
        }

    printf("\nC. CLIPPED: 128 x 128 at x 320: inside (y 200), 64 rows above the area (y 72), 64 rows below it (y 311)\n");
    static const int KC[4] = { TR, BR, ML, SRECT };
    for (int k = 0; k < 4; k++) {
        const Range a = measure(shapeAt(KC[k], 320, 200, 128), MAXD_DRAWN), b = measure(shapeAt(KC[k], 320, 72, 128), MAXD_DRAWN), c = measure(shapeAt(KC[k], 320, 311, 128), MAXD_DRAWN);
        printf("%-5s inside %5u..%-5u top %5u..%-5u bottom %5u..%-5u\n", SN[KC[k]], (unsigned)a.lo, (unsigned)a.hi, (unsigned)b.lo, (unsigned)b.hi, (unsigned)c.lo, (unsigned)c.hi);
    }

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-outside3: Done.\n");
    for (;;) VSync(0);
}
