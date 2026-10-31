/*
 * gpu/gp0-bit25c - when a polygon's words come back into the FIFO's count: GPUSTAT.25 by the cycle after its last word.
 *
 * WHY (2026-10-31). hwtests/gp0-bit25b on the second SCPH-5501: 200 cycles after a 12-word GP0(3Ch) or a 9-word GP0(34h) bit 25 reads 0 with GP1(04h)=1, nine or more words counted, and the quad stays so through its first
 * half. But nocash's DIAG, TEST GPU NUM PARAMS, graded on his console, reads bit 25 as 1 after every word of a 3Ch, the last too: a polygon collecting its vertices does not fill the FIFO. So a count that was low at the last
 * word is 9 by 200 cycles later. v0.377.225 needs the moment, and whether it falls at once, when the set-up ends, or in steps.
 *
 * WHAT IT DOES. gpu-degen's probe: interrupts off, the words stored, D cycles in a 3-cycle cached loop, ONE GPUSTAT read, GP1(04h)=1, an idle GPU each time, in the vertical blanking. D = 0, 8, 16 ... 800 for: a 64 x 64 3Ch, 34h, 2Ch,
 * 24h, 38h and 30h. A map a kind: bit 25, then bits 26 and 28.
 *
 * READ IT LIKE THIS. A character a D, 8 cycles apart. Bit 25 falling and rising against bit 28's rise (the set-up's end) is the rule. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gp0-bit25c --ntsc --raw --frames=2000`. PREDICTED.txt
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

enum { GT_QUAD, GT_TRI, T_QUAD, T_TRI, G_QUAD, G_TRI, N_KIND };
static const char* const KIND[N_KIND] = { "stq 3Ch", "sht 34h", "tq 2Ch", "tex 24h", "gq 38h", "gou 30h" };
static const uint32_t CMD[N_KIND] = { 0x3C000000u, 0x34000000u, 0x2C000000u, 0x24000000u, 0x38000000u, 0x30000000u };
static const int VX[4] = { 320, 384, 320, 384 }, VY[4] = { 200, 200, 264, 264 };
static const uint32_t CV[4] = { 0x102030u, 0x705030u, 0x3070B0u, 0xE0C0A0u };
static Shape shape(int k) {
    Shape s; s.n = 0;
    const int nv = (CMD[k] & 0x08000000u) ? 4 : 3, gour = (CMD[k] & 0x10000000u) != 0, tex = (CMD[k] & 0x04000000u) != 0;
    for (int i = 0; i < nv; i++) {
        if (i == 0) W(s, CMD[k] | (gour ? CV[0] : COL));
        else if (gour) W(s, CV[i]);
        W(s, xy(VX[i], VY[i]));
        if (tex) W(s, uv(i * 16, i * 8, i == 0 ? CLUT : i == 1 ? TPAGE : 0));
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
    printf("\ngpu/gp0-bit25c\n");
    printf("GPUSTAT 25, 26, 28 by the cycle after a polygon's last word, GP1(04h)=1. No assertions.\n\n");
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

    write32(GP1, 0x04000001u);
    static char b25[102], b26[102], b28[102];
    for (int k = 0; k < N_KIND; k++) {
        const Shape sh = shape(k);
        probe(sh, 4000); waitReady();    /* the code warm */
        for (int i = 0; i <= 100; i++) {
            sync(0);
            const uint32_t st = probe(sh, (uint32_t)i * 8);
            b25[i] = (st >> 25) & 1 ? '1' : '0'; b26[i] = (st >> 26) & 1 ? '1' : '0'; b28[i] = (st >> 28) & 1 ? '1' : '0';
        }
        b25[101] = b26[101] = b28[101] = 0;
        printf("%-8s words %d, D 0 to 800 by 8\n  25 %s\n  26 %s\n  28 %s\n", KIND[k], sh.n, b25, b26, b28);
    }
    write32(GP1, 0x04000000u);

    area(0, 0, 1023, 511);
    waitReady();
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gp0-bit25c: Done.\n");
    for (;;) VSync(0);
}
