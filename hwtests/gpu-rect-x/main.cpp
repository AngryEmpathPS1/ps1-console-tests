/*
 * gpu/gpu-rect-x - does a rectangle's cost depend on where it starts in x?
 *
 * WHY (2026-10-31). hwtests/gpu-outside5 on the second SCPH-5501: a flat rectangle clipped by the right edge to 8 wide at x 504 reads 433 over 64 rows, a textured one too, where gpu-rect-cached's free 8 x 64 at x 0
 * reads 404: 0.45 of a cycle a row more, from where it starts or from what was cut off. gpu-shape-time24 found an odd width's last pixel free and "the same at X 1", and its rows are all at x 0 or 1. Every sprite
 * in a game starts somewhere else. Pixel pairs from x 0? Blocks of 8 or 16 in VRAM?
 *
 * WHAT IT DOES. gpu-rect-cached's timing and its cached 16 x 16: rectangles 64 rows tall, W = 4, 8, 16, 32 at x = 0, 1, 2, 3, 4, 6, 8, 12, 15, 16, 24 from a 256-aligned column: flat GP0(60h) and raw textured GP0(65h)
 * with 0001h texels, and the 8-wide at x 504, 508 and 496 inside a whole area, as the edge's, to tell the place from the clip.
 *
 * READ IT LIKE THIS. D by W and x. A column that does not move with x is gpu-shape-time24's rule everywhere. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-rect-x --ntsc --raw --frames=9000`. PREDICTED.txt
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
#define MAXD 24000u                     /* a ceiling, and said so when reached: 5.8 a pixel */
#define SENTINEL 0x7C1Fu
#define TPAGE (8u | (1u << 4) | (2u << 7))   /* X 512, Y 256, 15-bit */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

/* gpu-shape-time6's probe, unchanged */
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

static uint32_t SPRITE[4] = { 0x65808080u, 0x00000000u, 0x00000000u, (64u << 16) | 64u };
static uint32_t probe(uint32_t d) { const uint32_t n = d / 3, k = d - 3 * n; return probe4(SPRITE, n, k); }

static void flush() { waitReady(); write32(GP0, 0x01000000u); waitReady(); }
static void sync(int j, int uncached) {
    if (uncached) flush(); else waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
/* the first D in lo..hi-1 at which bit 26 reads set, hi if none */
static uint32_t search(int uncached, int j, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(j, uncached);
        const uint32_t st = probe(mid);
        if (st & B26) hi = mid; else lo = mid + 1;
    }
    return lo;
}
struct Range { uint32_t lo, hi; };
static Range measure(int uncached) {
    waitReady(); probe(MAXD); waitReady(); probe(MAXD); waitReady();   /* the code warm, and the cached row's texels */
    const uint32_t r0 = search(uncached, 0, 0, MAXD + 1);
    Range r = { r0, r0 };
    for (int j = 1; j < 8; j++) {
        const uint32_t lo = r0 > 32 ? r0 - 32 : 0, hi = r0 + 224;
        uint32_t x = search(uncached, j, lo, hi);
        if (x == lo || x >= hi) x = search(uncached, j, 0, MAXD + 1);
        r.lo = x < r.lo ? x : r.lo; r.hi = x > r.hi ? x : r.hi;
    }
    return r;
}
/* GP0(02h) with the 15-bit colour v (bit 15 clear) */
static void fill15(int x, int y, int w, int h, uint32_t v) {
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    waitReady();
    write32(GP0, 0x02000000u | (b << 16) | (g << 8) | r);
    write32(GP0, xy(x, y));
    write32(GP0, xy(w, h));
    waitReady();
}
/* one pixel by the CPU, bounded: FFFFFFFF if the read never came ready */
static uint32_t pixel(int x, int y) {
    waitReady();
    write32(GP0, 0xC0000000u); write32(GP0, xy(x, y)); write32(GP0, xy(1, 1));
    const uint32_t t0 = now();
    while (!(read32(GP1) & B27)) if (now() - t0 > 400000u) return 0xFFFFFFFFu;
    return read32(GP0) & 0xFFFFu;
}
static void texels(uint32_t texel) {
    fill15(512, 256, 256, 256, 0);
    if (texel) fill15(512, 256, 16, 16, texel);
    waitReady();
    write32(GP0, 0xE2000000u | 0x1Eu | (0x1Eu << 5));
    flush();
}
static Range cell(uint32_t cmd, int x, int w, int h) {
    SPRITE[0] = cmd; SPRITE[1] = (200u << 16) | (uint32_t)x; SPRITE[2] = 0; SPRITE[3] = ((uint32_t)h << 16) | (uint32_t)w;
    return measure(0);
}

int main() {
    printf("\ngpu/gpu-rect-x\n");
    printf("Cached textured rectangles 64 rows tall by where they start in x. D as gpu-shape-time. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    waitReady();

    static const int WS[4] = { 4, 8, 16, 32 }, XS[11] = { 0, 1, 2, 3, 4, 6, 8, 12, 15, 16, 24 };
    texels(1);
    printf("raw textured 65h, 64 rows, D fastest by x\n          x");
    for (int i = 0; i < 11; i++) printf(" %5d", XS[i]);
    printf("\n");
    for (int w = 0; w < 4; w++) {
        printf("W %-3d      ", WS[w]);
        for (int i = 0; i < 11; i++) { const Range r = cell(0x65808080u, 256 + XS[i], WS[w], 64); printf(" %5u", (unsigned)r.lo); }
        printf("\n");
    }
    printf("the edge's places, W 8, 64 rows:");
    { static const int EX[3] = { 496, 504, 508 }; for (int i = 0; i < 3; i++) { const Range r = cell(0x65808080u, EX[i], 8, 64); printf("  x %d %u", EX[i], (unsigned)r.lo); } }
    printf("\n");

    waitReady();
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE1000400u);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-rect-x: Done.\n");
    for (;;) {}
}
