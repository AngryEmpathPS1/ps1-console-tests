/*
 * gpu/gpu-outside2 - what does a SHADED, TEXTURED or SEMI-TRANSPARENT primitive wholly outside the drawing area cost?
 *
 * WHY (2026-10-30 night). hwtests/gpu-outside on the second SCPH-5501, x3: a flat triangle wholly outside costs 37 + H cycles, 101 + H
 * wholly left of the area, 37 below it; a flat rectangle 16 + H, 80 + H, 16. Flat is the rare one: what a game's clipper lets through
 * is textured and shaded, and this core charges those no rows outside at all, and their full set-up (shaded 31 + ?, textured ...)
 * from a fit made on polygons that draw. Does a textured triangle pay more set-up outside, and does the left's 64 belong to every
 * kind or to flat alone? The rule is by kind or it is by nothing.
 *
 * WHAT IT DOES. gpu-outside's exact timing: interrupts off, D cycles in a 3-cycle cached loop after the last word, ONE GPUSTAT read,
 * D binary-searched for bit 26, eight start points an eighth of a line apart in the blanking, one probe a frame. The drawing area
 * is (256,136)-(511,375) again. The words come from memory in a loop, not as gpu-shape-time's back-to-back stores, so the first row
 * of the table is gpu-outside's flat triangle again: that row reads 117 / 53 / 53 / 37 at H 16 or the two probes differ by what it
 * reads. Primitives 128 wide, H = 16 and 128, at LEFT (64,136), RIGHT (576,136), ABOVE (320,0), BELOW (320,384):
 *   triangles (x,y) (x+128,y) (x+128,y+H): flat 20h, semi 22h, shaded 30h, textured 24h, raw 25h, shaded textured 34h;
 *   quads (x,y) (x+128,y) (x,y+H) (x+128,y+H): flat 28h, shaded textured 3Ch;
 *   rectangles: semi 62h, textured 64h.
 * Then a pixel of each read back: outside means untouched.
 *
 * READ IT LIKE THIS. D fastest..slowest for each H, then the cycles a row. Set-up is D at H 16 less 16. Beside gpu-outside's flat
 * rows, the difference by kind is the rule. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-outside2 --ntsc --raw --frames=8000`. PREDICTED.txt
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
static Range measure(const Shape& s) {
    waitReady(); probe(s, MAXD); waitReady(); probe(s, MAXD); waitReady();   /* the code warm */
    const uint32_t r0 = search(s, 0, 0, MAXD + 1);
    Range r = { r0, r0 };
    for (int j = 1; j < 8; j++) {
        const uint32_t lo = r0 > 16 ? r0 - 16 : 0, hi = r0 + 112;
        uint32_t x = search(s, j, lo, hi);
        if (x == lo || x >= hi) x = search(s, j, 0, MAXD + 1);
        r.lo = x < r.lo ? x : r.lo; r.hi = x > r.hi ? x : r.hi;
    }
    return r;
}

static const uint32_t COL = 0xFFFFFFu & 0x808080u;
static const uint32_t CLUT = (480u << 6) | 0u;
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static void W(Shape& s, uint32_t v) { s.w[s.n++] = v; }

enum { F_TRI, S_TRI, G_TRI, T_TRI, R_TRI, GT_TRI, F_QUAD, GT_QUAD, S_RECT, T_RECT, N_KIND };
static const char* const KIND[N_KIND] = { "flat 20h", "semi 22h", "shaded 30h", "tex 24h", "raw 25h", "shtex 34h", "fquad 28h", "stq 3Ch", "semi 62h", "tex 64h" };
static Shape shape(int k, int x, int y, int h) {
    Shape s; s.n = 0;
    const int x1 = x + 128, y1 = y + h;
    switch (k) {
    case F_TRI: W(s, 0x20000000u | COL); W(s, xy(x, y)); W(s, xy(x1, y)); W(s, xy(x1, y1)); break;
    case S_TRI: W(s, 0x22000000u | COL); W(s, xy(x, y)); W(s, xy(x1, y)); W(s, xy(x1, y1)); break;
    case G_TRI: W(s, 0x30000000u | COL); W(s, xy(x, y)); W(s, COL); W(s, xy(x1, y)); W(s, COL); W(s, xy(x1, y1)); break;
    case T_TRI: case R_TRI:
        W(s, (k == T_TRI ? 0x24000000u : 0x25000000u) | COL); W(s, xy(x, y)); W(s, uv(0, 0, CLUT)); W(s, xy(x1, y)); W(s, uv(64, 0, TPAGE)); W(s, xy(x1, y1)); W(s, uv(64, 64, 0)); break;
    case GT_TRI:
        W(s, 0x34000000u | COL); W(s, xy(x, y)); W(s, uv(0, 0, CLUT)); W(s, COL); W(s, xy(x1, y)); W(s, uv(64, 0, TPAGE)); W(s, COL); W(s, xy(x1, y1)); W(s, uv(64, 64, 0)); break;
    case F_QUAD: W(s, 0x28000000u | COL); W(s, xy(x, y)); W(s, xy(x1, y)); W(s, xy(x, y1)); W(s, xy(x1, y1)); break;
    case GT_QUAD:
        W(s, 0x3C000000u | COL); W(s, xy(x, y)); W(s, uv(0, 0, CLUT)); W(s, COL); W(s, xy(x1, y)); W(s, uv(64, 0, TPAGE)); W(s, COL); W(s, xy(x, y1)); W(s, uv(0, 64, 0));
        W(s, COL); W(s, xy(x1, y1)); W(s, uv(64, 64, 0)); break;
    case S_RECT: W(s, 0x62000000u | COL); W(s, xy(x, y)); W(s, xy(128, h)); break;
    default: W(s, 0x64000000u | COL); W(s, xy(x, y)); W(s, uv(0, 0, CLUT)); W(s, xy(128, h)); break;
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
    printf("\ngpu/gpu-outside2\n");
    printf("Primitives 128 wide, H rows, wholly outside the drawing area (256,136)-(511,375). D as gpu-shape-time: the first D with\n");
    printf("bit 26 set, fastest..slowest over eight start points in the blanking. No assertions.\n\n");
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

    static const struct { const char* name; int x, y; } PL[4] = { { "left", 64, 136 }, { "right", 576, 136 }, { "above", 320, 0 }, { "below", 320, 384 } };
    printf("                     H 16          H 128     a row\n");
    for (int k = 0; k < N_KIND; k++) {
        for (int p = 0; p < 4; p++) {
            const Range a = measure(shape(k, PL[p].x, PL[p].y, 16)), b = measure(shape(k, PL[p].x, PL[p].y, 128));
            const uint32_t px = pixel(PL[p].x + 100, PL[p].y + 8);
            const uint32_t d = b.lo > a.lo ? b.lo - a.lo : 0;
            printf("%-10s %-5s %5u..%-5u %5u..%-5u %u.%02u  pixel %s%s\n", KIND[k], PL[p].name, (unsigned)a.lo, (unsigned)a.hi, (unsigned)b.lo, (unsigned)b.hi,
                   (unsigned)(d / 112), (unsigned)((d % 112) * 100 / 112), px == SENTINEL ? "untouched" : "TOUCHED", b.hi > MAXD ? "  CEILING" : "");
        }
    }

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-outside2: Done.\n");
    for (;;) VSync(0);
}
