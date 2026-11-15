/*
 * gpu-spr-lines: A SPRITE'S EXACT TIME BY ITS ROWS, across one, two, three and four display lines - the display range
 * EMPTY and the display on at 320x240.
 *
 * WHY. hwtests/gpu-tex-noscan (the second SCPH-5501, polled): a 256x256 sprite reads +0.58% (4-bit) / +0.63% (8-bit) on
 * v0.377.348 with the display range EMPTY as with it on at 320. hwtests/gpu-spr-hold timed sprites under a line long at
 * 256x240, where this core charges a first fetch and refills besides the line hold: its numbers mix them, and its
 * blanking column showed the core charging the hold in the blanking too (13 ticks at each line start, scanPos's hold is
 * not gated by the blanking). What a filling sprite pays at a line start with NO scan-out, and what a row costs bare,
 * are both in the 256x256's 781 cycles and neither is measured apart.
 *
 * WHAT. One family at a time, the same sprite at growing heights N, its four words stored back to back 64 cycles after
 * Timer 0's reset, bit 26's exact D. D against N is a straight line (the row's cost) with a step wherever the drawing
 * has grown past another line start (every 2,152.5 cycles from the start): the step is what that line start costs.
 * Families: 8-bit 256 wide (N 1 .. 20), 8-bit 128 wide (2 .. 40 by 2), 4-bit 256 wide (2 .. 30 by 2), 4-bit 64 wide
 * cached (4 .. 64 by 4: the 4-bit cache holds 64 lines), flat 256 wide (4 .. 64 by 4). Twice: the vertical display range empty (E), then the display
 * on at 320x240 with the drawing started ~93 lines after VBlank (D320). No assertions.
 *
 * READ IT. tools: experiments/audit/sprlinesfit.js fits D = k + r N + h (line starts crossed) per family.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-lines --emu`.
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
struct Fam { const char* name; int depth, wd, n0, dn, cnt; };
static const Fam F[5] = {
    { "8b 256", 1, 256, 1, 1, 20 },
    { "8b 128", 1, 128, 2, 2, 20 },
    { "4b 256", 0, 256, 2, 2, 15 },
    { "4b 64 cached", 0, 64, 4, 4, 16 },
    { "flat 256", 3, 256, 4, 4, 16 },
};
static uint32_t W[4];
static void shape(const Fam& f, int ht) {
    if (f.depth == 3) { W[0] = 0x00000000u; W[1] = 0x60808080u; W[2] = xy(32, 0); W[3] = xy(f.wd, ht); }
    else { W[0] = 0x65808080u; W[1] = xy(32, 0); W[2] = 0x3c000000u; W[3] = xy(f.wd, ht); }
}
static uint32_t g_off = 20000u;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t once(uint32_t s, uint32_t d) {
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

static uint32_t D[2][5][20];
int main() {
    printf("\ngpu/gpu-spr-lines\n");
    printf("A sprite's four words stored back to back 64 cycles after Timer 0's reset, D cycles, one GPUSTAT read: the first D\n");
    printf("with bit 26 set, by the sprite's rows. No assertions.\n\n");
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
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (255u << 10) | 319u); write32(GP0, 0xE5000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320x240 */
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    for (int m = 0; m < 2; m++) {
        if (m == 0) { write32(GP1, 0x07000000u | (16u << 10) | 16u); g_off = 20000u; }           /* E: the vertical range empty */
        else { write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u); g_off = 199000u; }        /* D320: on, ~93 lines after VBlank */
        for (int f = 0; f < 5; f++) {
            waitReady(); write32(GP0, 0xE1000000u | 12u | ((uint32_t)(F[f].depth & 3) << 7));
            uint32_t guess = 0;
            for (int i = 0; i < F[f].cnt; i++) {
                const int ht = F[f].n0 + F[f].dn * i;
                shape(F[f], ht);
                once(64, 30000); once(64, 30000);   /* the code and (the cached family) the texels warm */
                uint32_t lo = 0, hi = 40001;
                if (i >= 2) { const uint32_t step = D[m][f][i - 1] - D[m][f][i - 2]; guess = D[m][f][i - 1] + step; lo = guess > 200 ? guess - 200 : 0; hi = guess + 200; }
                uint32_t d = search(64, lo, hi);
                if (d == lo || d >= hi) d = search(64, 0, 40001);   /* outside the guess: the whole range */
                D[m][f][i] = d;
            }
        }
    }
    waitReady();
    write32(GP0, 0xE1000400u); write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u);
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 0)); write32(GP0, xy(1023, 511));
    waitReady();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int m = 0; m < 2; m++) {
        printf(m ? "D320 (display on, 320x240, ~93 lines after VBlank):\n" : "E (vertical range empty):\n");
        for (int f = 0; f < 5; f++) {
            printf("%-13s rows %d + %d i :", F[f].name, F[f].n0, F[f].dn);
            for (int i = 0; i < F[f].cnt; i++) printf(" %u", (unsigned)D[m][f][i]);
            printf("\n");
        }
    }
    printf("\ngpu/gpu-spr-lines: Done.\n");
    for (;;) VSync(0);
}
