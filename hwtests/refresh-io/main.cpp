/*
 * cpu/refresh-io - what a DRAM refresh costs a register read, at every offset from it, with and without a store or a RAM
 * read before it (2026-10-10).
 *
 * WHY (v0.377.73's KNOWN-OPEN, src/psx.js beside IO_WB_OVERLAP). hwtests/spu-read-trace's I_STAT control at k = 12 - lhu
 * I_STAT; lhu T2; 12 nops; sll; or; sw - is 25 cycles a turn with one turn in ten 28 or 34, alternating 253 and 259
 * cycles apart, on all four consoles: every refresh costs that loop 3 or 9 cycles and the loop locks to them. This core
 * never makes an on-die read wait for a refresh and reads 26 or 28 once in 1,280. Two rules reproduce the traces: (a) a
 * refresh that had to wait behind a RAM transaction (a store's drain, a RAM read) holds off the next on-die read until a
 * cycle after its 5, and one begun on an idle bus does not; (b) any refresh in progress holds it. (a) costs line-split and
 * cputest's access-time; (b) locks io-shadow's storeless loops to the refresh, which the consoles do not. Neither is the
 * console's rule. This measures the rule itself: one read, alone or behind a store or a RAM read, timed at every offset
 * from the refresh.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, free-running (16 bits: its count mod 256 keeps the 256-cycle
 * refresh grid's phase). Each trial spins on Timer 2 to a target phase, then runs one fixed sequence between two Timer 2
 * reads and records their difference and the first read's phase. CALIBRATION first: `lhu T2; lw RAM; lhu T2` at every
 * phase - the RAM read waits for a refresh (M164, Sony's bus capture), so the phase where it costs most is the refresh's
 * (F). Then each case at the 72 phases from F - 32 to F + 39 (64 trials a target, each after 0..7 nops, landing a few
 * phases late):
 *   A  lhu T2; 4 nops; lhu I_STAT; 2 nops; lhu T2            an on-die read on an idle bus
 *   B  lhu T2; sw RAM; 3 nops; lhu I_STAT; 2 nops; lhu T2     ...behind a store (its drain)
 *   C  lhu T2; lw RAM; 2 nops; lhu I_STAT; 2 nops; lhu T2     ...behind a RAM read
 *   D  lhu T2; sw RAM; 3 nops; lhu SPUSTAT; 2 nops; lhu T2    a slow read behind a store
 *   E  lhu T2; 4 nops; lw RAM; 2 nops; lhu T2                 a RAM read alone (the calibration's shape, nops around)
 *   F  lhu T2; sw RAM; 3 nops; lw RAM; 2 nops; lhu T2         a RAM read behind a store
 * Each prints its commonest cost, then the cost at each phase from the refresh (the commonest cost of the trials that
 * landed there; '.' where none did), sixteen to a line.
 *
 * READ IT LIKE THIS. A case's row is flat at its commonest cost except where the refresh reaches it. The width and height
 * of that bump, case by case, is the rule: (a) predicts A flat and B / C bumped; (b) predicts A bumped too. The bump's
 * place against E's says where the read meets the refresh against where a RAM read does.
 *
 * ON THIS CORE AT v0.377.71 (2026-10-10): the RAM read's peak 15 against 10 (the refresh first at F, then 14..11);
 * A flat 13; B 14 with 19 18 17 16 15 at F+1..F+5 (the store's drain waited, the read behind it); C 19 with 24..20 at
 * F..F+4; D 31 with 36..32 at F+1..F+5; E 15 with 20..16 at F-2..F+2; F 18 with 23 from F-5 to F+1, then 22..19. Under
 * rule (a) (scratch/_cores/r5): B 16 16 23 23 23 23 19..15 from F, C 23-24 from F-3, D 34..30 at F+6, E 19-20 from F-3.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-io --frames=2000 --quiet=600`.
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
CASE(caseA, NOP NOP NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(caseB, "sw $zero, 4(%1)\n" NOP NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(caseC, "lw $t2, 8(%1)\n" NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(caseD, "sw $zero, 4(%1)\n" NOP NOP NOP "lhu $t2, 0x1dae($t4)\n" NOP NOP)
CASE(caseE, NOP NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(caseF, "sw $zero, 4(%1)\n" NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)

typedef uint32_t (*CaseFn)(volatile uint32_t*);
#define MAXC 48
static uint8_t hist[256][MAXC];     /* [phase][cost] counts, reused by each case */

static void spinTo(uint16_t phase) {
    while ((uint16_t)((read16(T2_COUNT) - phase) & 0xFF) >= 6) {}
}
/* j (0..7) nops, a cycle each from the I-cache: a jump into the run of eight below. It moves where the trial's first
 * Timer 2 read lands by a cycle at a time, which the spin alone cannot (its loop lands on every second or third phase). */
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
    printf("\ncpu/refresh-io\n");
    printf("What a DRAM refresh costs a register read at each phase from it (Timer 2 cycles between the two T2 reads).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(0x1F801DACu, 0x0004);
    write16(0x1F801DAAu, 0xC000);

    /* CALIBRATION: the RAM read at every phase; F = the first phase of its highest cost. */
    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n", base, hi, F);
    int lo = -1, hiEnd = -1;
    for (int k = -16; k < 16; k++) { const int m = modeAt(F + k); if (m > base) { if (lo < 0) lo = k; hiEnd = k; } }
    printf("  above the commonest from F%+d to F%+d:", lo, hiEnd);
    for (int k = -16; k < 16; k++) { const int m = modeAt(F + k); if (m < 0) printf(" ."); else printf(" %d", m); }
    printf("\n\n");

    report("A  on-die read alone          ", caseA, F);
    report("B  on-die read behind a store ", caseB, F);
    report("C  on-die read behind lw RAM  ", caseC, F);
    report("D  SPU read behind a store    ", caseD, F);
    report("E  RAM read alone             ", caseE, F);
    report("F  RAM read behind a store    ", caseF, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
