/*
 * cpu/refresh-io4 - where a RAM read N instructions after an on-die read meets the refresh: lhu T2; N nops; lw RAM, N = 0..6,
 * at every phase from a refresh (2026-10-11 night).
 *
 * WHY. refresh-io (E) and refresh-io3 (F) on the consoles: a RAM read four nops after the Timer 2 read starts its 6-step
 * refresh ramp at F-3, where this core starts it at F-2 - F being where the calibration's read, right after the Timer 2
 * read, meets it. So between "lhu T2; lw" and "lhu T2; 4 nops; lw" this core places the read a cycle differently from the
 * consoles: the on-die load's shadow (positions 3..4 refunded, M176), M209's back-to-back overlap for the read at position 1,
 * or the Timer 2 sample inside the lhu. Loops lock to the refresh where their reads meet it, so the cycle sets lock points
 * (cpu/access-time's rows). One ramp per N says where each read asks for the bus.
 *
 * WHAT IT DOES. refresh-io's harness and calibration, then for N = 0..6: lhu T2; N nops; lw RAM; 2 nops; lhu T2, at the 72
 * phases from F - 32 to F + 39. N = 0 is the calibration's own shape with 2 nops after the read.
 *
 * READ IT LIKE THIS. Each row's ramp (+6 .. +1) starts where its read's request meets the refresh: one phase earlier per
 * cycle the read asks later. Its start against N is the read's issue cycle after the Timer 2 read.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-io4 --frames=2000 --quiet=600`.
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
CASE(caseA, "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseB, NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseC, NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseD, NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseE, NOP NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseF, NOP NOP NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseG, NOP NOP NOP NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)

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
    printf("\ncpu/refresh-io4\n");
    printf("Where a RAM read N nops after a Timer 2 read meets the refresh (Timer 2 cycles between the two T2 reads).\n");
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

    report("N=0  lhu T2; 0 nops; lw RAM   ", caseA, F);
    report("N=1  lhu T2; 1 nops; lw RAM   ", caseB, F);
    report("N=2  lhu T2; 2 nops; lw RAM   ", caseC, F);
    report("N=3  lhu T2; 3 nops; lw RAM   ", caseD, F);
    report("N=4  lhu T2; 4 nops; lw RAM   ", caseE, F);
    report("N=5  lhu T2; 5 nops; lw RAM   ", caseF, F);
    report("N=6  lhu T2; 6 nops; lw RAM   ", caseG, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
