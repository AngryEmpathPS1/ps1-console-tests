/*
 * gpu-rect-tall: NARROW FLAT RECTANGLES AT 16 TO 128 ROWS, exact, three words and four - is a row under 8 wide the same
 * at 128 rows as at 16?
 *
 * WHY. v0.377.352 refitted the flat rectangle's rows under 8 wide with its last write's tail (a row 4.5 cycles to 3 wide,
 * 4 + half a cycle a pair after) on hwtests/gpu-spr-w1 / -w2's one, two and three rows and hwtests/gpu-shape-time24's
 * sixteen (the SCPH-9001: 88 90 99 108 117 at W 1 2 4 6 8). hwtests/gpu-version's TIMING row draws a 3 x 128 rectangle,
 * polled: 724 cycles on the consoles cold and 654 warm, where v0.377.351 read 626 / 652 and v0.377.352 reads 758 / 693 -
 * 128 rows of the refit's extra third of a cycle. Nothing times a narrow rectangle past sixteen rows exactly; and every
 * four-word probe row of the gpu-spr sweeps reads 1 over this core where the three-word tests read it exactly.
 *
 * WHAT. Flat opaque rectangles (60h) W 1, 2, 3, 4, 6, 8, 16 by H 16, 32, 64, 128, and 64 x 16 and 64 x 64, in the vertical
 * blanking at 320x240, bit 26's exact D - each twice: its three words stored back to back ("3w"), and a GP0(00h) word
 * ahead of them ("4w", the gpu-spr sweeps' way). The longest (64 x 64) is ~2,200 cycles: it alone meets a line start.
 * No assertions.
 *
 * READ IT. D by H at each width is the row; 4w less 3w is what the word ahead costs.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-rect-tall --emu`.
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
static uint32_t probe3(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}
static const int NS = 30;
static int SW[NS], SH[NS];
static uint32_t W[4];
static int g_four = 0;
static uint32_t g_off = 1500u;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t once(uint32_t s, uint32_t d) {
    sync();
    const uint32_t n = d / 3, k = d - 3 * n;
    return g_four ? probe4(W, s, n, k) : probe3(W + 1, s, n, k);
}
static uint32_t search(uint32_t s, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(s, mid) & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static uint16_t D[2][NS];
int main() {
    printf("\ngpu/gpu-rect-tall\n");
    printf("A flat rectangle's words stored back to back 64 cycles after Timer 0's reset in the vertical blanking, D cycles, one\n");
    printf("GPUSTAT read: the first D with bit 26 set; three words, and four (GP0(00h) ahead). No assertions.\n\n");
    static const int WS[7] = { 1, 2, 3, 4, 6, 8, 16 }, HS[4] = { 16, 32, 64, 128 };
    int ns = 0;
    for (int i = 0; i < 7; i++) for (int j = 0; j < 4; j++) { SW[ns] = WS[i]; SH[ns] = HS[j]; ns++; }
    SW[ns] = 64; SH[ns] = 16; ns++; SW[ns] = 64; SH[ns] = 64; ns++;
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end */
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (239u << 10) | 319u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320x240 */
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int f = 0; f < 2; f++) for (int i = 0; i < ns; i++) {
        g_four = f;
        W[0] = 0x00000000u; W[1] = 0x60808080u; W[2] = xy(0, 0); W[3] = xy(SW[i], SH[i]);
        once(64, 9000); once(64, 9000);   /* the code warm */
        D[f][i] = (uint16_t)search(64, 0, 4001);
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
    printf("W x H : D with three words, with four\n");
    for (int i = 0; i < ns; i++) printf("%3d x %3d : %u %u\n", SW[i], SH[i], (unsigned)D[0][i], (unsigned)D[1][i]);
    printf("\ngpu/gpu-rect-tall: Done.\n");
    for (;;) VSync(0);
}
