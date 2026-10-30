/*
 * cpu/refresh-store3 - an on-die read behind a store at every offset from a DRAM refresh, Timer 2 against I_STAT, with and without a store
 * in the read's delay slot (2026-10-30). refresh-store's harness.
 *
 * WHY. A refresh due a cycle into a store costs what is behind it 6 (refresh-io2's S rows: sw; k nops; lhu I_STAT reads 19 there, on all
 * seven consoles). Charged that way, hwtests/cpu-loop-tick10's DS-1 (sw A+4; lhu T2; sw A+12; nop) reads 13.31 where the consoles and the
 * core without the rule read 13.19, 0.1 a turn, and DS-SAMEW and DS-SH with it: what differs from the S rows? The read is Timer 2, not
 * I_STAT, nothing reads its value, and a store sits in its delay slot. refresh-store's header says a Timer 2 read goes ahead of queued
 * stores, and no program times this shape against the refresh.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39, the last line a load of the last
 * stored word so nothing passes the queue unseen:
 *   T0  sw A; lhu T2; lw A                    Timer 2 behind a store
 *   I0  sw A; lhu I_STAT; lw A                the same, I_STAT
 *   T1  sw A; lhu T2; sw A+8; lw A+8          DS-1's shape, the store in the read's delay slot
 *   I1  sw A; lhu I_STAT; sw A+8; lw A+8      the same, I_STAT
 *   T2  sw A; lhu T2; nop; sw A+8; lw A+8     the store out of the delay slot
 *   I2  sw A; lhu I_STAT; nop; sw A+8; lw A+8
 *   T3  sw A; lhu T2; sw A+8; nop; lw A+8     DS-1 itself, its nop
 *   U1  sw A; lhu T2; addu t2, t2, zero; sw A+8; lw A+8    a reader in the store's place
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store3 --frames=2000 --quiet=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ram[0x1010] __attribute__((aligned(4096)));   /* 16 KB + : A+16K another page, A+4K .. 12K others */

/* Each case: Timer 2 before (t1) and after (t3); returns (after - before) | before << 16. */
#define CASE(NAME, BODY)                                                         \
    static uint32_t __attribute__((noinline)) NAME(volatile uint32_t* p) {       \
        uint32_t r;                                                              \
        asm volatile(".set push\n.set noreorder\n.set noat\n"                    \
                     "lui   $t4, 0x1f80\n"                                       \
                     "lhu   $t1, 0x1120($t4)\n"                                  \
                     BODY                                                        \
                     "lhu   $t3, 0x1120($t4)\n"                                  \
                     "nop\n"                                                     \
                     "subu  $t3, $t3, $t1\n"                                     \
                     "andi  $t3, $t3, 0xffff\n"                                  \
                     "sll   $t1, $t1, 16\n"                                      \
                     "or    %0, $t3, $t1\n"                                      \
                     ".set pop\n"                                                \
                     : "=r"(r) : "r"(p) : "t1", "t2", "t3", "t4", "memory");     \
        return r;                                                                \
    }

#define NOP "nop\n"
CASE(calib, "lw $t2, 0(%1)\n")
CASE(caseT0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "lw $t2, 0(%1)\n")
CASE(caseI0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1070($t4)\n" "lw $t2, 0(%1)\n")
CASE(caseT1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseI1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1070($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseT2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "nop\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseI2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1070($t4)\n" "nop\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseT3, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseU1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "addu $t2, $t2, $zero\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")

typedef uint32_t (*CaseFn)(volatile uint32_t*);
#define MAXC 64
static uint8_t hist[256][MAXC];     /* [phase][cost] counts, reused by each case */

static void spinTo(uint16_t phase) {
    while ((uint16_t)((read16(T2_COUNT) - phase) & 0xFF) >= 6) {}
}
/* j (0..7) nops, a cycle each from the I-cache: moves where the trial's first Timer 2 read lands a cycle at a time. */
static void __attribute__((noinline)) sled(uint32_t j) {
    asm volatile(".set push\n.set noreorder\n"
                 "la    $t0, 1f\n"
                 "sll   $t1, %0, 2\n"
                 "subu  $t0, $t0, $t1\n"
                 "jr    $t0\n"
                 "nop\n"
                 "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
                 "1:\n"
                 ".set pop\n" : : "r"(j) : "t0", "t1");
}

static void sweep(CaseFn fn, int lo, int n, int reps) {
    for (int p = 0; p < 256; p++) for (int c = 0; c < MAXC; c++) hist[p][c] = 0;
    fn(ram); fn(ram); sled(0); sled(7);                          /* the code into the I-cache */
    for (int r = 0; r < reps; r++)
        for (int k = 0; k < n; k++) {
            spinTo((uint16_t)((lo + k) & 0xFF));
            sled((uint32_t)(r & 7));
            const uint32_t v = fn(ram);
            const int cost = v & 0xFFFF, ph = (v >> 16) & 0xFF;
            if (cost < MAXC && hist[ph][cost] < 255) hist[ph][cost]++;
            for (volatile int d = 0; d < 4; d++) {}             /* the bookkeeping's stores drain */
        }
}
static int modeAt(int ph) {
    int best = -1, bn = 0;
    for (int c = 0; c < MAXC; c++) if (hist[ph & 0xFF][c] > bn) { bn = hist[ph & 0xFF][c]; best = c; }
    return best;
}
static int modeAll(int lo, int n) {
    static uint32_t tot[MAXC];
    for (int c = 0; c < MAXC; c++) tot[c] = 0;
    for (int k = 0; k < n; k++) for (int c = 0; c < MAXC; c++) tot[c] += hist[(lo + k) & 0xFF][c];
    int best = 0;
    for (int c = 1; c < MAXC; c++) if (tot[c] > tot[best]) best = c;
    return best;
}

static void report(const char* name, CaseFn fn, int F) {
    sweep(fn, F - 48, 88, 64);
    printf("%s  commonest %d; cost at phase F-32 .. F+39:\n", name, modeAll(F - 32, 72));
    for (int k = 0; k < 72; k++) {
        const int m = modeAt(F - 32 + k);
        if (m < 0) printf("  ."); else printf(" %2d", m);
        if (k % 16 == 15 || k == 71) printf("\n");
    }
}

int main() {
    printf("\ncpu/refresh-store3\n");
    printf("What a DRAM refresh costs an on-die read behind a store, Timer 2 and I_STAT, at each phase from it (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("T0 sw; lhu T2                 ", caseT0, F);
    report("I0 sw; lhu I_STAT             ", caseI0, F);
    report("T1 sw; lhu T2; sw             ", caseT1, F);
    report("I1 sw; lhu I_STAT; sw         ", caseI1, F);
    report("T2 sw; lhu T2; nop; sw        ", caseT2, F);
    report("I2 sw; lhu I_STAT; nop; sw    ", caseI2, F);
    report("T3 sw; lhu T2; sw; nop        ", caseT3, F);
    report("U1 sw; lhu T2; reader; sw     ", caseU1, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
