/*
 * gpu/gpu-tex-fill8 - TWO and THREE texture fills a row at an 8-bit row's places: behind the ROW-END write by its size, and behind a
 * mid-row write a fill has pushed (2026-11-11)
 *
 * WHY. hwtests/gpu-tex-fill7 on the second SCPH-5501 and the PSone: rows with a fill every 8 pixels read 1.5-4.4 cycles a row
 * dearer on the console than on v0.377.326 by the row's blocks and by its LAST block's size, and no setting of GPU_FILL_WB's
 * numbers holds them and the fills 4 apart together (STATUS "gpu-tex-fill7 ...", "Working notes on FillWB ..."). gpu-tex-fill4's
 * part c already has pairs round the mid-row write - a write no fill disturbs holds the bus to 15.1 cycles after its pixel; a
 * fill 8 pixels behind one that pushed the write waits 4.06 - and one pair, (32,48), that no single write explains (1.1). It has
 * nothing behind the row-end write at these places. Here:
 *   e  the ROW-END write: rows of W = 40, 48, 56, 64 at half a texel a pixel (one mid-row write; the last block 8, 16, 24, 32
 *      halfwords): base; a fill at pixel 0, at 8, at 16 alone; pairs (0,8), (0,16), (0,24), (8,16)
 *   f  the MID-ROW write, W 64 - 2 u0 at half a texel: singles at 32, 40, 48 and at 34; (32,56), (24,32), (34,50), (38,46);
 *      threes (32,40,48), (32,48,56), (24,32,40), (26,34,42), (30,38,46)
 *   g  W 48 at 2/3 of a texel a pixel (lines 6 pixels apart, a boundary at 32): pairs of lines (5,6), (5,7), (6,7), (4,5), (4,6)
 * on gpu-tex-fill4's harness unchanged (15-bit, every line primed and the chosen ones evicted, cycle-exact, the display one line
 * tall).
 *
 * READ IT LIKE THIS. A variant less its base, over 32 rows, less 8 a fill: the fills' waits a row. No assertions.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill8 --ntsc --frames=20000 --quiet=15000`): PREDICTED.txt.
 */
#include <common.h>
#include <string.h>
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
static void add(const char* nm, int W, int half, int u0, uint32_t mask) {
    Var& v = V[NV++]; strncpy(v.name, nm, 23); v.name[23] = 0; v.W = W; v.odd = 0; v.mask = mask; v.u0 = u0; v.half = half;
}
#define L(j) (1u << (j))
static void defs() {
    static char nm[64][24]; int k = 0;
    for (int W = 40; W <= 64; W += 8) {
        static const char* const N[8] = { "base", "fill 0", "fill 8", "fill 16", "fills 0 8", "fills 0 16", "fills 0 24", "fills 8 16" };
        static const uint32_t M[8] = { 0, L(0), L(1), L(2), L(0) | L(1), L(0) | L(2), L(0) | L(3), L(1) | L(2) };
        for (int i = 0; i < 8; i++) { sprintf(nm[k], "e W%d %s", W, N[i]); add(nm[k++], W, 1, 0, M[i]); }
    }
    add("f u0 0 base", 64, 1, 0, 0);
    add("f fill 32", 64, 1, 0, L(4)); add("f fill 40", 64, 1, 0, L(5)); add("f fill 48", 64, 1, 0, L(6));
    add("f fills 32 56", 64, 1, 0, L(4) | L(7)); add("f fills 24 32", 64, 1, 0, L(3) | L(4));
    add("f fills 32 40 48", 64, 1, 0, L(4) | L(5) | L(6)); add("f fills 32 48 56", 64, 1, 0, L(4) | L(6) | L(7));
    add("f fills 24 32 40", 64, 1, 0, L(3) | L(4) | L(5));
    add("f u0 3 base", 58, 1, 3, 0);
    add("f fill 34", 58, 1, 3, L(5)); add("f fills 34 50", 58, 1, 3, L(5) | L(7)); add("f fills 26 34 42", 58, 1, 3, L(4) | L(5) | L(6));
    add("f u0 1 base", 62, 1, 1, 0);
    add("f fills 38 46", 62, 1, 1, L(5) | L(6)); add("f fills 30 38 46", 62, 1, 1, L(4) | L(5) | L(6));
    add("g W48 base", 48, 2, 0, 0);
    add("g lines 5 6", 48, 2, 0, L(5) | L(6)); add("g lines 5 7", 48, 2, 0, L(5) | L(7)); add("g lines 6 7", 48, 2, 0, L(6) | L(7));
    add("g lines 4 5", 48, 2, 0, L(4) | L(5)); add("g lines 4 6", 48, 2, 0, L(4) | L(6));
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
    printf("\ngpu/gpu-tex-fill8\n");
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
    for (int i = 0; i < NV; i++) printf("%-18s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-fill8: Done.\n");
    for (;;) VSync(0);
}
