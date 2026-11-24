/*
 * gpu-tri-grid: A SPACED TRIANGLE'S SET-UP AND DRAWING OVER A WIDTH x HEIGHT GRID, ROWS WIDENING AND ROWS SHRINKING (the GPU
 * session, 2026-11-24).
 *
 * WHY. With the CPU intake modelled (land_tricredit, v0.377.376: a spaced feed's set-up 3 cycles sooner), what is left between this
 * core and the consoles is the drawing, and it changes sign with the shape: gpu-tri-height's short apex-top triangles (1-6 rows,
 * 32 / 96 wide) end 2-4 cycles EARLY here, gpu-shape-time6's tiny flat-top ones (legs 1-5) 3-6 LATE, its shaded 320x16 shapes 2-8
 * late, and gour BR 320x16 (gpu-tri-height's own orientation) +2 where gpu-tri-height's 96-wide H 3 is -3. No term in the growth
 * of the rows alone (g = W / H in pixel time) carries all of them. This maps the surface the tests on disk only sample.
 *
 * WHAT. gpu-tri-height's probe and environment (VBlank + 1,500 cycles, Timer 0's line sync, T0 = 64; every word a load apart;
 * D cycles after the last word, ONE GPUSTAT read, D binary-searched for bit 26 and bit 28; its 320x240 display and drawing area).
 * Right triangles from (0, 40): "up" the apex at the top right, the rows widening leftwards - (W, 40), (0, 40 + H), (W, 40 + H),
 * gpu-tri-height's own shape; "dn" the top row whole, the rows shrinking - (0, 40), (W, 40), (W, 40 + H). W 8, 16, 32, 64, 128, 319;
 * H 1, 2, 3, 4, 6, 8, 12, 16. Flat (20h) and gouraud (30h). Once each (every console prints the same bytes on these probes).
 * No assertions.
 *
 * READ IT. bit 28 is the set-up's end, bit 26 - bit 28 the drawing: per W x H, the console's against this core's.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-tri-grid --emu`.
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
#define SLOTA(i) \
        "lw $t7, " #i "*4(%[w])\n" \
        "nop\n" \
        "sw $t7, 0x1810($t0)\n"
static uint32_t probeB(const uint32_t* w, uint32_t c, const uint32_t* v, uint32_t b, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\n"
        "lw $s0, 0(%[v])\n" "lw $s1, 4(%[v])\n" "lw $s2, 8(%[v])\n" "lw $s3, 12(%[v])\n"
        "lw $s4, 16(%[v])\n" "lw $s5, 20(%[v])\n" "lw $s6, 24(%[v])\n" "lw $s7, 28(%[v])\n"
        "la $t9, 14f\n"
        "sll $t2, %[b], 2\n"
        "subu $t9, $t9, $t2\n"          /* 14f - 4 x b: the last b of the eight stores */
        "lui $t0, 0x1f80\n"
        "la $t1, 2f\n"
        "sll $t2, %[k], 2\n"
        "subu $t1, $t1, $t2\n"
        "la $t8, 9f\n"
        "sll $t2, %[c], 2\n"
        "subu $t8, $t8, $t2\n"
        "sll $t2, %[c], 3\n"
        "subu $t8, $t8, $t2\n"
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
        "jr $t8\n"
        "nop\n"
        SLOTA(0) SLOTA(1) SLOTA(2) SLOTA(3) SLOTA(4) SLOTA(5) SLOTA(6) SLOTA(7)
        SLOTA(8) SLOTA(9) SLOTA(10) SLOTA(11) SLOTA(12) SLOTA(13) SLOTA(14) SLOTA(15)
        "9:\n"
        "jr $t9\n"
        "nop\n"
        "sw $s0, 0x1810($t0)\n"
        "sw $s1, 0x1810($t0)\n"
        "sw $s2, 0x1810($t0)\n"
        "sw $s3, 0x1810($t0)\n"
        "sw $s4, 0x1810($t0)\n"
        "sw $s5, 0x1810($t0)\n"
        "sw $s6, 0x1810($t0)\n"
        "sw $s7, 0x1810($t0)\n"
        "14:\n"
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
        : [st] "=r"(st), [n] "+r"(n) : [w] "r"(w), [c] "r"(c), [b] "r"(b), [v] "r"(v), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "memory");
    return st;
}


static const int NW = 6, NH = 8;
static const int WS[NW] = { 8, 16, 32, 64, 128, 319 };
static const int HS[NH] = { 1, 2, 3, 4, 6, 8, 12, 16 };
static uint32_t TA[16], TB[8]; static uint32_t g_n = 0;
static void setShape(int cls, int up, int W, int H) {   /* every word a load apart: the last n of tri-height's sixteen slots */
    uint32_t t[8]; int n = 0;
    const uint32_t v0 = up ? xy(W, 40) : xy(0, 40), v1 = up ? xy(0, 40 + H) : xy(W, 40), v2 = xy(W, 40 + H);
    if (cls == 0) { t[n++] = 0x20808080u; t[n++] = v0; t[n++] = v1; t[n++] = v2; }
    else { t[n++] = 0x30808080u; t[n++] = v0; t[n++] = 0x404040u; t[n++] = v1; t[n++] = 0x208020u; t[n++] = v2; }
    for (int i = 0; i < 16; i++) TA[i] = 0;
    for (int i = 0; i < 8; i++) TB[i] = 0;
    for (int i = 0; i < n; i++) TA[16 - n + i] = t[i];
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
    syncTH();
    return probeB(TA, g_n, TB, 0, 64, n, k);
}
static uint32_t search(uint32_t bit) {
    uint32_t lo = 0, hi = 8001;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(mid) & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static uint16_t D[2][2][NW][NH][2];
int main() {
    printf("\ngpu/gpu-tri-grid\n");
    printf("Spaced right triangles from (0,40), W x H, the rows widening (up) or shrinking (dn), flat and gouraud: bit 26 / bit 28's\n");
    printf("first D after the last word. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end */
    lastT = read16(T2_COUNT); clk = 0;
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
    for (int c = 0; c < 2; c++) for (int u = 0; u < 2; u++) for (int w = 0; w < NW; w++) for (int h = 0; h < NH; h++) {
        setShape(c, u, WS[w], HS[h]);
        if (w == 0 && h == 0) { once(9000); once(9000); }   /* the code warm */
        D[c][u][w][h][0] = (uint16_t)search(0x04000000u);
        D[c][u][w][h][1] = (uint16_t)search(0x10000000u);
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
    static const char* const CL[2] = { "ft", "gt" };
    static const char* const UP[2] = { "dn", "up" };
    printf("  H           :");
    for (int h = 0; h < NH; h++) printf(" %9d |", HS[h]);
    printf("\n");
    for (int c = 0; c < 2; c++) for (int u = 1; u >= 0; u--) for (int w = 0; w < NW; w++) {
        printf("  %s %s %3d    :", CL[c], UP[u], WS[w]);
        for (int h = 0; h < NH; h++) printf(" %4u %4u |", (unsigned)D[c][u][w][h][0], (unsigned)D[c][u][w][h][1]);
        printf("\n");
    }
    printf("\ngpu/gpu-tri-grid: Done.\n");
    for (;;) VSync(0);
    return 0;
}
