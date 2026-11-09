/*
 * gpu/gpu-tex-fill6 - WIDE rows of texture fills: rows of 64..208 pixels, a fill every 4 or every 8 pixels.
 *
 * WHY (2026-11-08). The texture-fill model of gpu-tex-fill1-5 (STATUS "## Proposed: a texture fill waits for the row's
 * frame-buffer writes") was fitted on rows of 64 pixels or fewer: one mid-row write at most. gpu-scan3's 15-bit textured
 * triangle (rows 128 down to 2, a fill every 4 pixels) reads 29-45 cycles dearer on the model than on src/ and the consoles,
 * and the model's term that moves it is the +0.5 every fill after a row's first mid-row write pays (516 of its 1,056 fills).
 * Rows past 64 pixels ask for two to six mid-row writes: here they are, in clean rows.
 *
 * WHAT IT DOES. gpu-tex-fill5's harness (cycle-exact, the display one line tall), its 15-bit triangle grown: from (128,128)
 * with its left edge upright, u and v 1:1 (or u half a texel a pixel), the hypotenuse past the drawing area, which clips it
 * to W x 32. Rows of W >= 64 at a texel a pixel use each of the row's 8 cache entries at least twice (u and u + 32 share
 * one), so every line misses on every draw: a fill every 4 pixels; at half a texel a pixel W >= 128 does the same every 8.
 *   W base             u span 0 (every pixel texel (0, v)): one line a row, cached by the untimed draw - no fill
 *   W every 4          a texel a pixel: a fill every 4 pixels, W / 4 a row
 *   W every 8          half a texel a pixel: a fill every 8 pixels, W / 8 a row
 *
 * READ IT LIKE THIS. A variant less its base, over 32 rows: one row's fills and waits at that width.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill6 --ntsc --frames=40000 --quiet=35000`): PREDICTED.txt.
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

struct Var { char name[24]; int W; int U; };   /* U: u at x + 252 (252 a texel a pixel, 126 half, 0 none) */
static Var V[64];
static int NV = 0;
static void add(const char* nm, int W, int U) { Var& v = V[NV++]; sprintf(v.name, nm, W); v.W = W; v.U = U; }
static void defs() {
    static const int W4[13] = { 64, 72, 80, 96, 100, 112, 120, 128, 144, 160, 176, 192, 208 };
    for (int i = 0; i < 13; i++) { add("W %d base", W4[i], 0); add("W %d every 4", W4[i], 252); }
    for (int i = 7; i < 13; i++) add("W %d every 8", W4[i], 126);
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[7];
static void tri(const Var& v) {   /* right vertex 252 out, bottom 252 down: row 31 still reaches 220 past x0 */
    tw[0] = 0x24808080u;
    tw[1] = xy(X0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(X0 + 252, Y0);        tw[4] = uv(v.U, 0, TPAGE);
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
    printf("\ngpu/gpu-tex-fill6\n");
    printf("A 15-bit triangle clipped to W x 32 from x 128, u and v 1:1 (every 4: a fill every 4 pixels) or u 1:2 (every 8):\n");
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
    static uint32_t r[64][2];
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
    printf("\ngpu/gpu-tex-fill6: Done.\n");
    for (;;) VSync(0);
}
