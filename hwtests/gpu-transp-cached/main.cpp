/*
 * gpu/gpu-transp-cached - what a transparent texel saves when the texture is in the cache.
 *
 * WHY (2026-10-30). psx-spx's dev text: "A raw 15bpp 64x64 rectangle whose texels are all 0000h draws about 0.08 cycles per
 * pixel faster than one with visible texels on the v0 GPU and 0.04 faster on the v2 GPU when the texels are cached, and 0.26
 * (v0) and 0.38 (v2) faster when they are not." This core matches the uncached pair and charges nothing cached (0.001 /
 * 0.000): 13% of the v0's 64x64 draw and 7% of the v2's. They would be the first per-texel rates for the open question in the
 * core's textured rectangle. hwtests/gpu-tex-opaque polled at 45 cycles and drew through fills, so it could not see this.
 *
 * WHAT IT DOES. gpu-shape-time's exact timing, its probe unchanged: interrupts off, the words stored, the last four back to
 * back, D cycles in a 3-cycle cached loop, ONE GPUSTAT read, D binary-searched for bit 26, at eight start points an eighth of a
 * line apart in the vertical blanking, one probe a frame. A raw GP0(65h) rectangle, 64x64, at (0,0), from a 15-bit texture
 * page at (512,256), no semi-transparency, no mask:
 *   CACHED    GP0(E2h) masks U and V to 16 (0x1E both), so the rectangle reads the 16x16 at the page's corner over and over:
 *             64 cache lines in one block, loaded by the draw before and hit by every texel of the draw measured
 *   UNCACHED  no window, the texture 1:1, GP0(01h) waited for before each probe: as psx-spx's note defines it
 * each with 0001h texels (red 1: drawn) and 0000h (skipped). GP0(02h) writes the texels, so bit 15 is clear in both.
 * In the CACHED 0001h row only the 16x16 holds 0001h and the page around it 0000h, so a rectangle whose window failed would
 * leave holes: pixels of the rectangle are read back after each row, past the first 16, to show the window held and that
 * 0000h wrote nothing.
 *
 * READ IT LIKE THIS. D fastest..slowest for each, then the saving, 0001h less 0000h, in cycles and in cycles a pixel (of 4,096).
 * psx-spx's 0.08 on the old GPU is 328 cycles, 0.04 on the new 164; uncached 1,065 and 1,556. A saving near 0 cached and
 * psx-spx's uncached is this core. The cached rows should read about 0.5 a pixel and the uncached about 3-4; if they do not,
 * the row is not in the state it names. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js gpu-transp-cached --raw --frames=4000`. PREDICTED.txt
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

static const uint32_t SPRITE[4] = { 0x65808080u, 0x00000000u, 0x00000000u, (64u << 16) | 64u };
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
static Range row(const char* name, int uncached, uint32_t texel) {
    /* the page clean, the texels, the place drawn to the sentinel, then the cache told */
    fill15(512, 256, 256, 256, 0);
    if (texel) { const int n = uncached ? 64 : 16; fill15(512, 256, n, n, texel); }
    fill15(0, 0, 64, 64, SENTINEL);
    waitReady();
    write32(GP0, uncached ? 0xE2000000u : 0xE2000000u | 0x1Eu | (0x1Eu << 5));
    flush();
    const Range r = measure(uncached);
    const uint32_t a = pixel(0, 0), b = pixel(17, 17), c = pixel(40, 33), d = pixel(63, 63);
    printf("%-18s %6u..%-6u  rectangle reads %04x %04x %04x %04x%s\n", name, (unsigned)r.lo, (unsigned)r.hi,
           (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d, r.hi > MAXD ? "  CEILING" : "");
    return r;
}
static void saving(const char* name, const Range& drawn, const Range& skipped) {
    const int s = (int)drawn.lo - (int)skipped.lo, m = s < 0 ? -s : s;
    printf("%s: 0001h less 0000h %d cycles, %s%d.%03d a pixel\n", name, s, s < 0 ? "-" : "", m / 4096, (m % 4096) * 1000 / 4096);
}

int main() {
    printf("\ngpu/gpu-transp-cached\n");
    printf("A raw 15-bit 64x64 rectangle, 0001h texels against 0000h, cached and not. D as gpu-shape-time: the first D with bit 26 set,\n");
    printf("fastest..slowest over eight start points in the blanking. psx-spx: 0.08 / 0.04 a pixel cached (old / new GPU), 0.26 / 0.38 not.\n");
    printf("No assertions.\n\n");
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

    const Range c1 = row("cached 0001h", 0, 1);
    const Range c0 = row("cached 0000h", 0, 0);
    const Range u1 = row("uncached 0001h", 1, 1);
    const Range u0 = row("uncached 0000h", 1, 0);
    printf("\n");
    saving("cached", c1, c0);
    saving("uncached", u1, u0);

    waitReady();
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE1000400u);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-transp-cached: Done.\n");
    for (;;) {}
}
