/*
 * gpu-spr-w1: A ONE-ROW SPRITE AT EVERY WIDTH 1 .. 72 - where its set-up steps.
 *
 * WHY. hwtests/gpu-spr-first and gpu-spr-row on three new-GPU consoles (the same bytes on all): a one-row sprite reads 1
 * cycle over v0.377.351 cached or 16 wide (32 against 31, 48 / 47), 3 over filling at 64 wide and up (132 / 129, 468 /
 * 465; 4-bit 96 / 93, 312 / 309), and at exactly 32 wide everything reads 5 over - the flat rectangle too (46 against 41),
 * where this core dips 2 under its own line and the console's flat one sits 2 over. The texture page change is not it
 * (gpu-spr-first: five ways alike). Three or five cycles a sprite is nothing on one and a per cent of a frame on two
 * thousand. Five widths do not say where the steps are.
 *
 * WHAT. One-row opaque sprites and flat rectangles at every width 1 .. 72, in the vertical blanking, bit 26's exact D:
 * 8-bit filling and 4-bit filling (evicted by a sprite of the same page whose texels share the cache entries), 4-bit
 * cached (64 and under), flat (60h, a no-operation word ahead of its three so every shape is four words). No assertions.
 *
 * READ IT. Each row against the emulator's, width by width: a constant is the set-up, a step at a width the block (32
 * pixels on FillWB's clock) or the pair that begins there.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-w1 --emu`.
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
static const char* const KN[4] = { "8b fill", "4b fill", "4b cached", "flat" };
static uint32_t W[4], P[4];
static int g_kind = 0;
static void shape(int wd) {
    if (g_kind == 3) { W[0] = 0x00000000u; W[1] = 0x60808080u; W[2] = xy(0, 0); W[3] = xy(wd, 1); return; }
    W[0] = 0x65808080u; W[1] = xy(0, 0); W[2] = 0x3c000000u; W[3] = xy(wd, 1);
    P[0] = 0x65808080u; P[1] = xy(0, 0); P[2] = 0x3c000000u | ((uint32_t)(g_kind == 0 ? 32 : 64) << 8); P[3] = xy(wd, 1);   /* the same cache entries, other texels */
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
    if (g_kind < 2) for (int q = 0; q < 4; q++) write32(GP0, P[q]);
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

static const int NW = 72;
static uint16_t D[4][NW];
int main() {
    printf("\ngpu/gpu-spr-w1\n");
    printf("A one-row sprite's four words stored back to back 64 cycles after Timer 0's reset in the vertical blanking, D cycles,\n");
    printf("one GPUSTAT read: the first D with bit 26 set, at every width 1 .. 72. No assertions.\n\n");
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
    for (int k = 0; k < 4; k++) {
        g_kind = k;
        waitReady(); write32(GP0, 0xE1000000u | 12u | (k == 0 ? 0x80u : 0u));
        uint32_t prev = 0;
        for (int w = 1; w <= NW; w++) {
            D[k][w - 1] = 0;
            if (k == 2 && w > 64) continue;
            shape(w);
            once(64, 9000); once(64, 9000);   /* the code warm; the cached kind's texels in */
            uint32_t d = prev ? search(64, prev > 24 ? prev - 24 : 0, prev + 40) : search(64, 0, 2001);
            if (prev && (d == (prev > 24 ? prev - 24 : 0) || d >= prev + 40)) d = search(64, 0, 2001);   /* outside the guess: the whole range */
            D[k][w - 1] = (uint16_t)d; prev = d;
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
    printf("D at widths 1 .. 72 (0: not run)\n");
    for (int k = 0; k < 4; k++) {
        printf("%-9s :", KN[k]);
        for (int w = 0; w < NW; w++) printf(" %u", (unsigned)D[k][w]);
        printf("\n");
    }
    printf("\ngpu/gpu-spr-w1: Done.\n");
    for (;;) VSync(0);
}
