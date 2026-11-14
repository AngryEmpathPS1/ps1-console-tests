/*
 * gpu-tex-floor: A NARROW ROW'S COST ON A SHADED TRIANGLE - textured and gouraud, 1 to 8 pixels wide, no texture fills.
 *
 * WHY. hwtests/gpu-doom-cols on the second SCPH-5501 and the PSone (two runs each, alike): Doom's wall columns - textured
 * triangles (x,y0) (x+1,y1) (x,y1), every row one pixel - draw in 138 / 270 / 243 / 240 / 556 / 627 / 692 cycles at 3 / 14 / 16 /
 * 17 / 42 / 53 / 72 rows where v0.377.348 says 140 / 282 / 258 / 253 / 587 / 694 / 745: about 0.75 cycles a row dear here, with
 * fills or without (the 3-row one has none), while its one-row spans 7 / 56 / 260 wide read 1-5 cycles cheap. This core charges
 * a shaded row its pixels and walk or a floor of 2.59 pixels (GPU_SHADED_ROWS.floorPx), whichever is more: the floor reads
 * ~0.7 of a pixel high for these. Is it the floor, is it textured only, and where does it end?
 *
 * WHAT. In the blanking, at the game's mode (256x240): the Doom column at 16 / 32 / 64 / 128 rows, and a right triangle
 * (x,y) (x+w,y) (x,y+64) - rows from w pixels down to none - at w 1 / 2 / 3 / 4 / 6 / 8; each TEXTURED with one texel (the three
 * UVs equal: cached after the first draw, no fills) and GOURAUD. gpu-copy-modes' method: the words back to back from s cycles
 * after Timer 0's reset, D cycles, one GPUSTAT read; the first D with bit 26 set. Eight starts across a line, to show it
 * does not move. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-tex-floor --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

/* From a fresh Timer 0 reset (T0 >= 200, then < 64), delay to T0 = s exactly (3-cycle loop, its remainder in nops), store
   the words, delay D (n x 3 + k) and read GPUSTAT. */
#define SYNC_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n" \
        "5:\n" \
        "lhu $t3, 0x1100($t0)\n" \
        "nop\n" \
        "sltiu $t5, $t3, 200\n" \
        "bnez $t5, 5b\n" \
        "nop\n" \
        "6:\n" \
        "lhu $t3, 0x1100($t0)\n" \
        "nop\n" \
        "sltiu $t5, $t3, 64\n" \
        "beqz $t5, 6b\n" \
        "nop\n" \
        "lhu $t3, 0x1100($t0)\n" \
        "nop\n" \
        "subu $t4, %[s], $t3\n" \
        "7:\n" \
        "addiu $t4, $t4, -3\n" \
        "bgtz $t4, 7b\n" \
        "nop\n" \
        "la $t6, 8f\n" \
        "sll $t5, $t4, 2\n" \
        "addiu $t6, $t6, -8\n" \
        "subu $t6, $t6, $t5\n" \
        "jr $t6\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "8:\n"
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
static uint32_t probe7(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        "sw %[w4], 0x1810($t0)\n"
        "sw %[w5], 0x1810($t0)\n"
        "sw %[w6], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [w4] "r"(w[4]), [w5] "r"(w[5]), [w6] "r"(w[6]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}
static uint32_t probe6(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        "sw %[w4], 0x1810($t0)\n"
        "sw %[w5], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [w4] "r"(w[4]), [w5] "r"(w[5]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}
struct Shape { const char* name; int n; uint32_t w[7]; };
static Shape S[24];
static int NS = 0;
static const uint32_t UV = 0x1010u;
static void tex(const char* nm, int x0, int y0, int x1, int y1, int x2, int y2) {
    Shape& s = S[NS++]; s.name = nm; s.n = 7;
    s.w[0] = 0x24808080u; s.w[1] = xy(x0, y0); s.w[2] = 0x3c000000u | UV; s.w[3] = xy(x1, y1); s.w[4] = 0x008c0000u | UV; s.w[5] = xy(x2, y2); s.w[6] = UV;
}
static void gou(const char* nm, int x0, int y0, int x1, int y1, int x2, int y2) {
    Shape& s = S[NS++]; s.name = nm; s.n = 6;
    s.w[0] = 0x30804020u; s.w[1] = xy(x0, y0); s.w[2] = 0x00208040u; s.w[3] = xy(x1, y1); s.w[4] = 0x00402080u; s.w[5] = xy(x2, y2); s.w[6] = 0;
}
static void defs() {
    static const int H[4] = { 16, 32, 64, 128 }, W[6] = { 1, 2, 3, 4, 6, 8 };
    static const char* const TC[4] = { "tex col 16", "tex col 32", "tex col 64", "tex col 128" };
    static const char* const GC[4] = { "gou col 16", "gou col 32", "gou col 64", "gou col 128" };
    static const char* const TT[6] = { "tex tri 1", "tex tri 2", "tex tri 3", "tex tri 4", "tex tri 6", "tex tri 8" };
    static const char* const GT[6] = { "gou tri 1", "gou tri 2", "gou tri 3", "gou tri 4", "gou tri 6", "gou tri 8" };
    for (int i = 0; i < 4; i++) tex(TC[i], 40, 40, 41, 40 + H[i], 40, 40 + H[i]);
    for (int i = 0; i < 6; i++) tex(TT[i], 40, 40, 40 + W[i], 40, 40, 104);
    for (int i = 0; i < 4; i++) gou(GC[i], 40, 40, 41, 40 + H[i], 40, 40 + H[i]);
    for (int i = 0; i < 6; i++) gou(GT[i], 40, 40, 40 + W[i], 40, 40, 104);
}
static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return sh.n == 6 ? probe6(sh.w, s, n, k) : probe7(sh.w, s, n, k);
}
static uint32_t g_off = 20000u;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t search(const Shape& sh, uint32_t s, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync();
        const uint32_t st = probe(sh, s, mid);
        if (st & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static const int NI = 8;
static uint16_t r26[24][NI];
static uint32_t lo26[24];
static uint32_t base[24];
int main() {
    printf("\ngpu/gpu-tex-floor\n");
    printf("A thin triangle's words stored back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read:\n");
    printf("the first D with bit 26 set, at 256x240. No assertions.\n\n");
    defs();
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end */
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int q = 0; q < 4; q++) { waitReady(); write32(GP0, 0x02305070u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    waitReady();
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 0)); write32(GP0, xy(512, 240));
    waitReady();
    /* pass 428's own environment (hwtests/gpu-doom-feed/stream.h P0_ENV) */
    write32(GP0, 0xe100008au); write32(GP0, 0xe2010463u); write32(GP0, 0xe3000100u); write32(GP0, 0xe403bdffu); write32(GP0, 0xe5000100u);
    write32(GP1, 0x08000000u); write32(GP1, 0x03000000u);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int i = 0; i < NS; i++) {
        waitReady(); sync(); probe(S[i], 64, 9000); waitReady(); sync(); probe(S[i], 64, 9000); waitReady();   /* the code warm */
        g_off = 1500u; base[i] = search(S[i], 64, 0, 20001);
        g_off = 199000u;
        const uint32_t lo = base[i] > 64 ? base[i] - 64 : 0, hi = base[i] + base[i] / 4 + 256;
        lo26[i] = lo;
        for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 64 + 264 * j, lo, hi + 1); r26[i][j] = (uint16_t)(d > hi ? 0xFFFF : d - lo); }
    }
    waitReady();
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE2000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then from s = 64 + 264 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..7\n");
    printf("256x240 15-bit:\n");
    for (int i = 0; i < NS; i++) {
        printf("%-12s %5u :", S[i].name, (unsigned)base[i]);
        for (int j = 0; j < NI; j++) printf(" %d", r26[i][j] == 0xFFFF ? 999 : (int)(r26[i][j] + lo26[i]) - (int)base[i]);
        printf("\n");
    }
    printf("\ndone\n");
    return 0;
}
