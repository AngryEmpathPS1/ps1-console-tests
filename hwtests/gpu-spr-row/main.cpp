/*
 * gpu-spr-row: A FILLING SPRITE'S BARE ROW BY ITS WIDTH - one, two and three rows in the vertical blanking, no line start met.
 *
 * WHY. hwtests/gpu-spr-lines and gpu-spr-hold on four consoles: v0.377.348's line events read the consoles in total, and
 * what is over on a long sprite is its bare row - an 8-bit 256-wide row 449 cycles on the consoles against 452.4 here
 * (256 rows of it are the 256x256's +781 of hwtests/gpu-tex-noscan), a 128-wide one ~1.5 over, a 64-wide one right; a 4-bit
 * 256-wide one ~2 over. The fills are FillWB's own cycles since v0.377.347 and exact where they were measured (a filling
 * row less the cached row of its width); so the rest of the row - the flat rectangle's law the sprite formula carries -
 * is what is dear at these widths. Two widths do not make a curve.
 *
 * WHAT. Opaque sprites (65h) 1, 2 and 3 rows tall at ten widths (16 .. 320), 8-bit and 4-bit, every row filling (the
 * same sprite drawn from the page beside first), and the flat rectangle (60h) at the same widths as the control; their
 * words stored back to back 64 cycles after Timer 0's reset in the VERTICAL BLANKING at 320x240, bit 26's exact D. The
 * longest (8-bit 320x3) is ~1,700 cycles: no drawing reaches the next line start. No assertions.
 *
 * READ IT. D(3) - D(2) and D(2) - D(1) are the bare row at that width (alike if the row is a row); D(1) less it the set-up.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-row --emu`.
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
static const int WD[10] = { 16, 32, 64, 96, 128, 160, 192, 224, 256, 320 };
static const char* const KN[3] = { "8b fill", "4b fill", "flat" };
static const int KD[3] = { 1, 0, 3 };
static uint32_t W[4];
static void shape(int depth, int wd, int ht) {
    if (depth == 3) { W[0] = 0x00000000u; W[1] = 0x60808080u; W[2] = xy(0, 0); W[3] = xy(wd, ht); }
    else { W[0] = 0x65808080u; W[1] = xy(0, 0); W[2] = 0x3c000000u; W[3] = xy(wd, ht); }
}
static uint32_t g_off = 1500u;
static int g_depth = 3;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t once(uint32_t s, uint32_t d) {
    if (g_depth != 3) {   /* the primer: the same texels' cache entries from the page beside */
        waitReady(); write32(GP0, 0xE1000000u | 13u | ((uint32_t)(g_depth & 3) << 7)); for (int q = 0; q < 4; q++) write32(GP0, W[q]);
        waitReady(); write32(GP0, 0xE1000000u | 12u | ((uint32_t)(g_depth & 3) << 7));
    }
    sync();
    const uint32_t n = d / 3, k = d - 3 * n;
    return probe4(W, s, n, k);
}
static uint32_t search(uint32_t s, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(s, mid) & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static uint16_t D[3][10][3];
int main() {
    printf("\ngpu/gpu-spr-row\n");
    printf("A sprite's four words stored back to back 64 cycles after Timer 0's reset in the vertical blanking, D cycles, one\n");
    printf("GPUSTAT read: the first D with bit 26 set, at 1, 2 and 3 rows. No assertions.\n\n");
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
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (239u << 10) | 319u); write32(GP0, 0xE5000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320x240 */
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int k = 0; k < 3; k++) for (int w = 0; w < 10; w++) for (int h = 0; h < 3; h++) {
        g_depth = KD[k];
        shape(KD[k], WD[w], h + 1);
        once(64, 9000); once(64, 9000);   /* the code warm */
        D[k][w][h] = (uint16_t)search(64, 0, 4001);
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
    printf("width :  D at 1, 2, 3 rows\n");
    for (int k = 0; k < 3; k++) {
        printf("%s:\n", KN[k]);
        for (int w = 0; w < 10; w++) printf("  %3d : %u %u %u\n", WD[w], (unsigned)D[k][w][0], (unsigned)D[k][w][1], (unsigned)D[k][w][2]);
    }
    printf("\ngpu/gpu-spr-row: Done.\n");
    for (;;) VSync(0);
}
