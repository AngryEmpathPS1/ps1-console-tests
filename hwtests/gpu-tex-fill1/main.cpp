/*
 * gpu/gpu-tex-fill1 - ONE texture-cache fill a row at a chosen pixel: what a fill costs by its place in the row, by the row's
 * start parity and by the gap to another fill, every other texel cached.
 *
 * WHY (2026-11-08). hwtests/gpu-tex-stride/2/3 on the second SCPH-5501: a fill's cost on the new GPU goes by its pattern - by
 * the pixels between fills (-0.3 a fill at 2, 0 at 4, +0.3..+0.6 from ~6, jagged), by an odd row start (N = 8 rows 46
 * cycles cheaper), not by VRAM's 16-halfword places - and no feature, ramp or simulated write buffer fits it (STATUS
 * "...with the scan-out gone: the curve is the drawing's own ..."). Those quads mix every spacing with a diagonal split and
 * row-start fills. This places single fills.
 *
 * WHAT IT DOES. A 15-bit textured triangle (24h) from (128,128) with its left edge upright, clipped by the drawing area to
 * W x 32 pixels (rows of W from x 128, or 129 for "odd"), v 1:1 (texel rows 0..31), u 0..31 across the W pixels (W 32: a
 * texel a pixel, a cache line every 4 pixels; W 64: half a texel, a line every 8). Its 8 x 32 lines fill the whole texture
 * cache. Before every timed draw: the triangle drawn once (every line cached), then 4x32 sprites at texel rows 32..63 over
 * the chosen lines j (the same cache entries, another block: each evicts line j of every row), then the timed draw - which
 * fills exactly the evicted lines, one fill a row at pixel 4j (8j at W 64). Timed the gpu-shape-time way: its last four
 * words stored back to back, D cycles, ONE GPUSTAT read, D binary-searched for bit 26 at eight start points 20,000 + 271 j
 * cycles after VBlank's IRQ, the display ONE line tall (GP1(07h) Y2 = Y1+1: no scan-out). The opaque texture is 1234h.
 *   base               nothing evicted (no fill)
 *   1 at j             line j evicted: one fill a row at pixel 4j (j = 0: the row's first pixel), j = 0..7, even and odd starts
 *   2 at j,j+g         lines 1 and 1+g: two fills a row 4g apart, g = 1..6
 *   all                every line: a fill every 4 pixels (gpu-tex-stride's N = 4)
 *   W64 ...            the same at half a texel a pixel: base, one fill at 8j (j = 0..7), all (a fill every 8)
 *
 * READ IT LIKE THIS. A row less base, over 32 rows, is one fill's cost at that place.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill1 --ntsc --frames=40000 --quiet=30000`): PREDICTED.txt.
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
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */

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

struct Var { const char* name; int W; int odd; uint32_t mask; };   /* mask: the lines evicted */
static Var V[48];
static int NV = 0;
static void add(const char* nm, int W, int odd, uint32_t mask) { Var& v = V[NV++]; v.name = nm; v.W = W; v.odd = odd; v.mask = mask; }
static void defs() {
    static const char* const one[8] = { "1 at 0", "1 at 1", "1 at 2", "1 at 3", "1 at 4", "1 at 5", "1 at 6", "1 at 7" };
    static const char* const oneo[8] = { "odd 1 at 0", "odd 1 at 1", "odd 1 at 2", "odd 1 at 3", "odd 1 at 4", "odd 1 at 5", "odd 1 at 6", "odd 1 at 7" };
    static const char* const two[6] = { "2 at 1,2", "2 at 1,3", "2 at 1,4", "2 at 1,5", "2 at 1,6", "2 at 1,7" };
    static const char* const w64[8] = { "W64 1 at 0", "W64 1 at 1", "W64 1 at 2", "W64 1 at 3", "W64 1 at 4", "W64 1 at 5", "W64 1 at 6", "W64 1 at 7" };
    add("base", 32, 0, 0);
    for (int j = 0; j < 8; j++) add(one[j], 32, 0, 1u << j);
    add("odd base", 32, 1, 0);
    for (int j = 0; j < 8; j++) add(oneo[j], 32, 1, 1u << j);
    for (int g = 1; g <= 6; g++) add(two[g - 1], 32, 0, 2u | (1u << (1 + g)));
    add("all", 32, 0, 0xFFu);
    add("odd all", 32, 1, 0xFFu);
    add("W64 base", 64, 0, 0);
    for (int j = 0; j < 8; j++) add(w64[j], 64, 0, 1u << j);
    add("W64 all", 64, 0, 0xFFu);
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[7];
static void tri(const Var& v) {   /* the timed triangle's words */
    const int x0 = X0 + v.odd;
    tw[0] = 0x24808080u;
    tw[1] = xy(x0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(x0 + 2 * v.W, Y0);    tw[4] = uv(v.W == 64 ? 63 : 64, 0, TPAGE);   /* W 64 at 64 reaches u 32 (the next block) at its last pixel */
    tw[5] = xy(x0, Y0 + 2 * H);      tw[6] = uv(0, 64, 0);
}
static void clipTo(const Var& v) {
    const int x0 = X0 + v.odd;
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)x0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(x0 + v.W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void prime(const Var& v) {
    waitReady(); clipTo(v);
    for (int i = 0; i < 7; i++) write32(GP0, tw[i]);     /* every line cached */
    waitReady(); clipAll();
    for (int j = 0; j < 8; j++) if (v.mask & (1u << j)) {   /* 4x32 sprite over line j at texel rows 32..63: evicts it */
        write32(GP0, 0x65808080u); write32(GP0, xy(400 + 8 * j, 128)); write32(GP0, uv(4 * j, 32, 0)); write32(GP0, xy(4, 32));
        waitReady();                                       /* the FIFO holds 16 words: eight sprites back to back overflow it */
    }
    waitReady(); clipTo(v); waitReady();
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    for (int i = 0; i < 3; i++) write32(GP0, tw[i]);
    return probe4(tw + 3, n, k);
}
static void sync(const Var& v, int j) {
    prime(v);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 6000;
static uint32_t search(const Var& v, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(v, j);
        const uint32_t st = probe(mid);
        if (st & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-tex-fill1\n");
    printf("A 15-bit triangle clipped to W x 32, every texel cached but the evicted lines (one fill a row at 4j, or 8j at W 64):\n");
    printf("its last four words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest\n");
    printf("over eight start points in the blanking, the display one line tall. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP1, 0x07000000u | (17u << 10) | 16u);                 /* the display one line tall */
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    clipAll();
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* 1234h */
    waitReady();
    static uint32_t r[48][2];
    for (int i = 0; i < NV; i++) {
        tri(V[i]);
        sync(V[i], 0); probe(MAXD); waitReady();                 /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(V[i], j); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant          bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-16s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-fill1: Done.\n");
    for (;;) VSync(0);
}
