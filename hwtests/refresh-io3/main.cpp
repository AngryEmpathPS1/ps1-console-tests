/*
 * cpu/refresh-io3 - what a DRAM refresh costs a read of RAM_SIZE (1F801060h, the DRAM controller's own register), at every
 * phase from it, alone and behind a store or a RAM read (2026-10-11 night).
 *
 * WHY. hwtests/at-split on the SCPH-9001 (access-time's windows apart, at the runner's landing): RAM_SIZE's read loops run
 * 8-13 cycles longer than this core's - three refreshes' worth in a 720-cycle loop - and src/psx.js makes a RAM_SIZE read
 * wait for the RAM bus (M230, nocash's DIAG: it waits as a RAM read does) but never for a refresh. Charged like a RAM read
 * (scratch core v116) at-split's rows come within a few cycles, but access-time's own RAM_SIZE rows overshoot by 4-8. This
 * measures the read against the refresh directly, as refresh-io does for I_STAT and RAM.
 *
 * WHAT IT DOES. refresh-io's harness and calibration (`lhu T2; lw RAM; lhu T2` at every phase, F = the first phase of the
 * highest cost), then each case at the 72 phases from F - 32 to F + 39:
 *   A  lhu T2; 4 nops; lw RAM_SIZE; 2 nops; lhu T2            RAM_SIZE alone
 *   B  lhu T2; sw RAM; 3 nops; lw RAM_SIZE; 2 nops; lhu T2     ...behind a store
 *   C  lhu T2; lw RAM; 2 nops; lw RAM_SIZE; 2 nops; lhu T2     ...behind a RAM read
 *   D  lhu T2; 4 nops; lw RAM_SIZE; lw RAM_SIZE; 1 nop; lhu T2 two back to back
 *   E  lhu T2; 4 nops; lw I_STAT; 2 nops; lhu T2               the control (refresh-io's A: never meets a refresh)
 *   F  lhu T2; 4 nops; lw RAM; 2 nops; lhu T2                  a RAM read alone (refresh-io's E)
 *
 * READ IT LIKE THIS. Flat rows never meet the refresh; a ramp like F's (6 5 4 3 2 1 over the refresh) is a RAM read's.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-io3 --frames=2000 --quiet=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramWord[16];

/* Each case: Timer 2 before (t1) and after (t3); returns (after - before) | before << 16. */
#define CASE(NAME, BODY)                                                         \
    static uint32_t __attribute__((noinline)) NAME(volatile uint32_t* ram) {      \
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
                     : "=r"(r) : "r"(ram) : "t1", "t2", "t3", "t4", "memory");   \
        return r;                                                                \
    }

#define NOP "nop\n"
CASE(calib, "lw $t2, 0(%1)\n")
CASE(caseA, NOP NOP NOP NOP "lw $t2, 0x1060($t4)\n" NOP NOP)
CASE(caseB, "sw $zero, 4(%1)\n" NOP NOP NOP "lw $t2, 0x1060($t4)\n" NOP NOP)
CASE(caseC, "lw $t2, 8(%1)\n" NOP NOP "lw $t3, 0x1060($t4)\n" NOP NOP)
CASE(caseD, NOP NOP NOP NOP "lw $t2, 0x1060($t4)\n" "lw $t3, 0x1060($t4)\n" NOP)
CASE(caseE, NOP NOP NOP NOP "lw $t2, 0x1070($t4)\n" NOP NOP)
CASE(caseF, NOP NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)

typedef uint32_t (*CaseFn)(volatile uint32_t*);
#define MAXC 48
static uint8_t hist[256][MAXC];     /* [phase][cost] counts, reused by each case */

static void spinTo(uint16_t phase) {
    while ((uint16_t)((read16(T2_COUNT) - phase) & 0xFF) >= 6) {}
}
/* j (0..7) nops, a cycle each from the I-cache: a jump into the run of eight below (refresh-io's own). */
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

/* Runs fn `reps` times at each of the targets lo..lo+n-1 (phases mod 256); fills hist by the first read's phase. */
static void sweep(CaseFn fn, int lo, int n, int reps) {
    for (int p = 0; p < 256; p++) for (int c = 0; c < MAXC; c++) hist[p][c] = 0;
    fn(ramWord); fn(ramWord); sled(0); sled(7);                 /* the code into the I-cache */
    for (int r = 0; r < reps; r++)
        for (int k = 0; k < n; k++) {
            spinTo((uint16_t)((lo + k) & 0xFF));
            sled((uint32_t)(r & 7));
            const uint32_t v = fn(ramWord);
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
    printf("\ncpu/refresh-io3\n");
    printf("What a DRAM refresh costs a read of RAM_SIZE at each phase from it (Timer 2 cycles between the two T2 reads).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    /* CALIBRATION: the RAM read at every phase; F = the first phase of its highest cost. */
    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n", base, hi, F);
    printf("\n");

    report("A  RAM_SIZE alone              ", caseA, F);
    report("B  RAM_SIZE behind a store     ", caseB, F);
    report("C  RAM_SIZE behind lw RAM      ", caseC, F);
    report("D  RAM_SIZE twice              ", caseD, F);
    report("E  I_STAT alone (control)      ", caseE, F);
    report("F  RAM read alone              ", caseF, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
