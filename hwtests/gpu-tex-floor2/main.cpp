/*
 * gpu-tex-floor2: TEXTURE FILLS DOWN A THIN TRIANGLE - what a fill costs a row 1 to 8 pixels wide.
 *
 * WHY. hwtests/gpu-tex-floor: thin shaded rows with every texel cached read this core to a cycle or two on the second
 * SCPH-5501 and the PSone (the floor of 2.59 pixels is right). hwtests/gpu-doom-cols: Doom's one-pixel columns, whose rows
 * fill, draw 12-67 cycles faster on those consoles than here. The candidate (experiments/audit/land_fillfloor.js): a fill
 * runs under its row's floor. Fitted on seven of the game's columns, whose fills fall where the game's texture put them.
 *
 * WHAT. gpu-tex-floor's own shapes - the column (x,y) (x+1,y+H) (x,y+H) at H 8 / 16 / 32 / 64 / 128 and the right triangle
 * (x,y) (x+w,y) (x,y+64) at w 1 / 2 / 3 / 4 / 6 / 8 - with V running 0 to 128 down the shape at one U, 8-bit texels: the rows
 * read texture lines of their own, and lines 64 apart share a cache entry, so a draw evicts what the one before left and
 * rows fill on every draw (how many is the cache's business: this core fills about a third of them, PREDICTED.txt). The
 * column at H 128 with V 0 to 64 is the same walk over half the lines. In the blanking at 256x240; four starts across a
 * line. The same shapes with every texel cached are gpu-tex-floor's rows: the difference is the fills'.
 * No assertions.
 *
 * READ IT. This D less gpu-tex-floor's D for the same shape is the fills' time. The candidate says the console's is shorter
 * than this core's on the rows narrower than the floor (the columns, the triangles 1 and 2 wide) and not on the wider ones.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-tex-floor2 --emu`.
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
struct Shape { const char* name; int n; uint32_t w[7]; };
static Shape S[24];
static int NS = 0;
static const uint32_t UV = 0x1010u;
static void tex(const char* nm, int x0, int y0, int x1, int y1, int x2, int y2, int vBot) {
    Shape& s = S[NS++]; s.name = nm; s.n = 7;
    const uint32_t u = 0x10u, top = u, bot = u | ((uint32_t)vBot << 8);
    s.w[0] = 0x24808080u; s.w[1] = xy(x0, y0); s.w[2] = 0x3c000000u | top; s.w[3] = xy(x1, y1); s.w[4] = 0x008c0000u | (y1 == y0 ? top : bot); s.w[5] = xy(x2, y2); s.w[6] = bot;
}
static void defs() {
    static const int H[5] = { 8, 16, 32, 64, 128 }, W[6] = { 1, 2, 3, 4, 6, 8 };
    static const char* const TC[5] = { "fill col 8", "fill col 16", "fill col 32", "fill col 64", "fill col 128" };
    static const char* const TT[6] = { "fill tri 1", "fill tri 2", "fill tri 3", "fill tri 4", "fill tri 6", "fill tri 8" };
    for (int i = 0; i < 5; i++) tex(TC[i], 40, 40, 41, 40 + H[i], 40, 40 + H[i], 128);
    for (int i = 0; i < 6; i++) tex(TT[i], 40, 40, 40 + W[i], 40, 40, 104, 128);
    tex("half col 128", 40, 40, 41, 168, 40, 168, 64);
}
static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return probe7(sh.w, s, n, k);
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

static const int NI = 4;
static uint16_t r26[24][NI];
static uint32_t lo26[24];
static uint32_t base[24];
int main() {
    printf("\ngpu/gpu-tex-floor2\n");
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
        g_off = 1500u; base[i] = search(S[i], 64, 0, 40001);
        g_off = 199000u;
        const uint32_t lo = base[i] > 64 ? base[i] - 64 : 0, hi = base[i] + base[i] / 4 + 256;
        lo26[i] = lo;
        for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 64 + 528 * j, lo, hi + 1); r26[i][j] = (uint16_t)(d > hi ? 0xFFFF : d - lo); }
    }
    waitReady();
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE2000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then from s = 64 + 528 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..3\n");
    printf("256x240 15-bit:\n");
    for (int i = 0; i < NS; i++) {
        printf("%-14s %5u :", S[i].name, (unsigned)base[i]);
        for (int j = 0; j < NI; j++) printf(" %d", r26[i][j] == 0xFFFF ? 999 : (int)(r26[i][j] + lo26[i]) - (int)base[i]);
        printf("\n");
    }
    printf("\ndone\n");
    return 0;
}
