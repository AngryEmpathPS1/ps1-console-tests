/*
 * gpu/gpu-tex-join1 - A TEXTURED QUAD'S HALVES ALONE WITH EVERY TEXEL MISSING: the four right triangles of a W x 16 rectangle, the
 * two quads made of them, and rows whose left edge moves 0 / 1 / 2 / 4 / 8 pixels a row (2026-11-11)
 *
 * WHY. hwtests/gpu-shape-time80 on the second SCPH-5501 (six runs), a quad's halves apart by bit 28: at 15-bit the quad
 * (0,0),(W,0),(0,16),(W,16) reads its first half within 5 cycles of this emulator and its second 36 short at W 64 and 128, 9 at
 * 160; the other diagonal reads BOTH halves short (22 + 26 at W 64, 15 + 19 at 128). But there each shape was drawn again and
 * again, so a lone triangle met the cache its own last draw left (72 fills where the same triangle in the quad has 129) and a
 * half's error cannot be told from what the cache held. Here every draw meets a cache that holds none of its texels: an untimed
 * draw of the same shape from the texture page beside it goes first (the same cache entries, other VRAM addresses - and a change
 * of page empties this emulator's cache outright), so each timed draw misses on every entry it touches, a lone triangle as its
 * twin inside a quad.
 *
 * WHAT IT DOES. gpu-tex-fill9's harness (15-bit, v 1:1, u a texel a pixel; clipped to W x 16 from (128,128); the display one line
 * tall; the last four words stored back to back, D cycles, one GPUSTAT read; the first D with bit 26 set over eight start points
 * in the blanking; for the quads bit 28's first D too - the second half's start). Raw texture (25h / 2Dh), as gpu-shape-time80.
 *   TA  (0,0),(W,0),(0,16)    every row from x 0, the widest first
 *   TB  (0,0),(0,16),(W,16)   every row from x 0, the other way up
 *   TC  (W,0),(0,16),(W,16)   each row from the diagonal (the quad's second half)
 *   TD  (0,0),(W,0),(W,16)    each row from the diagonal, the other way up
 *   Q   (0,0),(W,0),(0,16),(W,16) = TA then TC;   QF  (W,0),(0,0),(W,16),(0,16) = TD's pixels then TB's
 *   Sd  (0,0),(220,0),(55 d,55) clipped to W x 16: 16 rows, the left edge d pixels further right each row, the right end the clip's
 * at W 64 / 128 / 160 (Sd: d 0 / 1 / 2 / 4 / 8 at W 64 / 128). "fresh": as above. "base": u 0 at every vertex (one texel a row,
 * cached by the untimed draw): the shape's cost without a fill.
 *
 * READ IT LIKE THIS. fresh less base = a shape's fills and what they wait. Q fresh against TA fresh + TC fresh (less one base's
 * set-up) is the join; Sd against S0 is the walk beside dense fills. No assertions - compare against your emulator.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-join1 --ntsc --frames=40000 --quiet=35000`): PREDICTED.txt.
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

struct Var { char name[20]; int n; int x[4], y[4]; int fresh; int W; };   /* fresh 1: u = x, the untimed draw from the page beside; 0: u 0 */
static Var V[64];
static int NV = 0;
static void add(const char* sh, int W, int fresh, int n, const int* p) {
    Var& v = V[NV++]; sprintf(v.name, "%s %d %s", sh, W, fresh ? "fresh" : "base"); v.n = n; v.fresh = fresh; v.W = W;
    for (int i = 0; i < n; i++) { v.x[i] = p[2 * i]; v.y[i] = p[2 * i + 1]; }
}
static void defs() {
    static const int WS[3] = { 64, 128, 160 }, DS[5] = { 0, 1, 2, 4, 8 };
    for (int f = 1; f >= 0; f--) {
        for (int i = 0; i < 3; i++) {
            const int W = WS[i];
            const int a[6] = { 0, 0, W, 0, 0, 16 }, b[6] = { 0, 0, 0, 16, W, 16 }, c[6] = { W, 0, 0, 16, W, 16 }, d[6] = { 0, 0, W, 0, W, 16 };
            const int q[8] = { 0, 0, W, 0, 0, 16, W, 16 }, qf[8] = { W, 0, 0, 0, W, 16, 0, 16 };
            add("TA", W, f, 3, a); add("TB", W, f, 3, b); add("TC", W, f, 3, c); add("TD", W, f, 3, d); add("Q", W, f, 4, q); add("QF", W, f, 4, qf);
        }
        for (int i = 0; i < 2; i++) for (int k = 0; k < 5; k++) {
            const int s[6] = { 0, 0, 220, 0, 55 * DS[k], 55 };
            char nm[8]; sprintf(nm, "S%d", DS[k]);
            add(nm, WS[i], f, 3, s);
        }
    }
}

static const int X0 = 128, Y0 = 128, H = 16;
static uint32_t tw[9];
static int nw;
static const uint32_t TPAGE2 = 12u | (1u << 4) | (2u << 7);   /* X 768, Y 256, 15-bit */
static void build(const Var& v, int alt) {
    tw[0] = v.n == 4 ? 0x2D000000u : 0x25000000u;
    for (int i = 0; i < v.n; i++) {
        tw[1 + 2 * i] = xy(X0 + v.x[i], Y0 + v.y[i]);
        tw[2 + 2 * i] = uv(v.fresh ? v.x[i] : 0, v.y[i], i == 1 ? (alt ? TPAGE2 : TPAGE) : 0);
    }
    nw = 1 + 2 * v.n;
}
static void clipTo(const Var& v) {
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)X0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(X0 + v.W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void prime(const Var& v) {
    build(v, v.fresh);
    waitReady(); clipTo(v);
    for (int i = 0; i < nw; i++) write32(GP0, tw[i]);    /* untimed: the same cache entries, the other page's texels (base: the row's own one) */
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
    printf("\ngpu/gpu-tex-join1\n");
    printf("A 15-bit raw-textured triangle or quad inside W x 16 at (128,128), u a texel a pixel, v 1:1. fresh: an untimed draw of the same\n");
    printf("shape from the texture page beside it first (every entry it touches misses); base: u 0 (no fill). Its last four words stored back to back,\n");
    printf("D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points in the blanking, the display\n");
    printf("one line tall; for the quads bit 28's too. No assertions.\n\n");
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
    static uint32_t r[64][4];
    for (int i = 0; i < NV; i++) {
        sync(V[i], 0); probe(MAXD); waitReady();                 /* the code warm */
        for (int m = 0; m < (V[i].n == 4 ? 2 : 1); m++) {
            uint32_t a = 0xFFFFFFFFu, b = 0;
            for (int j = 0; j < 8; j++) { const uint32_t x = search(V[i], j, m ? 0x10000000u : 0x04000000u); a = x < a ? x : a; b = x > b ? x : b; }
            r[i][2 * m] = a; r[i][2 * m + 1] = b;
        }
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant            bit 26's D     bit 28's D\n");
    for (int i = 0; i < NV; i++) {
        if (V[i].n == 4) printf("%-18s %5u..%-5u   %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1], (unsigned)r[i][2], (unsigned)r[i][3]);
        else printf("%-18s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    }
    printf("\ngpu/gpu-tex-join1: Done.\n");
    for (;;) VSync(0);
}
