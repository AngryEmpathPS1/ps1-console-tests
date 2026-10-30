/*
 * cpu/refresh-store5 - what follows a slow read that waited behind a store: a bus access at each distance, a RAM load, another slow read, a read that went ahead of a
 * second store (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store4 (the SCPH-5501 #2) and refresh-store2: sw; k nops; R; 2 nops; lhu T2 read a cycle less than the core at k = 0..2, where the read waits, for
 * SPUSTAT and the CD alike, in or out of a delay slot, and exactly the core at k >= 3. experiments/_mkwrb220.js gives the load behind such a read the cycle a writer of its
 * register in the delay slot already had (slowEarly), and reads both programs exact at every phase, and refresh-io2 on all seven consoles. But the only follower those
 * programs hold is Timer 2 three on. Unmeasured: the same at one, two, four and more on; a RAM load there; a second slow read; and a read that WENT AHEAD of a store
 * behind it, where the writer takes 2 (SLOW_JUMP), not 1.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39. R = lhu SPUSTAT. The bracket's closing Timer 2 read is the follower
 * of the first rows, each with its control:
 *   Z0 Z1 Z3 Z4 Z6     R; k nops                       no store
 *   A0 A1 A3 A4 A6     sw A; R; k nops                 the closing read at k + 1 on (A2 is refresh-store2's P0)
 *   B0 B2              sw A; R; k nops; lw A+8         a RAM load one and three on, then the closing read
 *   D2                 sw A; R; 2 nops; R              a second slow read
 *   S2                 sw A; sw A+16K; R; 2 nops       two pages: the read goes ahead of the second store
 *   S0                 sw A; sw A+16K; R
 *   V2                 sw A; R; nop; addu t2, t2; nop  a reader of the value two on, the closing read behind it
 *   W2                 sw A; R; andi t2, t2; 2 nops    a writer in the delay slot, the closing read three on
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store5 --frames=3000 --quiet=600`.
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
CASE(caseZ0, "lhu $t2, 0x1dae($t4)\n")
CASE(caseZ1, "lhu $t2, 0x1dae($t4)\n" "nop\n")
CASE(caseZ3, "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "nop\n")
CASE(caseZ4, "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "nop\n" "nop\n")
CASE(caseZ6, "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n")
CASE(caseA0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n")
CASE(caseA1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n")
CASE(caseA3, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "nop\n")
CASE(caseA4, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "nop\n" "nop\n")
CASE(caseA6, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n")
CASE(caseB0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "lw $t2, 8(%1)\n")
CASE(caseB2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseD2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n")
CASE(caseS2, "sw $zero, 0(%1)\n" "sw $zero, 0x4000(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseS0, "sw $zero, 0(%1)\n" "sw $zero, 0x4000(%1)\n" "lhu $t2, 0x1dae($t4)\n")
CASE(caseV2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "addu $t2, $t2, $zero\n" "nop\n")
CASE(caseW2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1dae($t4)\n" "andi $t2, $t2, 0xffff\n" "nop\n" "nop\n")

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
    printf("\ncpu/refresh-store5\n");
    printf("What follows a slow read that waited behind a store, at each phase from a refresh (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("Z0 R; 0 nops                  ", caseZ0, F);
    report("Z1 R; 1 nops                  ", caseZ1, F);
    report("Z3 R; 3 nops                  ", caseZ3, F);
    report("Z4 R; 4 nops                  ", caseZ4, F);
    report("Z6 R; 6 nops                  ", caseZ6, F);
    report("A0 sw; R; 0 nops              ", caseA0, F);
    report("A1 sw; R; 1 nops              ", caseA1, F);
    report("A3 sw; R; 3 nops              ", caseA3, F);
    report("A4 sw; R; 4 nops              ", caseA4, F);
    report("A6 sw; R; 6 nops              ", caseA6, F);
    report("B0 sw; R; lw                  ", caseB0, F);
    report("B2 sw; R; 2 nops; lw          ", caseB2, F);
    report("D2 sw; R; 2 nops; R           ", caseD2, F);
    report("S2 sw; sw far; R; 2 nops      ", caseS2, F);
    report("S0 sw; sw far; R              ", caseS0, F);
    report("V2 sw; R; nop; reader; nop    ", caseV2, F);
    report("W2 sw; R; writer; 2 nops      ", caseW2, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
