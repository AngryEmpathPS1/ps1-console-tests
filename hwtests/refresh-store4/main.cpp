/*
 * cpu/refresh-store4 - a slow read behind a store at more distances, the store in and out of the bracket read's delay slot, and a load right behind a
 * delay-slot store (2026-10-30). refresh-store's harness.
 *
 * WHY. v0.377.220 left two shapes a cycle off. refresh-store2 on the SCPH-5501 #2: sw; k nops; lhu SPUSTAT; 2 nops reads 28 28 28 29 for k = 0 .. 3, this
 * core 29 29 29 29, and refresh-io2's P3 ramp (all seven consoles) 34 33 .. against 35 34 ... The core's trace (experiments/rs2trace.js) says wait + k is
 * max(23, 19 + k) at the read; the console's four numbers fit max(22, 20 + k), a wait a cycle shorter and a floor a cycle higher, or something else: k stops at
 * 3, where the two lines cross. cpu-loop-tick12 / 13's one-store rows (sw; R; ...) are the core's to 0.05 a turn with this wait, and a cycle off every slow read's wait
 * broke them (the race, STATUS "The race read the fourth rule wrong ..."). What differs: their store follows a nop, the P rows' sits in the bracket's Timer 2
 * read's delay slot; and they run around a loop. And refresh-store3: sw; on-die read; sw; lw reads 24, the core 23, with a nop before the lw both 23: a nop
 * makes it FASTER on the console.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39:
 *   N0 N3       k nops; lhu SPUSTAT; 2 nops               no store: the read alone, k = 0, 3
 *   P4 P5 P6 P8 sw A; k nops; lhu SPUSTAT; 2 nops         where the P rows' line goes past k = 3
 *   Q0 .. Q3    nop; sw A; k nops; lhu SPUSTAT; 2 nops    the store out of the bracket's delay slot
 *   C0 .. C3    sw A; k nops; lbu CD status; 2 nops       the CD's register, the other slow read
 *   T4 T5       sw A; lhu T2; sw A+8; 2 / 3 nops; lw A+8  refresh-store3's T1 (0 nops, 24) and T3 (1 nop, 23) further on
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store4 --frames=3000 --quiet=600`.
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
CASE(caseN0, "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseN3, "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP4, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP5, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP6, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP8, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseQ0, "nop\n" "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseQ1, "nop\n" "sw $zero, 0(%1)\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseQ2, "nop\n" "sw $zero, 0(%1)\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseQ3, "nop\n" "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseC0, "sw $zero, 0(%1)\n" "lbu $t2, 0x1800($t4)\n" "nop\n" "nop\n")
CASE(caseC1, "sw $zero, 0(%1)\n" "nop\n" "lbu $t2, 0x1800($t4)\n" "nop\n" "nop\n")
CASE(caseC2, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "lbu $t2, 0x1800($t4)\n" "nop\n" "nop\n")
CASE(caseC3, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "lbu $t2, 0x1800($t4)\n" "nop\n" "nop\n")
CASE(caseT4, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseT5, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t2, 8(%1)\n")

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
    printf("\ncpu/refresh-store4\n");
    printf("A slow read behind a store at more distances, and a load behind a delay-slot store, at each phase from a refresh (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("N0 lhu SPUSTAT                ", caseN0, F);
    report("N3 3 nops; lhu SPUSTAT        ", caseN3, F);
    report("P4 sw; 4 nops; lhu SPUSTAT    ", caseP4, F);
    report("P5 sw; 5 nops; lhu SPUSTAT    ", caseP5, F);
    report("P6 sw; 6 nops; lhu SPUSTAT    ", caseP6, F);
    report("P8 sw; 8 nops; lhu SPUSTAT    ", caseP8, F);
    report("Q0 nop; sw; 0 nops; SPUSTAT   ", caseQ0, F);
    report("Q1 nop; sw; 1 nops; SPUSTAT   ", caseQ1, F);
    report("Q2 nop; sw; 2 nops; SPUSTAT   ", caseQ2, F);
    report("Q3 nop; sw; 3 nops; SPUSTAT   ", caseQ3, F);
    report("C0 sw; 0 nops; lbu CD         ", caseC0, F);
    report("C1 sw; 1 nops; lbu CD         ", caseC1, F);
    report("C2 sw; 2 nops; lbu CD         ", caseC2, F);
    report("C3 sw; 3 nops; lbu CD         ", caseC3, F);
    report("T4 sw; lhu T2; sw; 2 nops     ", caseT4, F);
    report("T5 sw; lhu T2; sw; 3 nops     ", caseT5, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
