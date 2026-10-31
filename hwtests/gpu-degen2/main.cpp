/*
 * gpu/gpu-degen22 - when does a polygon of no area pay its 13 cycles a gradient?
 *
 * WHY (2026-10-31). hwtests/gpu-degen on the second SCPH-5501: a shaded textured quad on one point inside the drawing area, all its colours and texels different, takes 735 cycles. But nocash's DIAG, TEST GPU NUM PARAMS,
 * graded on his console, sends GP0(3Ch) and eleven words of 55555555h and reads GPUSTAT ready about 530 cycles after the last: the same shape, all four vertices at (-683,-683), its texels equal, its colours three alike and
 * one zero. Outside the area, or the attributes equal? This core stands at 735 and fails that page.
 *
 * WHAT IT DOES. gpu-degen's timing, area (0,0)-(1023,511), offset 0. Kinds shaded 30h, textured 24h, shaded textured 34h, quads 38h, 2Ch, 3Ch; each five ways:
 *   DIAG   the command word, then 55555555h, DIAG's own words
 *   IN     a point at (320,200), attributes all different (gpu-degen's row)
 *   SAME   a point at (320,200), attributes all alike
 *   OUT    a point at (-683,-683), attributes all different
 *   OUT1   a point at (1100,200) past the right, attributes all different
 *
 * READ IT LIKE THIS. D fastest..slowest. IN is gpu-degen's. Which of the others lose the 13s is the rule. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-degen2 --ntsc --raw --frames=4000`. PREDICTED.txt
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
#define MAXD 4095u                      /* a ceiling, well over psx-spx's 460 */
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

enum { G_TRI, T_TRI, GT_TRI, G_QUAD, T_QUAD, GT_QUAD, N_KIND };
static const char* const KIND[N_KIND] = { "shaded 30h", "tex 24h", "shtex 34h", "gquad 38h", "tquad 2Ch", "stq 3Ch" };
static const uint32_t CMD[N_KIND] = { 0x30000000u, 0x24000000u, 0x34000000u, 0x38000000u, 0x2C000000u, 0x3C000000u };
static const uint32_t CV[4] = { 0x102030u, 0x705030u, 0x3070B0u, 0xE0C0A0u };
enum { DIAGW, IN, SAME, OUT, OUT1, N_SHAPE };
static Shape shape(int k, int sh) {
    Shape s; s.n = 0;
    const int nv = (CMD[k] & 0x08000000u) ? 4 : 3, gour = (CMD[k] & 0x10000000u) != 0, tex = (CMD[k] & 0x04000000u) != 0;
    const int words = 1 + nv + (gour ? nv - 1 : 0) + (tex ? nv : 0);
    if (sh == DIAGW) { W(s, CMD[k]); for (int i = 1; i < words; i++) W(s, 0x55555555u); return s; }
    const int x = sh == OUT ? -683 : sh == OUT1 ? 1100 : 320, y = sh == OUT ? -683 : 200, same = sh == SAME;
    for (int i = 0; i < nv; i++) {
        if (i == 0) W(s, CMD[k] | (gour ? CV[0] : COL));
        else if (gour) W(s, same ? CV[0] : CV[i]);
        W(s, xy(x, y));
        if (tex) W(s, same ? uv(8, 8, i == 0 ? CLUT : i == 1 ? TPAGE : 0) : uv(i * 16, i * 8, i == 0 ? CLUT : i == 1 ? TPAGE : 0));
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
    printf("\ngpu/gpu-degen2\n");
    printf("Polygons with no area by place and attributes. D as gpu-shape-time, fastest..slowest of eight start points. No assertions.\n\n");
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

    printf("           DIAG's words  in the area  all alike    (-683,-683)  (1100,200)\n");
    for (int k = 0; k < N_KIND; k++) {
        printf("%-10s", KIND[k]);
        for (int h = 0; h < N_SHAPE; h++) { const Range r = measure(shape(k, h)); printf(" %4u..%-5u", (unsigned)r.lo, (unsigned)r.hi); }
        printf("\n");
    }

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-degen2: Done.\n");
    for (;;) VSync(0);
}
