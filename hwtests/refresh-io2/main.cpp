/*
 * cpu/refresh-io2 - refresh-io's store cases taken apart: a store, k nops, then the read, at every phase from a DRAM
 * refresh (2026-10-10).
 *
 * WHY. hwtests/refresh-io on the SCPH-7501 (three runs identical): an on-die read alone never meets a refresh; a RAM
 * read waits up to 6 cycles (M164's "+1" after its 5); an I_STAT read 3 nops behind a store meets it once with +9 (the
 * phase before the RAM read's peak) and then on the store's own 5-step ramp; a RAM read behind a store +10 there and
 * +5/+6 for five phases before. Where the +9 comes from - the store's drain and the refresh asking for the bus at once,
 * the read losing its overlap with the drain - depends on how far behind the store the read is. Same method as
 * refresh-io (calibration F from a RAM read's peak, then each case's cost at the 32 phases from F - 16 to F + 15):
 *   S k  sw; k nops; lhu I_STAT (k = 0, 1, 2, 3, 5, 8)      R k  sw; k nops; lw RAM (k = 0, 1, 3, 5)
 *   W2   sw; sw; 3 nops; lhu I_STAT (two words queued)      P3   sw; 3 nops; lhu SPUSTAT (the slow read)
 * READ IT LIKE THIS. The phase of the spike against k says when the read asks; its height, what it waits for.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-io2 --frames=2500 --quiet=600`.
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
CASE(s0, "sw $zero, 4(%1)\n" "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(s1, "sw $zero, 4(%1)\n" NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(s2, "sw $zero, 4(%1)\n" NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(s3, "sw $zero, 4(%1)\n" NOP NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(s5, "sw $zero, 4(%1)\n" NOP NOP NOP NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(s8, "sw $zero, 4(%1)\n" NOP NOP NOP NOP NOP NOP NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(r0, "sw $zero, 4(%1)\n" "lw $t2, 8(%1)\n" NOP NOP)
CASE(r1, "sw $zero, 4(%1)\n" NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(r3, "sw $zero, 4(%1)\n" NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(r5, "sw $zero, 4(%1)\n" NOP NOP NOP NOP NOP "lw $t2, 8(%1)\n" NOP NOP)
CASE(w2, "sw $zero, 4(%1)\n" "sw $zero, 12(%1)\n" NOP NOP NOP "lhu $t2, 0x1070($t4)\n" NOP NOP)
CASE(p3, "sw $zero, 4(%1)\n" NOP NOP NOP "lhu $t2, 0x1dae($t4)\n" NOP NOP)

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
    sweep(fn, F - 48, 72, 64);
    printf("%s  commonest %d; cost at phase F-16 .. F+15:\n", name, modeAll(F - 16, 32));
    for (int k = 0; k < 32; k++) {
        const int m = modeAt(F - 16 + k);
        if (m < 0) printf("  ."); else printf(" %2d", m);
        if (k % 16 == 15) printf("\n");
    }
}

int main() {
    printf("\ncpu/refresh-io2\n");
    printf("refresh-io's store cases taken apart: a store, k nops, then a read, at each phase from a refresh.\n");
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

    report("S0 sw; lhu I_STAT             ", s0, F);
    report("S1 sw; nop; lhu I_STAT        ", s1, F);
    report("S2 sw; 2 nops; lhu I_STAT     ", s2, F);
    report("S3 sw; 3 nops; lhu I_STAT     ", s3, F);
    report("S5 sw; 5 nops; lhu I_STAT     ", s5, F);
    report("S8 sw; 8 nops; lhu I_STAT     ", s8, F);
    report("R0 sw; lw RAM                 ", r0, F);
    report("R1 sw; nop; lw RAM            ", r1, F);
    report("R3 sw; 3 nops; lw RAM         ", r3, F);
    report("R5 sw; 5 nops; lw RAM         ", r5, F);
    report("W2 sw; sw; 3 nops; lhu I_STAT ", w2, F);
    report("P3 sw; 3 nops; lhu SPUSTAT    ", p3, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
