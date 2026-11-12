/*
 * gpu/gpu-tex-walk1 - A TEXTURED ROW BEGUN AFTER ITS LEFT EDGE MOVED: one fill a row at its first pixel, and dense fills, behind a
 * left edge that moves 0 to 16 pixels a row (2026-11-11)
 *
 * WHY. hwtests/gpu-tex-join1 on the second SCPH-5501 and the PSone: with every texel missing, the triangles whose rows start on
 * the diagonal (the left edge 4, 8 or 10 pixels further right each row) read ~30 cycles dearer than this emulator's candidate
 * whatever their width - about 2 cycles a row - while those whose rows all start at x 0 read within 5. A step of 2 pixels a row
 * read ~1 a row, a step of 1 nothing or less; but there the rows shrank by the step and the fills fell at other pixels row by row.
 * What a row's first fill pays after the walk, by the walk's length, is the question - and with it the race between that fill and
 * the row before's last frame-buffer write, which this emulator asks 4.25 cycles after the row's end.
 *
 * WHAT IT DOES. gpu-tex-join1's harness (15-bit raw-textured triangle, the display one line tall, the last four words stored back
 * to back, D cycles, one GPUSTAT read; the first D with bit 26 set over eight start points in the blanking). The triangle is
 * (0,128),(W,128),(d K,128 + K), K = min(255, 960 / d), clipped to 16 rows: row y starts d y pixels in and is W (1 - y / K) wide -
 * the rows keep their width within 4 pixels (16 at d 16) while the left edge moves d a row. u = the pixel's place in its row
 * (0 at the left edge, whatever d), v = y. Three kinds:
 *   none   u 0 everywhere, the untimed draw the same: every texel cached - the shape's cost without a fill
 *   one    u 0 everywhere, the untimed draw from the texture page beside: one fill a row, at its first pixel
 *   dense  u a texel a pixel, the untimed draw from the page beside: a fill every 4 pixels, at the same places in every row
 * at d 0, 1, 2, 3, 4, 5, 6, 8, 12, 16 and W 24 (no write inside the row) and 60 (one).
 *
 * READ IT LIKE THIS. one - none over 16 rows = 16 fills and what they wait: against d it is the first fill's cost after a walk
 * of d pixels. dense - none the same with a row's other fills behind it. No assertions - compare against your emulator.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-walk1 --ntsc --frames=40000 --quiet=35000`): PREDICTED.txt.
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

struct Var { char name[20]; int W, d, kind; };   /* kind 0 none, 1 one, 2 dense */
static Var V[64];
static int NV = 0;
static void defs() {
    static const int DS[10] = { 0, 1, 2, 3, 4, 5, 6, 8, 12, 16 }, WS[2] = { 24, 60 };
    static const char* const KN[3] = { "none", "one", "dense" };
    for (int k = 0; k < 3; k++) for (int w = 0; w < 2; w++) for (int i = 0; i < 10; i++) {
        Var& v = V[NV++]; v.W = WS[w]; v.d = DS[i]; v.kind = k; sprintf(v.name, "W%d d%d %s", v.W, v.d, KN[k]);
    }
}

static const int X0 = 0, Y0 = 128, H = 16;
static uint32_t tw[9];
static int nw;
static const uint32_t TPAGE2 = 12u | (1u << 4) | (2u << 7);   /* X 768, Y 256, 15-bit */
static void build(const Var& v, int alt) {
    const int K = v.d ? (960 / v.d < 255 ? 960 / v.d : 255) : 255;
    tw[0] = 0x25000000u;
    tw[1] = xy(X0, Y0);               tw[2] = uv(0, 0, 0);
    tw[3] = xy(X0 + v.W, Y0);         tw[4] = uv(v.kind == 2 ? v.W : 0, 0, alt ? TPAGE2 : TPAGE);
    tw[5] = xy(X0 + v.d * K, Y0 + K); tw[6] = uv(0, K, 0);
    nw = 7;
}
static void clipTo(const Var& v) {
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)X0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | 511u);   /* 16 rows; the rows end at the triangle's own edge */
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void prime(const Var& v) {
    build(v, v.kind != 0);
    waitReady(); clipTo(v);
    for (int i = 0; i < nw; i++) write32(GP0, tw[i]);    /* untimed: the same cache entries, the other page's texels (none: the rows' own) */
    waitReady(); clipTo(v); waitReady();
    build(v, 0);
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    for (int i = 0; i < nw - 4; i++) write32(GP0, tw[i]);
    return probe4(tw + nw - 4, n, k);
}
static void sync(const Var& v, int j) {
    prime(v);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 40000;
static uint32_t search(const Var& v, int j, uint32_t bit) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(v, j);
        const uint32_t st = probe(mid);
        if (st & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-tex-walk1\n");
    printf("A 15-bit raw-textured triangle (0,128),(W,128),(d K,128 + K) clipped to 16 rows: each row starts d pixels further right, W wide\n");
    printf("within 4 pixels. none: every texel cached; one: a fill a row, at its first pixel; dense: a fill every 4 pixels. Its last four words\n");
    printf("stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points in the\n");
    printf("blanking, the display one line tall. No assertions.\n\n");
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
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(512, 256));   /* 1234h, both pages */
    waitReady();
    static uint32_t r[64][2];
    for (int i = 0; i < NV; i++) {
        sync(V[i], 0); probe(MAXD); waitReady();                 /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(V[i], j, 0x04000000u); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant            bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-18s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-walk1: Done.\n");
    for (;;) VSync(0);
}
