/*
 * gpu-tri-lag: WHAT DECIDES THE CPU INTAKE'S LAG ON A TRIANGLE'S LAST WORD - the same four back-to-back stores, after each of the
 * lead-ins the two tests that disagree use (the GPU session, 2026-11-23).
 *
 * WHY. land_trislope's intake lag (STATUS "trislope split"): gpu-tri-height's consoles pay 5 cycles for a flat triangle's four
 * words stored back to back over the same words a load apart, at every height (7 gouraud) - and gpu-shape-time6's consoles pay
 * none for the same four back-to-back stores (flat TL 2: bit 26 at 42..42 at all eight starts, the slope term alone; the lag
 * would make it 46). Both run in the vertical blanking. They differ around the stores: tri-height polls Timer 0 with lhu, slides
 * into a nop run by a computed jr, then jumps twice (jr $t8 over its sixteen spaced slots, jr $t9 into its store run) before
 * the four sw; shape-time6 stores straight after lui / la / sll / subu, synced to VBlank alone.
 *
 * WHAT. tri-height's harness (VBlank + 1,500 cycles, Timer 0's line sync, T0 = 64, D cycles after the last word, ONE GPUSTAT
 * read, D binary-searched for bit 26 and for bit 28). Three flat triangles: tri-height's 32 x 4 at (40,40) and its 32 x 0 (no
 * area), and shape-time6's flat TL 2 at (192,128). Each reached by:
 *   jj   tri-height's lead-in: jr over 48 instructions, jr over 4, then the stores
 *   j    one jr over 4, then the stores
 *   n    four nops
 *   alu  lui / lui / addiu / sll / subu (shape-time6's lead-in)
 *   io   lhu T0_COUNT, nop (an I/O read just before)
 *   a    every word a load apart (lw / nop / sw): tri-height's "a" reference
 *   st6  shape-time6's own probe (its lead-in, its tail) after its own sync (VBlank + 20,000 cycles, no Timer 0 wait)
 * Twice each. No assertions.
 *
 * READ IT. Per triangle, each lead-in's bit 26 against "a": the lag that lead-in pays. If jj pays 5 and alu / st6 pay none, the
 * lag belongs to what jj does that alu does not (a jump, the slide, the instruction fetch after it), and the rows between say
 * which.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-tri-lag --emu`.
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

/* tri-height's probe, its lead-in made a parameter: load the four words into s4-s7, wait for a fresh Timer 0 line (T0 >= 200,
   then < 64), delay to T0 = s exactly (3-cycle loop, its remainder in a nop slide), the lead-in, the four stores, delay D
   (n x 3 + k) and read GPUSTAT. */
#define HEAD \
        ".set noreorder\n" \
        "lw $s4, 0(%[v])\n" "lw $s5, 4(%[v])\n" "lw $s6, 8(%[v])\n" "lw $s7, 12(%[v])\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n" \
        "la $t8, 9f\n" \
        "la $t9, 13f\n" \
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
#define STORES \
        "13:\n" \
        "sw $s4, 0x1810($t0)\n" \
        "sw $s5, 0x1810($t0)\n" \
        "sw $s6, 0x1810($t0)\n" \
        "sw $s7, 0x1810($t0)\n"
#define TAIL \
        "14:\n" \
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
#define N4 "nop\n" "nop\n" "nop\n" "nop\n"
#define N16 N4 N4 N4 N4
#define SLOTA(i) "lw $t7, " #i "*4(%[v])\n" "nop\n" "sw $t7, 0x1810($t0)\n"
#define LEAD_JJ "jr $t8\n" "nop\n" N16 N16 N16 "9:\n" "jr $t9\n" "nop\n" N4
#define LEAD_J  "jr $t9\n" "nop\n" N4 "9:\n"
#define LEAD_N  N4 "9:\n"
#define LEAD_ALU "lui $t2, 0x1f80\n" "lui $t3, 0\n" "addiu $t3, $t3, 0\n" "sll $t2, $t2, 2\n" "subu $t2, $t2, $t3\n" "9:\n"
#define LEAD_IO "lhu $t3, 0x1100($t0)\n" "nop\n" "9:\n"
#define LEAD_A  SLOTA(0) SLOTA(1) SLOTA(2) SLOTA(3) "b 14f\n" "nop\n" "9:\n"
#define PROBE(name, LEAD) \
static uint32_t name(const uint32_t* v, uint32_t s, uint32_t n, uint32_t k) { \
    uint32_t st; \
    __asm__ volatile(HEAD LEAD STORES TAIL \
        : [st] "=r"(st), [n] "+r"(n) : [v] "r"(v), [s] "r"(s), [k] "r"(k) \
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "s4", "s5", "s6", "s7", "memory"); \
    return st; \
}
PROBE(probeJJ, LEAD_JJ)
PROBE(probeJ, LEAD_J)
PROBE(probeN, LEAD_N)
PROBE(probeALU, LEAD_ALU)
PROBE(probeIO, LEAD_IO)
PROBE(probeA, LEAD_A)

/* gpu-shape-time6's probe4, as it is there */
#define ST6_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n"
#define ST6_TAIL \
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
static uint32_t probeST6(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(ST6_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        ST6_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}

static void syncTH() {   /* gpu-tri-height's */
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 1500u) {} }
}
static void syncST6() {   /* gpu-shape-time6's, its first start point */
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
}

static const int NS = 3, NV = 7;
static const char* const SHAPE[NS] = { "ft 32x4 (40,40)", "ft 32x0 (40,40)", "flat TL 2 (192,128)" };
static const char* const WAY[NV] = { "jj", "j", "n", "alu", "io", "a", "st6" };
static uint32_t TW[NS][4];
static void shapes() {
    TW[0][0] = 0x20808080u; TW[0][1] = xy(72, 40); TW[0][2] = xy(40, 44); TW[0][3] = xy(72, 44);
    TW[1][0] = 0x20808080u; TW[1][1] = xy(72, 40); TW[1][2] = xy(40, 40); TW[1][3] = xy(72, 40);
    TW[2][0] = 0x20408020u; TW[2][1] = xy(192, 128); TW[2][2] = xy(194, 128); TW[2][3] = xy(192, 130);
}
static uint32_t once(int sh, int way, uint32_t d) {
    waitReady(); write32(GP0, 0xE1000008u);
    const uint32_t n = d / 3, k = d - 3 * n;
    if (way == 6) { syncST6(); return probeST6(TW[sh], n, k); }
    syncTH();
    switch (way) {
        case 0: return probeJJ(TW[sh], 64, n, k);
        case 1: return probeJ(TW[sh], 64, n, k);
        case 2: return probeN(TW[sh], 64, n, k);
        case 3: return probeALU(TW[sh], 64, n, k);
        case 4: return probeIO(TW[sh], 64, n, k);
        default: return probeA(TW[sh], 64, n, k);
    }
}
static uint32_t search(int sh, int way, uint32_t bit) {
    uint32_t lo = 0, hi = 4001;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(sh, way, mid) & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static uint16_t D[NS][NV][2][2];
int main() {
    printf("\ngpu/gpu-tri-lag\n");
    printf("Three flat triangles, their four words stored back to back after each lead-in (jj = gpu-tri-height's, alu / st6 =\n");
    printf("gpu-shape-time6's) or a load apart (a): bit 26's and bit 28's first D after the last word, twice each. No assertions.\n\n");
    shapes();
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
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (239u << 10) | 319u); write32(GP0, 0xE5000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320x240, as gpu-tri-height */
    write32(GP1, 0x05000000u | (256u << 10));
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int s = 0; s < NS; s++) for (int v = 0; v < NV; v++) for (int r = 0; r < 2; r++) {
        if (r == 0) { once(s, v, 9000); once(s, v, 9000); }   /* the code warm */
        D[s][v][0][r] = (uint16_t)search(s, v, 0x04000000u);
        D[s][v][1][r] = (uint16_t)search(s, v, 0x10000000u);
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
    printf("  %-20s :", "lead-in");
    for (int v = 0; v < NV; v++) printf(" %-11s|", WAY[v]);
    printf("\n");
    for (int s = 0; s < NS; s++) for (int b = 0; b < 2; b++) {
        printf("  %-20s :", b ? "  bit 28" : SHAPE[s]);
        for (int v = 0; v < NV; v++) printf(" %4u %4u  |", (unsigned)D[s][v][b][0], (unsigned)D[s][v][b][1]);
        printf("\n");
    }
    printf("\ngpu/gpu-tri-lag: Done.\n");
    for (;;) VSync(0);
    return 0;
}
