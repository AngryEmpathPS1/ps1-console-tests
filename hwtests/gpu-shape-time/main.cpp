/*
 * gpu/gpu-shape-time - when a lone primitive is done, to the cycle, for thirty-odd small and thin shapes, at eight
 * places in the line (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-batch3 on the SCPH-1001: its flat rectangles alone - 60h 8x8 / 16x16 / 32x32 / 64x16 / 16x64 -
 * read 22-42 cycles under this core while 1x1 and 64x64 sit on it, so the old GPU's rectangle set-up (GPU_RECTF_OLD2's
 * 55.9 ticks, held from gpu-cmd-ready's lone 1x1) looks like a FLOOR, not a set-up, and the rows fit S ~15 + this core's
 * own row terms. But those tests time with a poll loop that reads Timer 2 each time round (~25 cycles), and the same
 * cell read 228 in one program and 263 in another. gpu-cmd-ready's way is exact - the words stored back to back, k
 * NOPs, ONE GPUSTAT read - but it has six commands and k only to 240. The old GPU also stalls ~28 cycles once a line at
 * the line's start (gpu-scanline2), which a run longer than a line cannot dodge.
 *
 * WHAT IT DOES. Interrupts off, the GPU idle, in the vertical blanking at eight start points an eighth of a line apart
 * (20,000 + 271j cycles after the VBlank IRQ): the command's words stored back to back, then D cycles of waiting (a
 * cached 3-cycle loop and 0-2 NOPs), then one GPUSTAT read; D binary-searched for the first at which bit 26 (ready for a
 * command) reads set, and again for bit 28 (FIFO empty). Each shape's code is run once first so the cache is warm.
 * Shapes, at (192,128) - texture page (512,256) 15-bit, filled; CLUT (0,480):
 *   flat: 68h 1x1, 60h 1x1, 2x2, 4x4, 70h 8x8, 60h 8x8, 78h 16x16, 60h 16x16, 32x32, 64x4, 4x64, 64x16, 16x64, 64x64,
 *         128x8, 8x128, 256x2, 2x256
 *   semi: 62h 1x1, 8x8, 7Ah 16x16, 62h 16x16, 64x4, 4x64, 64x16
 *   15-bit sprite on cached texels: 7Dh 16x16, 65h 8x8, 16x16, 64x4, 4x64
 *   fill: 16x16, 64x4, 16x64, 64x64
 *
 * READ IT LIKE THIS. Per shape, bit 26's D (cycles from the last word's store to the read, plus a constant shared with
 * this core) fastest..slowest over the eight start points - on the old GPU the fastest is the stall-free cost, the
 * slowest with the line's stall - and bit 28's the same. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time --ntsc --frames=20000 --quiet=20000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */

/* The words stored back to back, n loop turns (3 cycles each) and k (0..2) NOPs, one GPUSTAT read. */
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
static uint32_t probe2(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
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

struct Shape { const char* name; int n; uint32_t w[4]; };
static Shape S[40];
static int NS = 0;
static void add(const char* name, int n, uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0) {
    Shape& s = S[NS++]; s.name = name; s.n = n; s.w[0] = a; s.w[1] = b; s.w[2] = c; s.w[3] = d;
}
static void defs() {
    const uint32_t P = xy(192, 128), F = 0x408020u, T = 0x808080u, UV = uv(0, 0, CLUT);
    add("flat 68h 1x1", 2, 0x68000000u | F, P);
    add("flat 60h 1x1", 3, 0x60000000u | F, P, xy(1, 1));
    add("flat 60h 2x2", 3, 0x60000000u | F, P, xy(2, 2));
    add("flat 60h 4x4", 3, 0x60000000u | F, P, xy(4, 4));
    add("flat 70h 8x8", 2, 0x70000000u | F, P);
    add("flat 60h 8x8", 3, 0x60000000u | F, P, xy(8, 8));
    add("flat 78h 16x16", 2, 0x78000000u | F, P);
    add("flat 60h 16x16", 3, 0x60000000u | F, P, xy(16, 16));
    add("flat 60h 32x32", 3, 0x60000000u | F, P, xy(32, 32));
    add("flat 60h 64x4", 3, 0x60000000u | F, P, xy(64, 4));
    add("flat 60h 4x64", 3, 0x60000000u | F, P, xy(4, 64));
    add("flat 60h 64x16", 3, 0x60000000u | F, P, xy(64, 16));
    add("flat 60h 16x64", 3, 0x60000000u | F, P, xy(16, 64));
    add("flat 60h 64x64", 3, 0x60000000u | F, P, xy(64, 64));
    add("flat 60h 128x8", 3, 0x60000000u | F, P, xy(128, 8));
    add("flat 60h 8x128", 3, 0x60000000u | F, P, xy(8, 128));
    add("flat 60h 256x2", 3, 0x60000000u | F, xy(0, 128), xy(256, 2));
    add("flat 60h 2x256", 3, 0x60000000u | F, xy(192, 0), xy(2, 256));
    add("semi 62h 1x1", 3, 0x62000000u | F, P, xy(1, 1));
    add("semi 62h 8x8", 3, 0x62000000u | F, P, xy(8, 8));
    add("semi 7Ah 16x16", 2, 0x7A000000u | F, P);
    add("semi 62h 16x16", 3, 0x62000000u | F, P, xy(16, 16));
    add("semi 62h 64x4", 3, 0x62000000u | F, P, xy(64, 4));
    add("semi 62h 4x64", 3, 0x62000000u | F, P, xy(4, 64));
    add("semi 62h 64x16", 3, 0x62000000u | F, P, xy(64, 16));
    add("sprite 7Dh 16x16", 3, 0x7D000000u | T, P, UV);
    add("sprite 65h 8x8", 4, 0x65000000u | T, P, UV, xy(8, 8));
    add("sprite 65h 16x16", 4, 0x65000000u | T, P, UV, xy(16, 16));
    add("sprite 65h 64x4", 4, 0x65000000u | T, P, UV, xy(64, 4));
    add("sprite 65h 4x64", 4, 0x65000000u | T, P, UV, xy(4, 64));
    add("fill 16x16", 3, 0x02000000u | F, P, xy(16, 16));
    add("fill 64x4", 3, 0x02000000u | F, P, xy(64, 4));
    add("fill 16x64", 3, 0x02000000u | F, P, xy(16, 64));
    add("fill 64x64", 3, 0x02000000u | F, P, xy(64, 64));
}

static uint32_t probe(const Shape& s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return s.n == 2 ? probe2(s.w, n, k) : s.n == 3 ? probe3(s.w, n, k) : probe4(s.w, n, k);
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
/* the first D (0..MAXD) at which (GPUSTAT & bit) reads set, at start point j; MAXD+1 if never */
static const uint32_t MAXD = 6000;
static uint32_t search(const Shape& s, uint32_t bit, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(j);
        const uint32_t st = probe(s, mid);
        if (st & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-shape-time\n");
    printf("A lone primitive's words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set and with\n");
    printf("bit 28 set, fastest..slowest over eight start points in the blanking. No assertions.\n\n");
    defs();
    EnterCriticalSection();
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
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    static uint32_t r26[40][2], r28[40][2];
    for (int i = 0; i < NS; i++) {
        waitReady(); probe(S[i], 4000); waitReady(); probe(S[i], 4000); waitReady();   /* the code and the texels warm */
        uint32_t a = 0xFFFFFFFFu, b = 0, c = 0xFFFFFFFFu, d = 0;
        for (int j = 0; j < 8; j++) {
            const uint32_t x = search(S[i], 0x04000000u, j), y = search(S[i], 0x10000000u, j);
            a = x < a ? x : a; b = x > b ? x : b; c = y < c ? y : c; d = y > d ? y : d;
        }
        r26[i][0] = a; r26[i][1] = b; r28[i][0] = c; r28[i][1] = d;
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("shape               bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-18s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-shape-time: Done.\n");
    for (;;) VSync(0);
}
