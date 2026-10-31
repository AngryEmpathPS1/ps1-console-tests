/*
 * gpu/gpu-rect-cached - a textured rectangle whose texels are all in the cache, by width and height, drawn and transparent.
 *
 * WHY (2026-10-30 night). hwtests/gpu-transp-cached on the second SCPH-5501: a raw 15-bit 64x64 rectangle cached reads 2,206..2,220 with 0001h texels and 2,127 with
 * 0000h, against this core's 2,123 and 2,122, and 7Dh 16x16 in a stream 174.0 against 174.5. psx-spx's table has cached rectangles at 0.53 a pixel against
 * GPU_TEXR2's fit, -6% at its sizes. So drawn texels are right small and cheap large, or the row is. One size cannot say which, and sprites are most of what a
 * 2D game draws. A flat rectangle's row is paid in pixel pairs (GPU_RECTF3): is a cached one's, and is a transparent texel's pair the same?
 *
 * WHAT IT DOES. gpu-transp-cached's CACHED row: gpu-shape-time's exact timing, GP0(E2h) masking U and V to 16, so the 16x16 at the page's corner is read over and over, loaded by the
 * draw before and hit by every texel of the draw measured. Raw GP0(65h) at (0,0), W = 8, 16, 32, 64, 128, 256 by H = 8, 16, 64: 0001h texels, then 0000h; and modulated GP0(64h) 0001h at H = 16.
 *
 * READ IT LIKE THIS. D for each cell, fastest..slowest. Beside GPU_RECTF3's flat row (2 + 1.004 p for p pairs at 16 and over) on D(W, 64) - D(W, 8) over 56 rows. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-rect-cached --ntsc --raw --frames=5000`. PREDICTED.txt
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
static Range cell(uint32_t cmd, int w, int h) {
    SPRITE[0] = cmd; SPRITE[3] = ((uint32_t)h << 16) | (uint32_t)w;
    return measure(0);
}

int main() {
    printf("\ngpu/gpu-rect-cached\n");
    printf("Textured rectangles whose texels are all cached, by width and height. D as gpu-shape-time. No assertions.\n\n");
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

    static const int WS[6] = { 8, 16, 32, 64, 128, 256 }, HS[3] = { 8, 16, 64 };
    printf("D fastest..slowest         W 8           16            32            64            128           256\n");
    for (int t = 1; t >= 0; t--) {
        texels(t);
        for (int h = 0; h < 3; h++) {
            printf("raw %s H %-3d  ", t ? "0001h" : "0000h", HS[h]);
            for (int i = 0; i < 6; i++) { const Range r = cell(0x65808080u, WS[i], HS[h]); printf(" %6u..%-6u", (unsigned)r.lo, (unsigned)r.hi); }
            printf("\n");
        }
    }
    texels(1);
    printf("mod 0001h H 16   ");
    for (int i = 0; i < 6; i++) { const Range r = cell(0x64808080u, WS[i], 16); printf(" %6u..%-6u", (unsigned)r.lo, (unsigned)r.hi); }
    printf("\n");

    waitReady();
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE1000400u);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-rect-cached: Done.\n");
    for (;;) {}
}
