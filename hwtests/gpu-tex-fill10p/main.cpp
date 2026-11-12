/*
 * gpu/gpu-tex-fill10p - gpu-tex-fill10 with its knobs in a PARAMS block: ONE row width W and ONE fill spacing GAP a run, so
 * tools/discriminate.js --param sweeps any grid of them (2026-11-12, the first parametric test; docs/DISCRIMINATE.md)
 *
 * WHY. gpu-tex-fill10 measures 24 variants in a fixed grid (W 48..208 x a fill every 8, 16, 32 and the base) and takes its
 * time doing it. The candidates it was built to part (STATUS "gpu-tex-fill10 ...": v0.377.327 and the FillWB candidate, by
 * 154 cycles at a fill every 32, W 208) part where fills are few a write, which is a corner of that grid; the next question
 * may be a corner just outside it (W 220, a fill every 64). Here the grid is the tool's: the exe carries its knobs, the tool
 * patches a copy per set and runs the copies (PSX_HWEXE), and a console runs the same patched copies.
 *
 * WHAT IT DOES. gpu-tex-fill10's measurement, unchanged, for two variants: W base (u 0, every texel cached) and W every GAP
 * (u running 4/GAP texel a pixel over a span of 252 less 252 mod GAP/4 - 252 for 8 and 16, 248 for 32 as fill10 has them -
 * the untimed draw from the texture page beside, so every entry misses). The first D with GPUSTAT bit 26 set, fastest..slowest
 * over J start points in the blanking (fill10's eight: J 8).
 *
 * THE PARAMS BLOCK. 'PARM', the number of knobs, then tag / value pairs (a tag is up to four ASCII characters, zero-padded,
 * little-endian): the tool finds the block in the built exe by its marker and reads the names and defaults from it, so no
 * table outside the exe has to agree with it. volatile: the compiler must read each value from the image, never fold it.
 *   W    the clip's width, 16..220 (fill10's 48, 64, 96, 128, 160, 208)
 *   GAP  pixels a fill, a multiple of 4 from 4 to 252; 0 measures the base alone
 *   J    start points, 1..8
 * The values a run used are printed on its "params" line, so a console log says which copy it was.
 *
 * READ IT LIKE THIS. "less base" is fill10's PREDICTED.txt number: the variant less its base, over 32 rows - one row's fills
 * and waits. At the defaults (W 208, a fill every 32) it is fill10's "W 208 every 32" less "W 208 base".
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill10p --ntsc --frames=40000 --quiet=35000`): as gpu-tex-fill10's rows of the
 * same W and spacing (docs/DISCRIMINATE.md, the worked sweep).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define TAG(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
__attribute__((used)) static volatile const uint32_t PARAMS[16] = {
    0x4D524150u /* 'PARM' */, 3,
    TAG('W', 0, 0, 0), 208,
    TAG('G', 'A', 'P', 0), 32,
    TAG('J', 0, 0, 0), 8,
};

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

struct Var { char name[24]; int W, U, span; };   /* U: u at x + span (0: base) */
static Var V[2];
static int NV = 0;
static int PW, PGAP, PJ;
static bool defs() {
    PW = (int)PARAMS[3]; PGAP = (int)PARAMS[5]; PJ = (int)PARAMS[7];
    if (PW < 16 || PW > 220 || PJ < 1 || PJ > 8 || (PGAP && (PGAP < 4 || PGAP > 252 || (PGAP & 3)))) return false;
    { Var& v = V[NV++]; sprintf(v.name, "W %d base", PW); v.W = PW; v.U = 0; v.span = 252; }
    if (PGAP) {
        const int q = PGAP / 4, span = 252 - 252 % q;   /* u 4/GAP a pixel: 126 over 252 at 8, 63 at 16, 31 over 248 at 32 (fill10's) */
        Var& v = V[NV++]; sprintf(v.name, "W %d every %d", PW, PGAP); v.W = PW; v.U = span / q; v.span = span;
    }
    return true;
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
    printf("\ngpu/gpu-tex-fill10p\n");
    printf("gpu-tex-fill10's measurement for one row width W and one fill spacing GAP (its PARAMS block; tools/discriminate.js\n");
    printf("--param sweeps them): a 15-bit raw-textured triangle clipped to W x 32, u 4/GAP texel a pixel or u 0 (base). Its last\n");
    printf("four words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over J start\n");
    printf("points in the blanking, the display one line tall. No assertions.\n\n");
    if (!defs()) {
        printf("params W=%d GAP=%d J=%d: out of range (W 16..220, GAP 0 or 4..252 a multiple of 4, J 1..8)\n", PW, PGAP, PJ);
        printf("\ngpu/gpu-tex-fill10p: Done.\n");
        for (;;) VSync(0);
    }
    printf("params W=%d GAP=%d J=%d\n\n", PW, PGAP, PJ);
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
    static uint32_t r[2][2];
    for (int i = 0; i < NV; i++) {
        sync(V[i], 0); probe(MAXD); waitReady();                 /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < PJ; j++) { const uint32_t x = search(V[i], j, 0x04000000u); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant            bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-18s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    if (NV > 1) printf("less base          %5d..%-5d\n", (int)(r[1][0] - r[0][0]), (int)(r[1][1] - r[0][1]));
    printf("\ngpu/gpu-tex-fill10p: Done.\n");
    for (;;) VSync(0);
}
