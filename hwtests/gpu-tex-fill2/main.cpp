/*
 * gpu/gpu-tex-fill2 - ONE texture fill a row, mapped pixel by pixel: where in a row a fill waits, for how long, and what the
 * row before decides.
 *
 * WHY (2026-11-08). hwtests/gpu-tex-fill1 on the second SCPH-5501: a lone fill costs 8.0 cycles - except early in a row (at
 * pixel 4 16.7, 8 12.8, 12 9.0, 16-28 8.0; an odd start ~1 less) and at pixel 40 of a 64-pixel row (14.9); a fill at a row's
 * first pixel 8.0 (9.0 in 64-pixel rows), and a second fill after one at pixel 4 never waits. The reading: the row before's
 * last write-buffer drain (17 halfwords, ~12.8 cycles) holds the bus from the row's start, and a fill asked for before a
 * drain has started goes first. This maps it finely and moves what the reading says it depends on.
 *
 * WHAT IT DOES. gpu-tex-fill1's harness (a 15-bit triangle with its left edge upright at x 128, clipped by the drawing area to
 * W x 32, v 1:1, every line primed and the chosen ones evicted by 4x32 sprites at texel rows 32..63; cycle-exact: its last
 * four words stored back to back, D cycles, one GPUSTAT read, D binary-searched for bit 26 at eight starts in the blanking;
 * the display one line tall). New: a texel offset u0 (texels u0.. across the row) moves the lines' boundaries, the row's
 * width W is free (u stays inside the block: W <= 32 - u0 at a texel a pixel), and two lines can be evicted.
 *   a: a texel a pixel, W 32 - u0, u0 = 0..3: base and one fill at line j = 1..7 - pixel 4j - u0 (every pixel 1..28)
 *   b: a texel a pixel, W = 18, 20, ..., 30 (the row before's last chunk 2..14): bases at u0 0 and 2, a fill at pixel 2, 4, 6
 *   c: half a texel a pixel, W 64 - 2 u0, u0 = 0..3: base and a fill at line j = 1..7 - pixel 8j - 2 u0 (even pixels 2..62)
 *   d: W 32: fills at 0 and 4, 0 and 8, 4 and 8, 4 and 12, 8 and 12
 *
 * READ IT LIKE THIS. A variant less its base, over 32 rows, is one fill's cost at that place (d: two fills').
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill2 --ntsc --frames=40000 --quiet=30000`): PREDICTED.txt.
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

struct Var { char name[24]; int W; int odd; uint32_t mask; int u0; int half; };   /* mask: the lines evicted */
static Var V[160];
static int NV = 0;
static void add(const char* nm, int a1, int a2, int W, int half, int u0, uint32_t mask) {
    Var& v = V[NV++]; sprintf(v.name, nm, a1, a2); v.W = W; v.odd = 0; v.mask = mask; v.u0 = u0; v.half = half;
}
static void defs() {
    for (int u0 = 0; u0 < 4; u0++) {                       /* a */
        add("a u0 %d base", u0, 0, 32 - u0, 0, u0, 0);
        for (int j = 1; j < 8; j++) add("a fill at %2d", 4 * j - u0, 0, 32 - u0, 0, u0, 1u << j);
    }
    for (int W = 18; W <= 30; W += 2) {                    /* b: pixel 4 from u0 0 (line 1), 2 and 6 from u0 2 (lines 1, 2) */
        add("b W %d base", W, 0, W, 0, 0, 0);
        add("b W %d base u0 2", W, 0, W, 0, 2, 0);
        add("b W %d fill at %d", W, 2, W, 0, 2, 2u);
        add("b W %d fill at %d", W, 4, W, 0, 0, 2u);
        add("b W %d fill at %d", W, 6, W, 0, 2, 4u);
    }
    for (int u0 = 0; u0 < 4; u0++) {                       /* c */
        add("c u0 %d base", u0, 0, 64 - 2 * u0, 1, u0, 0);
        for (int j = 1; j < 8; j++) add("c fill at %2d", 8 * j - 2 * u0, 0, 64 - 2 * u0, 1, u0, 1u << j);
    }
    add("d fills %d and %d", 0, 4, 32, 0, 0, 3u); add("d fills %d and %d", 0, 8, 32, 0, 0, 5u); add("d fills %d and %d", 4, 8, 32, 0, 0, 6u);
    add("d fills %d and %d", 4, 12, 32, 0, 0, 10u); add("d fills %d and %d", 8, 12, 32, 0, 0, 12u);
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[7];
static void tri(const Var& v) {   /* the timed triangle's words: u from u0, a texel (half a texel) a pixel */
    const int x0 = X0 + v.odd, du = v.half ? 64 : 2 * 32;   /* u0 + du over 2 x (32 or 64) pixels... */
    const int span = v.half ? 128 : 64;                   /* the far vertex 2 x 64 (half) or 2 x 32 pixels out */
    tw[0] = 0x24808080u;
    tw[1] = xy(x0, Y0);              tw[2] = uv(v.u0, 0, 0);
    tw[3] = xy(x0 + span, Y0);       tw[4] = uv(v.u0 + (v.half ? 63 : du), 0, TPAGE);
    tw[5] = xy(x0, Y0 + 2 * H);      tw[6] = uv(v.u0, 64, 0);
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
    printf("\ngpu/gpu-tex-fill2\n");
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
    static uint32_t r[160][2];
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
    printf("\ngpu/gpu-tex-fill2: Done.\n");
    for (;;) VSync(0);
}
