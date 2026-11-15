/*
 * gpu-spr-first: A SPRITE'S FIRST ROW - is the 3 cycles a filling sprite pays over this core its own, or the texture page
 * set just before it?
 *
 * WHY. hwtests/gpu-spr-row on three new-GPU consoles: D at one row less the bare row is 19 cycles for an 8-bit filling
 * sprite at every width and 23 for a 4-bit one, where v0.377.349 says 16 and 20 (the flat rectangle 26 against 25). That
 * test evicts the texels by drawing the same sprite from the page beside first, so a GP0(E1h) sits between the two draws
 * (waited out, a whole frame before the timed sprite). Either a filling sprite's first fill costs 3 more than the rest, or
 * the first textured draw behind a texture page change does.
 *
 * WHAT. One- and two-row opaque sprites at 16, 64 and 256 wide, 8-bit and 4-bit, in the vertical blanking, bit 26's exact
 * D, five ways:
 *   page   filling, evicted by the same sprite from the page beside (GP0(E1h) twice between) - gpu-spr-row's way
 *   line   filling, evicted by a sprite of the SAME page whose texels share the cache entries (V + 32 at 8 bits, + 64 at
 *          4): no GP0(E1h) anywhere near
 *   line+E the same, and GP0(E1h) with the page's own value sent again before the timed sprite
 *   cached drawn before, nothing between (64 wide and under: wider rows never stay cached)
 *   cach+E cached, and GP0(E1h) with the same value sent again before it
 * No assertions.
 *
 * READ IT. "line" against "page" is the page change; "line+E" against "line" and "cach+E" against "cached" is a GP0(E1h)
 * that changes nothing. "line" at one row less the bare row (D at two rows less D at one) is the filling sprite's own
 * first-row cost.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-first --emu`.
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
static const int WD[3] = { 16, 64, 256 };
static const char* const WAY[5] = { "page", "line", "line+E", "cached", "cach+E" };
static uint32_t W[4], P[4];
static int g_depth = 1, g_way = 0;
static uint32_t pageW(int alt) { return 0xE1000000u | (uint32_t)(alt ? 13 : 12) | ((uint32_t)(g_depth & 3) << 7); }
static void shape(int wd, int ht) {
    W[0] = 0x65808080u; W[1] = xy(0, 0); W[2] = 0x3c000000u; W[3] = xy(wd, ht);
    P[0] = 0x65808080u; P[1] = xy(0, 0); P[2] = 0x3c000000u | ((uint32_t)(g_depth == 1 ? 32 : 64) << 8); P[3] = xy(wd, ht);   /* the same cache entries, other texels */
}
static uint32_t g_off = 1500u;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t once(uint32_t s, uint32_t d) {
    waitReady();
    switch (g_way) {
        case 0: write32(GP0, pageW(1)); for (int q = 0; q < 4; q++) write32(GP0, W[q]); waitReady(); write32(GP0, pageW(0)); break;
        case 1: for (int q = 0; q < 4; q++) write32(GP0, P[q]); break;
        case 2: for (int q = 0; q < 4; q++) write32(GP0, P[q]); waitReady(); write32(GP0, pageW(0)); break;
        case 3: break;
        default: write32(GP0, pageW(0)); break;
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

static uint16_t D[2][5][3][2];
int main() {
    printf("\ngpu/gpu-spr-first\n");
    printf("A sprite's four words stored back to back 64 cycles after Timer 0's reset in the vertical blanking, D cycles, one\n");
    printf("GPUSTAT read: the first D with bit 26 set, at 1 and 2 rows, its texels evicted or cached five ways. No assertions.\n\n");
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
    for (int k = 0; k < 2; k++) {
        g_depth = k ? 0 : 1;
        waitReady(); write32(GP0, pageW(0));
        for (int y = 0; y < 5; y++) for (int w = 0; w < 3; w++) for (int h = 0; h < 2; h++) {
            D[k][y][w][h] = 0;
            if (y >= 3 && WD[w] > 64) continue;   /* a row wider than the cache holds never stays cached */
            g_way = y;
            shape(WD[w], h + 1);
            once(64, 9000); once(64, 9000);   /* the code warm; the cached ways' texels in */
            D[k][y][w][h] = (uint16_t)search(64, 0, 4001);
        }
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
    printf("way     : D at 1 and 2 rows, at 16 / 64 / 256 wide (0 0: not run)\n");
    for (int k = 0; k < 2; k++) {
        printf(k ? "4-bit:\n" : "8-bit:\n");
        for (int y = 0; y < 5; y++) {
            printf("  %-7s :", WAY[y]);
            for (int w = 0; w < 3; w++) printf("  %u %u", (unsigned)D[k][y][w][0], (unsigned)D[k][y][w][1]);
            printf("\n");
        }
    }
    printf("\ngpu/gpu-spr-first: Done.\n");
    for (;;) VSync(0);
}
