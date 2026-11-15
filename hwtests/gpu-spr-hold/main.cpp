/*
 * gpu-spr-hold: WHAT A SPRITE PAYS FOR ONE LINE START - sprites shorter than a display line, started at 32 places across it.
 *
 * WHY. hwtests/gpu-tex-noscan on the second SCPH-5501 against v0.377.348: a 256x256 sprite reads +0.58% (4-bit) and +0.63%
 * (8-bit) here, a 128x128 8-bit one +0.46%, the 64x64 ones right - with the display range EMPTY as with it on at 320. Since
 * v0.377.347 a 4- or 8-bit sprite's fills are FillWB's own cycles (exact against the consoles row for row), its line
 * hold's average give-back (GPU_SCAN_HOLD.xQ) is cancelled inside its formula, and the hold is added at each line start
 * the sprite crosses: 57 line starts in the 8-bit 256x256, ~13.5 cycles each = the excess. So either a row that is filling
 * its texture cache does not feel the line start as a cached row does, or the hold it feels is smaller than this core's.
 * No test times a FILLING sprite across one line start: the polled ones cross dozens and give an average.
 *
 * WHAT. Opaque variable-size sprites (65h) whose whole drawing is shorter than a line, their words stored back to back s
 * cycles after Timer 0's reset (the hblank's end), then D cycles and one GPUSTAT read: the first D with bit 26 set.
 * In the blanking first (no line start to feel: the bare cost), then from s = 64 + 67 j, j = 0 .. 31 (a whole line), in
 * the frame at 256x240. Rows: 8-bit and 4-bit, filling (drawn from another page first, so no texel is cached - or wider
 * than the cache holds: 64 texels at 4 and 8 bits) and cached (64 wide, drawn before and nothing between); one 15-bit; two flat
 * rectangles as the control (their hold is modelled and exact, hwtests/gpu-shape-time26). No assertions.
 *
 * READ IT. A row's numbers are D less the blanking's at each start: 0 where the drawing met no line start, the hold where
 * it met one. Where the consoles' plateau is lower than the emulator's on the filling rows and equal on the cached and flat
 * ones, the hold hides in the fills.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-hold --emu`.
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
static uint32_t probe4(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}
/* depth 0 / 1 / 2 = 4 / 8 / 15 bits, 3 = flat; prime: drawn from the page beside first, so the timed draw finds no texel cached */
struct Shape { const char* name; int depth, wd, ht, prime; uint32_t w[4]; };
static Shape S[20];
static int NS = 0;
static void spr(const char* nm, int depth, int wd, int ht, int prime) {
    Shape& s = S[NS++]; s.name = nm; s.depth = depth; s.wd = wd; s.ht = ht; s.prime = prime;
    if (depth == 3) { s.w[0] = 0x00000000u; s.w[1] = 0x60808080u; s.w[2] = xy(32, 40); s.w[3] = xy(wd, ht); }
    else { s.w[0] = 0x65808080u; s.w[1] = xy(32, 40); s.w[2] = 0x3c000000u; s.w[3] = xy(wd, ht); }
}
static uint32_t page(int depth, int alt) { return 0xE1000000u | (uint32_t)(alt ? 13 : 12) | ((uint32_t)(depth & 3) << 7); }
static void defs() {
    spr("8b 256x4 fill", 1, 256, 4, 1);
    spr("8b 256x2 fill", 1, 256, 2, 1);
    spr("8b 128x8 fill", 1, 128, 8, 1);
    spr("8b 128x4 fill", 1, 128, 4, 1);
    spr("8b 64x8 fill", 1, 64, 8, 1);
    spr("8b 64x8 cached", 1, 64, 8, 0);
    spr("8b 64x16 cached", 1, 64, 16, 0);
    spr("4b 256x4 fill", 0, 256, 4, 1);
    spr("4b 64x16 cached", 0, 64, 16, 0);
    spr("4b 64x32 cached", 0, 64, 32, 0);
    spr("4b 128x8 fill", 0, 128, 8, 1);
    spr("4b 64x16 fill", 0, 64, 16, 1);
    spr("15b 256x2", 2, 256, 2, 0);
    spr("flat 256x8", 3, 256, 8, 0);
    spr("flat 64x24", 3, 64, 24, 0);
}
static uint32_t g_off = 20000u;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t once(const Shape& sh, uint32_t s, uint32_t d) {
    if (sh.depth != 3) {
        if (sh.prime) { waitReady(); write32(GP0, page(sh.depth, 1)); for (int q = 0; q < 4; q++) write32(GP0, sh.w[q]); }
        waitReady(); write32(GP0, page(sh.depth, 0));
    }
    sync();
    const uint32_t n = d / 3, k = d - 3 * n;
    return probe4(sh.w, s, n, k);
}
static uint32_t search(const Shape& sh, uint32_t s, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(sh, s, mid) & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static const int NI = 32;
static uint16_t r26[20][NI];
static uint32_t lo26[20];
static uint32_t base[20];
int main() {
    printf("\ngpu/gpu-spr-hold\n");
    printf("A sprite's four words stored back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read: the\n");
    printf("first D with bit 26 set, at 256x240. No assertions.\n\n");
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
    /* every texel and every CLUT entry one non-zero colour: all pixels drawn, at any depth */
    for (int q = 0; q < 4; q++) { waitReady(); write32(GP0, 0x02305070u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    waitReady();
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 0)); write32(GP0, xy(512, 240));
    waitReady();
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (239u << 10) | 319u); write32(GP0, 0xE5000000u);
    write32(GP1, 0x08000000u); write32(GP1, 0x03000000u);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int i = 0; i < NS; i++) {
        g_off = 1500u;
        once(S[i], 64, 9000); once(S[i], 64, 9000);   /* the code warm */
        base[i] = search(S[i], 64, 0, 20001);
        g_off = 199000u;
        const uint32_t lo = base[i] > 64 ? base[i] - 64 : 0, hi = base[i] + base[i] / 4 + 256;
        lo26[i] = lo;
        for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 64 + 67 * j, lo, hi + 1); r26[i][j] = (uint16_t)(d > hi ? 0xFFFF : d - lo); }
    }
    waitReady();
    write32(GP0, 0xE1000400u); write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u);
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 0)); write32(GP0, xy(1023, 511));
    waitReady();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then D less it from s = 64 + 67 j cycles after Timer 0's reset ~93 lines after VBlank, j = 0..31\n");
    printf("256x240 15-bit:\n");
    for (int i = 0; i < NS; i++) {
        printf("%-16s %5u :", S[i].name, (unsigned)base[i]);
        for (int j = 0; j < NI; j++) printf(" %d", r26[i][j] == 0xFFFF ? 999 : (int)(r26[i][j] + lo26[i]) - (int)base[i]);
        printf("\n");
    }
    printf("\ngpu/gpu-spr-hold: Done.\n");
    for (;;) VSync(0);
}
