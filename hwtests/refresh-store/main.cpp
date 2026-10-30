/*
 * cpu/refresh-store - what a DRAM refresh costs a run of RAM stores, at every offset from it: one store, stores to two and
 * four DRAM pages, page bursts of two and four words, a store pair with an on-die read between (2026-10-28 night).
 *
 * WHY (v0.377.211's KNOWN-OPEN beside WRITE_QUEUE_REFRESH_EXTRA). hwtests/cpu-loop-tick8 on the SCPH-9001 and 5501: stores to
 * two DRAM pages back to back read 12.50 a turn, four pages 24.98 - a refresh costs that saturated stream ~10.5 cycles, this
 * core 6.4 (5 + WRITE_QUEUE_REFRESH_EXTRA). Charging a new-row store already queued when the refresh starts 4 more
 * (experiments/_mkrefw211.js) fits those loops and moves counter-bracket, store-shadow and sw-ioread2 off every console:
 * the rule is wider than the console's. A loop's average cannot say where in a run of stores a refresh costs what; this
 * times one run at each phase from the refresh.
 *
 * WHAT IT DOES. As hwtests/refresh-io: interrupts off, Timer 2 at the system clock, free-running (its count mod 256 keeps
 * the 256-cycle refresh grid's phase); each trial spins to a target phase, then runs one sequence between two Timer 2 reads
 * (each case ends with a load of its LAST stored word, which the queue's read priority cannot let past that store - a
 * Timer 2 read would go ahead of the queued stores) and records the cost and the first read's phase.
 * CALIBRATION first (refresh-io's: a RAM read at every phase - F, the refresh's phase, where it costs most). Then each case
 * at the 72 phases from F - 32 to F + 39 (64 trials a target):
 *   A  sw A; lw A                                    one store
 *   B  sw A; sw A+16K; lw A+16K                      two DRAM pages: two first words
 *   C  sw A; sw A+4K; sw A+8K; sw A+12K; lw A+12K    four pages - the queue full, the stream saturated
 *   D  sw A; sw A+4; lw A+4                          a two-word burst
 *   E  sw A; sw A+4; sw A+8; sw A+12; lw A+12        a four-word burst
 *   G  sw A; lhu JOY_STAT; nop; sw A+8; lw A+8       a read between two stores to one page (cpu-loop-tick6's 15)
 *   H  sw A; 5 nops; sw A+16K; lw A+16K              two pages, the second after the first's drain
 *   R  lw A; lw A+16K                                two loads, no store (the loads' own refresh)
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial
 * landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store --frames=2000 --quiet=600`.
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
CASE(caseA, "sw $zero, 0(%1)\n" "lw $t2, 0(%1)\n")
CASE(caseB, "sw $zero, 0(%1)\n" "sw $zero, 0x4000(%1)\n" "lw $t2, 0x4000(%1)\n")
CASE(caseC, "sw $zero, 0(%1)\n" "sw $zero, 0x1000(%1)\n" "sw $zero, 0x2000(%1)\n" "sw $zero, 0x3000(%1)\n" "lw $t2, 0x3000(%1)\n")
CASE(caseD, "sw $zero, 0(%1)\n" "sw $zero, 4(%1)\n" "lw $t2, 4(%1)\n")
CASE(caseE, "sw $zero, 0(%1)\n" "sw $zero, 4(%1)\n" "sw $zero, 8(%1)\n" "sw $zero, 12(%1)\n" "lw $t2, 12(%1)\n")
CASE(caseG, "sw $zero, 0(%1)\n" "lhu $t2, 0x1044($t4)\n" NOP "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseH, "sw $zero, 0(%1)\n" NOP NOP NOP NOP NOP "sw $zero, 0x4000(%1)\n" "lw $t2, 0x4000(%1)\n")
CASE(caseR, "lw $t2, 0(%1)\n" "lw $t2, 0x4000(%1)\n")

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
    printf("\ncpu/refresh-store\n");
    printf("What a DRAM refresh costs a run of RAM stores at each phase from it (Timer 2 cycles between the two T2 reads).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("A  one store                  ", caseA, F);
    report("B  two pages                  ", caseB, F);
    report("C  four pages                 ", caseC, F);
    report("D  a two-word burst           ", caseD, F);
    report("E  a four-word burst          ", caseE, F);
    report("G  sw; JOY_STAT read; sw (page)", caseG, F);
    report("H  two pages, the second later", caseH, F);
    report("R  two loads, no store        ", caseR, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
