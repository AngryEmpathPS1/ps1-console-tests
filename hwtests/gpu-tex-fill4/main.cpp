/*
 * gpu/gpu-tex-fill4 - TWO texture fills a row around the row's writes, rows of 6 to 64 pixels: how fills push the writes and
 * queue behind them.
 *
 * WHY (2026-11-08). hwtests/gpu-tex-fill1/2/3 on the second SCPH-5501: a lone fill costs 8.0 cycles and waits only for the
 * row's 32-halfword writes - the row before's last (from ~pixel 2.5 to ~11-14 cycles in) and, past 32 pixels, the first 32
 * halfwords' at the 33rd pixel (~35-47); one asked for before a write starts goes first. A burst model of that
 * (experiments/wbsim3.js) fits the lone fills to 13-15 cycles a variant but follows gpu-tex-stride3's many-fill quads only part
 * of the way (STATUS "...one fill at a time ..."). These are the cases a lone fill cannot show.
 *
 * WHAT IT DOES. gpu-tex-fill2's harness (a 15-bit triangle clipped to W x 32, v 1:1, every line primed and the chosen ones
 * evicted, cycle-exact, the display one line tall), a texel offset u0 moving the lines.
 *   a: W 32 - u0 at a texel a pixel: fills at (2, 6 / 10 / 14), (1, 5 / 9 / 13), (3, 7 / 11 / 15), (0, 12 / 16 / 20)
 *   b: rows of W = 6, 8, 10, 12, 14, 16 at a texel a pixel: base and a fill at pixel 4 (the row before's write, short)
 *   c: W 64 - 2 u0 at half a texel: fills at (32, 40), (32, 48), (24, 40), (40, 48), (30, 38), (30, 46), (36, 44), (28, 36),
 *      (34, 42), (26, 34)
 *   d: W 48 at 2/3 of a texel: base and a fill at each line, 1..7 (pixels ~7, 13, ..., 43)
 *
 * READ IT LIKE THIS. A variant less its base, over 32 rows: the fills' cost (a pair's together).
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill4 --ntsc --frames=20000 --quiet=15000`): PREDICTED.txt.
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
    static const int A[4][2] = { { 2, 2 }, { 3, 1 }, { 1, 3 }, { 0, 0 } };   /* u0 and the first fill's pixel (its line 1, or 0) */
    for (int i = 0; i < 4; i++) {
        const int u0 = A[i][0], p1 = A[i][1], l1 = p1 == 0 ? 0 : 1;
        add("a u0 %d base", u0, 0, 32 - u0, 0, u0, 0);
        for (int k = 1; k <= 3; k++) { const int l2 = l1 + k + (p1 == 0 ? 2 : 0); add("a fills %d and %d", p1, 4 * l2 - u0, 32 - u0, 0, u0, (1u << l1) | (1u << l2)); }
    }
    for (int W = 6; W <= 16; W += 2) { add("b W %d base", W, 0, W, 0, 0, 0); add("b W %d fill at %d", W, 4, W, 0, 0, 2u); }
    static const int C[10][3] = { { 0, 4, 5 }, { 0, 4, 6 }, { 0, 3, 5 }, { 0, 5, 6 }, { 1, 4, 5 }, { 1, 4, 6 }, { 2, 5, 6 }, { 2, 4, 5 }, { 3, 5, 6 }, { 3, 4, 5 } };
    for (int u0 = 0; u0 < 4; u0++) {
        add("c u0 %d base", u0, 0, 64 - 2 * u0, 1, u0, 0);
        for (int i = 0; i < 10; i++) if (C[i][0] == u0) add("c fills %d and %d", 8 * C[i][1] - 2 * u0, 8 * C[i][2] - 2 * u0, 64 - 2 * u0, 1, u0, (1u << C[i][1]) | (1u << C[i][2]));
    }
    add("d W48 base", 0, 0, 48, 2, 0, 0);
    for (int j = 1; j < 8; j++) add("d W48 fill line %d", j, 0, 48, 2, 0, 1u << j);
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[7];
static void tri(const Var& v) {   /* u from u0: a texel a pixel (half 0), half a texel (1), 2/3 of one (2) */
    const int x0 = X0 + v.odd;
    const int span = v.half == 1 ? 128 : v.half == 2 ? 96 : 64, du = v.half ? 63 : 64;
    tw[0] = 0x24808080u;
    tw[1] = xy(x0, Y0);              tw[2] = uv(v.u0, 0, 0);
    tw[3] = xy(x0 + span, Y0);       tw[4] = uv(v.u0 + du, 0, TPAGE);
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
    printf("\ngpu/gpu-tex-fill4\n");
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
    printf("\ngpu/gpu-tex-fill4: Done.\n");
    for (;;) VSync(0);
}
