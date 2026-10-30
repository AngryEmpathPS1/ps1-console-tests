/*
 * cpu/refresh-store2 - what a DRAM refresh does to two stores to one page with a gap between them, and to a slow read behind a
 * store, at every offset from it (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store, refresh-io2 and sw-ioread2 on the consoles (STATUS "sw-ioread2 row by row ...", v0.377.220's
 * entry). sw-ioread2's D rows, sw; nop; nop; sw, sit on the edge WRITE_QUEUE_CONT_LEAD was fitted to: at rest the second
 * store issues 3 cycles before the first word's end and starts a word of its own. This core lets a refresh that delays the
 * first word push the second inside the lead, and it continues the burst: 24 / 25 / 26 at 248 of 58 x 256 row-shifts
 * where every console's sixteen trials read 27 (experiments/rowshift.js). Those sixteen sample a few phases of 256, so that
 * cannot say whether the consoles do it too: refresh-store times back-to-back stores (its D) and two pages (its H), not this
 * shape. And refresh-io2's P3 (sw; 3 nops; lhu SPUSTAT) reads the store ramp a cycle lower than this core, the one row
 * of that program left off.
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39:
 *   N1  sw A; nop; sw A+4; lw A+4                        a burst with a gap of one
 *   N2  sw A; 2 nops; sw A+4; lw A+4                     sw-ioread2's D shape: no burst at rest
 *   N3  sw A; 3 nops; sw A+4; lw A+4                     one further
 *   J1  sw A; nop; sw A+4; lbu JOY_STAT; nop; its reader; lw A+4
 *   J2  sw A; 2 nops; sw A+4; lbu JOY_STAT; nop; its reader; lw A+4      sw-ioread2's D0r1, ended by the load
 *   K2  sw A; 2 nops; sw A+4; lbu JOY_STAT; 3 nops; its reader; lw A+4   its D0r3
 *   P0 .. P3  sw A; k nops; lhu SPUSTAT; 2 nops          refresh-io2's P3 at k = 0 .. 3
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial
 * landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store2 --frames=2000 --quiet=600`.
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
CASE(caseN1, "sw $zero, 0(%1)\n" "nop\n" "sw $zero, 4(%1)\n" "lw $t2, 4(%1)\n")
CASE(caseN2, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 4(%1)\n" "lw $t2, 4(%1)\n")
CASE(caseN3, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "sw $zero, 4(%1)\n" "lw $t2, 4(%1)\n")
CASE(caseJ1, "sw $zero, 0(%1)\n" "nop\n" "sw $zero, 4(%1)\n" "lbu $t2, 0x1044($t4)\n" "nop\n" "addu $t2, $t2, $zero\n" "lw $t2, 4(%1)\n")
CASE(caseJ2, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 4(%1)\n" "lbu $t2, 0x1044($t4)\n" "nop\n" "addu $t2, $t2, $zero\n" "lw $t2, 4(%1)\n")
CASE(caseK2, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 4(%1)\n" "lbu $t2, 0x1044($t4)\n" "nop\n" "nop\n" "nop\n" "addu $t2, $t2, $zero\n" "lw $t2, 4(%1)\n")
CASE(caseP0, "sw $zero, 4(%1)\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP1, "sw $zero, 4(%1)\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP2, "sw $zero, 4(%1)\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")
CASE(caseP3, "sw $zero, 4(%1)\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1dae($t4)\n" "nop\n" "nop\n")

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
    printf("\ncpu/refresh-store2\n");
    printf("What a DRAM refresh costs two stores with a gap, and a slow read behind a store, at each phase from it (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("N1 sw; nop; sw (page)         ", caseN1, F);
    report("N2 sw; 2 nops; sw (page)      ", caseN2, F);
    report("N3 sw; 3 nops; sw (page)      ", caseN3, F);
    report("J1 sw; nop; sw; JOY read r1   ", caseJ1, F);
    report("J2 sw; 2 nops; sw; JOY read r1", caseJ2, F);
    report("K2 sw; 2 nops; sw; JOY read r3", caseK2, F);
    report("P0 sw; lhu SPUSTAT            ", caseP0, F);
    report("P1 sw; nop; lhu SPUSTAT       ", caseP1, F);
    report("P2 sw; 2 nops; lhu SPUSTAT    ", caseP2, F);
    report("P3 sw; 3 nops; lhu SPUSTAT    ", caseP3, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
