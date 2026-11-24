/*
 * gpu-dma-refgap: WHEN A DMA2 BURST PAYS A DRAM REFRESH AT ITS START - the idle gap before it against its length (the GPU session,
 * 2026-11-24).
 *
 * WHY. hwtests/gpu-dma-burstlat's consoles (PSone, 5501-u2, 7001, 9001) pay no refresh at a SHORT burst's start after an idle bus,
 * where this core charges one that had already run (refreshIdle leaves it up to a period behind; charge / runBlock hold for it):
 * experiments/intake/short/mk_bri.js, a burst skipping it, matches burstlat to the byte. But gpu-a0abort8's consoles side with this
 * core on its 258-word burst after an idle poll (two refreshes here). The idle gap before the burst and the burst's length against
 * the 256-cycle period are what the two differ in; this sweeps both.
 *
 * WHAT. gpu-dma-burstlat's probe (Timer 0's line, the CHCR2 store at T0 = s, D cycles, ONE GPUSTAT read; D searched for bit 28, which
 * follows the burst's end: ~end + 5 for nops). A burst of k GP0 nops, k 4, 32, 128, 240, 260, 300 (and k 0: a flat 32 x 4 triangle,
 * burstlat's pre 0), at s 64, 96, 128, 192, 256, 320, 384, 448 - the idle stretch before the store (the CPU polling Timer 0, no DRAM)
 * growing with s. gpu-tri-height's environment. Twice each. No assertions.
 *
 * READ IT. Per k, bit 28's D against s: a 10-cycle step where a refresh is paid (DMA_BURST_REFRESH_ROWS), and where it moves with s.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-dma-refgap --emu`.
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
   the words (the last c of the sixteen slots: the jump into them is computed before the wait), delay D (n x 3 + k) and read
   GPUSTAT. */
/* From a fresh Timer 0 reset, delay to T0 = s exactly, store the first words (the last c of sixteen slots, a load apart), then
   the last b back to back from s0-s7 (loaded before the wait), delay D, read GPUSTAT. */

#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
/* gpu-tri-height's timing head (a fresh Timer 0 line, T0 = s exactly), then ONE store: DMA2's CHCR, a burst of the packet's words
   from RAM (the CPU held while it runs), delay D (n x 3 + k), read GPUSTAT */
static uint32_t probeD(uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st; const uint32_t ch = 0x11000001u;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "la $t1, 2f\n"
        "sll $t2, %[k], 2\n"
        "subu $t1, $t1, $t2\n"
        "5:\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "sltiu $t5, $t3, 200\n"
        "bnez $t5, 5b\n"
        "nop\n"
        "6:\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "sltiu $t5, $t3, 64\n"
        "beqz $t5, 6b\n"
        "nop\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "subu $t4, %[s], $t3\n"
        "7:\n"
        "addiu $t4, $t4, -3\n"
        "bgtz $t4, 7b\n"
        "nop\n"
        "la $t6, 8f\n"
        "sll $t5, $t4, 2\n"
        "addiu $t6, $t6, -8\n"
        "subu $t6, $t6, $t5\n"
        "jr $t6\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "8:\n"
        "sw %[ch], 0x10A8($t0)\n"
        "nop\n"
        "beqz %[n], 3f\n"
        "nop\n"
        "4:\n"
        "addiu %[n], %[n], -1\n"
        "bnez %[n], 4b\n"
        "nop\n"
        "3:\n"
        "jr $t1\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "2:\n"
        "lw %[st], 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : [st] "=r"(st), [n] "+r"(n) : [s] "r"(s), [k] "r"(k), [ch] "r"(ch)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}


static const int NK = 7, NS = 8;
static const int KS[NK] = { 0, 4, 32, 128, 240, 260, 300 };   /* k 0: the triangle alone (gpu-dma-burstlat's pre 0) */
static const int SS[NS] = { 64, 96, 128, 192, 256, 320, 384, 448 };
static uint32_t PK[304] __attribute__((aligned(16))); static uint32_t g_n = 0, g_s = 64;
static void setWords(int k) {
    int n = 0;
    if (k == 0) { PK[n++] = 0x20808080u; PK[n++] = xy(72, 40); PK[n++] = xy(40, 44); PK[n++] = xy(72, 44); }   /* gpu-tri-height's 32 x 4 */
    else for (int i = 0; i < k; i++) PK[n++] = 0;
    g_n = (uint32_t)n;
}
static void syncTH() {   /* gpu-tri-height's */
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 1500u) {} }
}
static uint32_t once(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    waitReady(); write32(GP0, 0xE1000008u);   /* as gpu-tri-height before each probe */
    write32(GP1, 0x04000002u);                /* DMA direction: to GP0 */
    write32(MADR2, (uint32_t)PK & 0xFFFFFCu);
    write32(BCR2, g_n);
    syncTH();
    const uint32_t st = probeD(g_s, n, k);
    { uint32_t p = 0; while ((read32(CHCR2) & 0x01000000u) && p < 200000) p++; if (p >= 200000) write32(CHCR2, 0); }
    write32(GP1, 0x04000000u);
    return st;
}
static uint32_t search(uint32_t bit) {
    uint32_t lo = 0, hi = 8001;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(mid) & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static uint16_t D[NK][NS][2];
int main() {
    printf("\ngpu/gpu-dma-refgap\n");
    printf("One DMA2 burst of k nops (k 0: a flat 32x4 triangle) stored at Timer 0 = s after a line's hblank: bit 28's first D after\n");
    printf("the CHCR2 store, twice each. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end */
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);   /* DMA2 enabled */
    write32(GP1, 0x04000000u);   /* gpu-tri-height's environment from here */
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int q = 0; q < 4; q++) { waitReady(); write32(GP0, 0x02305070u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    waitReady();
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (239u << 10) | 319u); write32(GP0, 0xE5000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320x240 */
    write32(GP1, 0x05000000u | (256u << 10));
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int i = 0; i < NK; i++) for (int j = 0; j < NS; j++) for (int r = 0; r < 2; r++) {
        setWords(KS[i]); g_s = (uint32_t)SS[j];
        if (r == 0 && j == 0) { once(9000); once(9000); }   /* the code warm */
        D[i][j][r] = (uint16_t)search(0x10000000u);
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
    printf("  s          :");
    for (int j = 0; j < NS; j++) printf(" %11d |", SS[j]);
    printf("\n");
    for (int i = 0; i < NK; i++) {
        printf("  k %3d      :", KS[i]);
        for (int j = 0; j < NS; j++) printf(" %5u %5u |", (unsigned)D[i][j][0], (unsigned)D[i][j][1]);
        printf("\n");
    }
    printf("\ngpu/gpu-dma-refgap: Done.\n");
    for (;;) VSync(0);
    return 0;
}
