/*
 * gpu/gpu-outside4 - lines outside the drawing area, and partly outside.
 *
 * WHY (2026-10-30 night). gpu-outside, -2 and -3 on the second SCPH-5501 have the rule for triangles and rectangles: rows are walked out from the
 * leftmost vertex and a walk past the area stops. Nothing here has read a line. psx-spx's dev text: "a primitive entirely outside the drawing
 * area still costs its setup plus 0.7 to 1.3 cycles for each row it would have covered", which for a level line would be one row, and
 * mednafen charges each clipped pixel of a line a tick. This core charges a line by its length, seen or not (GPU_LINE2, fitted on lines that draw).
 *
 * WHAT IT DOES. gpu-outside2's timing, area (256,136)-(511,375). Flat 40h, semi 42h and shaded 50h lines, three shapes each way round:
 *   H  (x,y) to (x+127,y)       and back
 *   V  (x,y) to (x,y+127)       and back
 *   D  (x,y) to (x+127,y+127)   and back
 * at INSIDE (320,200), LEFT (64,200), RIGHT (576,200), ABOVE (320,0), BELOW (320,384), and CROSS, across the area's top left corner (192,72).
 * A pixel inside the area is read back for the ones wholly outside.
 *
 * READ IT LIKE THIS. D fastest..slowest. INSIDE is the line's price. A line that pays only for rows reads H in the tens at every place outside; one that
 * walks its pixels reads as INSIDE. CROSS against INSIDE is what the hidden half saves. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-outside4 --ntsc --raw --frames=8000`. PREDICTED.txt
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

static const char* const KN[3] = { "flat 40h", "semi 42h", "shaded 50h" };
static const char* const SN[6] = { "H", "H back", "V", "V back", "D", "D back" };
static Shape line(int k, int sh, int x, int y) {
    Shape s; s.n = 0;
    const int dx = sh < 2 ? 127 : sh < 4 ? 0 : 127, dy = sh < 2 ? 0 : 127, back = sh & 1;
    const int x0 = back ? x + dx : x, y0 = back ? y + dy : y, x1 = back ? x : x + dx, y1 = back ? y : y + dy;
    W(s, (k == 0 ? 0x40000000u : k == 1 ? 0x42000000u : 0x50000000u) | COL); W(s, xy(x0, y0));
    if (k == 2) W(s, 0x204060u);
    W(s, xy(x1, y1));
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
    printf("\ngpu/gpu-outside4\n");
    printf("Lines 128 long inside, outside and across the drawing area (256,136)-(511,375). D as gpu-shape-time, fastest..slowest of eight start points. No assertions.\n\n");
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

    static const struct { const char* name; int x, y; } PL[6] = { { "inside", 320, 200 }, { "left", 64, 200 }, { "right", 576, 200 }, { "above", 320, 0 }, { "below", 320, 384 }, { "cross", 192, 72 } };
    printf("                     inside      left        right       above       below       cross\n");
    for (int k = 0; k < 3; k++)
        for (int h = 0; h < 6; h++) {
            printf("%-10s %-7s", KN[k], SN[h]);
            for (int p = 0; p < 6; p++) { const Range r = measure(line(k, h, PL[p].x, PL[p].y)); printf(" %4u..%-5u", (unsigned)r.lo, (unsigned)r.hi); }
            printf("\n");
        }

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-outside4: Done.\n");
    for (;;) VSync(0);
}
