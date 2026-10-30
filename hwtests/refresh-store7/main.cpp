/*
 * cpu/refresh-store7 - a load behind a delay-slot store: is the cycle the REGISTER? (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store6 on the SCPH-5501 #2 (three runs alike): sw A; T; sw A+8; [nop;] lw reads 24 / 23 wherever the load goes, the word stored or another page, so it is not the address (a second
 * opinion's guess). refresh-io2's R rows, sw; [k nops;] lw, read 18 18 18 18 with a Timer 2 read in the same place: no such step. What differs: every row of T1's family,
 * from refresh-store3 on, reads Timer 2 into t2 and loads into t2 too, a load at position 2 writing the on-die load's register. refresh-io2's bracket read goes to t1. That was an accident of the
 * harness. v0.377.210 found a writer at position 1 or 2 ends an on-die load's shadow; nothing asks whether a LOAD is such a writer.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39. T = lhu Timer 2 into t2.
 *   E0 / E1   sw A; T; sw A+8; [nop;] lw t2, A+8      T1 and T3, the control
 *   F0 / F1   the same, lw t5                         the load another register
 *   G0 / G1   sw A; T; sw A+8; [nop;] addu t2, zero, zero; nop; lw t5, A+8     an ALU writer of T's register in that place
 *   H0 / H1   sw A; T; nop; [nop;] lw t2, A           no second store, t2
 *   J0 / J1   the same, lw t5
 *   K0 / K1   T; sw A+8; [nop;] lw t5                 refresh-store6's B rows, another register
 *   S0 / S1   sw A; T; sw SP+8; [nop;] lw t5, SP+8    its S rows, another register
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store7 --frames=3000 --quiet=600`.
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
                     : "=r"(r) : "r"(p) : "t1", "t2", "t3", "t4", "t5", "memory");     \
        return r;                                                                \
    }

#define NOP "nop\n"
CASE(calib, "lw $t2, 0(%1)\n")
CASE(caseE0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseE1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseF0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t5, 8(%1)\n")
CASE(caseF1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t5, 8(%1)\n")
CASE(caseG0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "addu $t2, $zero, $zero\n" "nop\n" "lw $t5, 8(%1)\n")
CASE(caseG1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "addu $t2, $zero, $zero\n" "nop\n" "lw $t5, 8(%1)\n")
CASE(caseH0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "nop\n" "lw $t2, 0(%1)\n")
CASE(caseH1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "nop\n" "nop\n" "lw $t2, 0(%1)\n")
CASE(caseJ0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "nop\n" "lw $t5, 0(%1)\n")
CASE(caseJ1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "nop\n" "nop\n" "lw $t5, 0(%1)\n")
CASE(caseK0, "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t5, 8(%1)\n")
CASE(caseK1, "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t5, 8(%1)\n")
CASE(caseS0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8($t4)\n" "lw $t5, 8($t4)\n")
CASE(caseS1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8($t4)\n" "nop\n" "lw $t5, 8($t4)\n")

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
    printf("\ncpu/refresh-store7\n");
    printf("A load behind an on-die read, the same register or another, at each phase from a refresh (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("E0 lw the same reg            ", caseE0, F);
    report("E1 lw the same reg (nop)      ", caseE1, F);
    report("F0 lw another reg             ", caseF0, F);
    report("F1 lw another reg (nop)       ", caseF1, F);
    report("G0 ALU writer of T            ", caseG0, F);
    report("G1 ALU writer of T (nop)      ", caseG1, F);
    report("H0 no store, same reg         ", caseH0, F);
    report("H1 no store, same reg (nop)   ", caseH1, F);
    report("J0 no store, other            ", caseJ0, F);
    report("J1 no store, other (nop)      ", caseJ1, F);
    report("K0 no first store, other      ", caseK0, F);
    report("K1 no first store, other (nop)", caseK1, F);
    report("S0 scratchpad, other          ", caseS0, F);
    report("S1 scratchpad, other (nop)    ", caseS1, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
