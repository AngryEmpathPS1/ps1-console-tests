/*
 * gpu/gpu-tex-fill10 - SPARSE texture fills in wide rows: a fill every 8, 16 and 32 pixels in rows of 48 to 208 pixels, every
 * texel missing on every draw (2026-11-11)
 *
 * WHY. The FillWB candidate (STATUS "gpu-tex-fill8 ..."/"gpu-tex-fill9 ...": the first fill after each mid-row write pays ~1
 * cycle once) reads every cycle-exact test of fills 1 to 8 pixels apart closer to the consoles, and gpu-tex-scale worse: its
 * 15-bit quad magnified 4x (a fill every 16 pixels, rows to 256 pixels) -49 -> +650 cycles of 99,647, its 4-bit 1:1 quad (a
 * fill every 16 pixels too) -4 -> +145. But gpu-tex-scale draws under the display, times by polling (its values sit on a
 * 47-cycle grid), and redraws into its own cache. Whether the once-a-write cycle holds when the next fill comes 16 or 32 pixels
 * after the write has no cycle-exact measurement: gpu-tex-fill6 and 7 stop at 8 apart.
 *
 * WHAT IT DOES. gpu-tex-fill9's triangle in gpu-tex-walk1's harness (15-bit raw texture, from (128,128), its left edge upright,
 * v 1:1, clipped to W x 32; the display one line tall; the last four words stored back to back, D cycles, one GPUSTAT read; the
 * first D with bit 26 set over eight start points in the blanking), u running 1/2, 1/4 or 1/8 texel a pixel:
 *   W base       u 0, the untimed draw the same: every texel cached
 *   W every 8    half a texel a pixel, the untimed draw from the texture page beside: every entry misses
 *   W every 16   a quarter;  W every 32   an eighth
 * at W 48, 64, 96, 128, 160, 208.
 *
 * READ IT LIKE THIS. A variant less its base, over 32 rows: one row's fills and waits. A row W wide has a frame-buffer write
 * after every 32nd pixel; at 16 apart each write has two fills behind it, at 32 apart one. No assertions.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill10 --ntsc --frames=40000 --quiet=35000`): PREDICTED.txt.
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

struct Var { char name[20]; int W, U, span; };   /* U: u at x + span (0: base) */
static Var V[64];
static int NV = 0;
static void add(const char* nm, int W, int U, int span) { Var& v = V[NV++]; sprintf(v.name, nm, W); v.W = W; v.U = U; v.span = span; }
static void defs() {
    static const int WS[6] = { 48, 64, 96, 128, 160, 208 };
    for (int i = 0; i < 6; i++) add("W %d base", WS[i], 0, 252);
    for (int i = 0; i < 6; i++) add("W %d every 8", WS[i], 126, 252);
    for (int i = 0; i < 6; i++) add("W %d every 16", WS[i], 63, 252);
    for (int i = 0; i < 6; i++) add("W %d every 32", WS[i], 31, 248);
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[9];
static int nw;
static const uint32_t TPAGE2 = 12u | (1u << 4) | (2u << 7);   /* X 768, Y 256, 15-bit */
static void build(const Var& v, int alt) {   /* the right vertex span out, the bottom 252 down: row 31 still reaches 220 past x0 */
    tw[0] = 0x25000000u;
    tw[1] = xy(X0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(X0 + v.span, Y0);     tw[4] = uv(v.U, 0, alt ? TPAGE2 : TPAGE);
    tw[5] = xy(X0, Y0 + 252);        tw[6] = uv(0, 252, 0);
    nw = 7;
}
static void clipTo(const Var& v) {
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)X0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(X0 + v.W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void prime(const Var& v) {
    build(v, v.U != 0);
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
    printf("\ngpu/gpu-tex-fill10\n");
    printf("A 15-bit raw-textured triangle clipped to W x 32 from x 128, v 1:1, u 1/2, 1/4 or 1/8 texel a pixel (a fill every 8, 16 or 32\n");
    printf("pixels; the untimed draw from the texture page beside, so every entry misses) or u 0 (base: cached). Its last four words stored\n");
    printf("back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points in the\n");
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
    printf("\ngpu/gpu-tex-fill10: Done.\n");
    for (;;) VSync(0);
}
