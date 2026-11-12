/*
 * gpu/gpu-tex-fill7 - a fill every 8 pixels at 8-BIT against the same at 15-bit: is a paletted row's shortfall the palette's or the
 * fills' spacing? (2026-11-11)
 *
 * WHY. hwtests/gpu-shape-time81 on the second SCPH-5501 and the PSone: 8- and 4-bit textured rows read ~2 cycles a row dearer on
 * the console than on v0.377.326 once a row reaches ~96 pixels, where 15-bit rows (a fill every 4 pixels) read right. And
 * gpu-tex-fill6's 15-bit rows at HALF a texel a pixel - a fill every 8 pixels, the spacing an 8-bit row has - read short too at
 * W 128 / 160 / 192 (-34 / -17 / -39 over 32 rows) and dear at 144 / 176 / 208. FillWB was fitted on fills every 4 pixels and on
 * single fills. So: the palette, or the spacing?
 *
 * WHAT IT DOES. gpu-tex-fill6's harness (a triangle from (128,128), its left edge upright, clipped to W x 32; the display one
 * line tall; bit 26's first D over eight start points), W = 128, 136 .. 208, each four ways:
 *   15b base     15-bit, u span 0: one line a row, cached by the untimed draw - no fill
 *   15b every 8  15-bit, half a texel a pixel: a fill every 8 pixels (gpu-tex-fill6's, at more widths)
 *   8b base      8-bit, u span 0
 *   8b every 8   8-bit, a texel a pixel: a fill every 8 pixels (W >= 128: u and u + 64 share a cache entry, so every line misses)
 * The page is 1234h throughout and the 8-bit CLUT is the page's own first row (every entry 1234h): every texel drawn, opaque.
 *
 * READ IT LIKE THIS. A variant less its base, over 32 rows: one row's fills and waits at that width. The 8-bit column less the
 * 15-bit one at the same W is the palette's own. No assertions.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill7 --ntsc --frames=40000 --quiet=35000`): PREDICTED.txt.
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
static uint32_t tpageD(int d) { return 8u | (1u << 4) | ((uint32_t)d << 7); }   /* X 512, Y 256; d 2 = 15-bit, 1 = 8-bit */
static const uint32_t CLUT8 = (256u << 6) | 32u;   /* (512,256): the page's own first row, every entry 1234h */

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

struct Var { char name[24]; int W; int U; int d; };   /* U: u at x + 252 (252 a texel a pixel, 126 half, 0 none) */
static Var V[64];
static int NV = 0;
static void add(const char* nm, int W, int U, int d) { Var& v = V[NV++]; sprintf(v.name, nm, W); v.W = W; v.U = U; v.d = d; }
static void defs() {
    for (int w = 128; w <= 208; w += 8) {
        add("W %d 15b base", w, 0, 2); add("W %d 15b every 8", w, 126, 2);
        add("W %d 8b base", w, 0, 1); add("W %d 8b every 8", w, 252, 1);
    }
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[7];
static void tri(const Var& v) {   /* right vertex 252 out, bottom 252 down: row 31 still reaches 220 past x0 */
    tw[0] = 0x24808080u;
    tw[1] = xy(X0, Y0);              tw[2] = uv(0, 0, CLUT8);
    tw[3] = xy(X0 + 252, Y0);        tw[4] = uv(v.U, 0, tpageD(v.d));
    tw[5] = xy(X0, Y0 + 252);        tw[6] = uv(0, 252, 0);
}
static void clipTo(const Var& v) {
    const int x0 = X0;
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)x0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(x0 + v.W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void prime(const Var& v) {
    waitReady(); clipTo(v);
    for (int i = 0; i < 7; i++) write32(GP0, tw[i]);     /* untimed: a base row's one line cached (wide rows evict their own) */
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
static const uint32_t MAXD = 40000;
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
    printf("\ngpu/gpu-tex-fill7\n");
    printf("A triangle clipped to W x 32 from x 128, 15-bit at half a texel a pixel or 8-bit at a texel a pixel (a fill every 8 pixels), or u span 0 (base):\n");
    printf("its last four words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest\n");
    printf("over eight start points in the blanking, the display one line tall. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP1, 0x07000000u | (17u << 10) | 16u);                 /* the display one line tall */
    write32(GP0, 0xE1000400u | tpageD(2));
    write32(GP0, 0xE2000000u);
    clipAll();
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* 1234h */
    waitReady();
    static uint32_t r[64][2];
    for (int i = 0; i < NV; i++) {
        tri(V[i]);
        waitReady(); write32(GP0, 0xE1000400u | tpageD(V[i].d)); waitReady();
        sync(V[i], 0); probe(MAXD); waitReady();                 /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(V[i], j); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant              bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-20s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-fill7: Done.\n");
    for (;;) VSync(0);
}
