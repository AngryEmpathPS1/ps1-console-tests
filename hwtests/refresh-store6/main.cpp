/*
 * cpu/refresh-store6 - why a nop makes a load behind a delay-slot store FASTER: where the load goes, and whether the read before it waited (2026-10-30). refresh-store's harness.
 *
 * WHY. hwtests/refresh-store3 and 4 on the SCPH-5501 #2: sw A; on-die read; sw A+8; lw A+8 reads 24, with 1, 2 or 3 nops before the lw 23. The load waits for that store either way, so a nop should
 * change nothing, and on the console it saves a cycle. This core reads 23 for all four. A second opinion (an AI's report, not included; its explanation is
 * its own hypothesis) names what nothing here ever varied: the load is the very word just stored, Sony's documented case (a read of a pending write waits for it). Every row of T1's family has
 * that load. Psy-Q's Hardware.pdf ch.2 and that report give the same rule. So: is it the address?
 *
 * WHAT IT DOES. refresh-store's harness and calibration, each case at the 72 phases from F - 32 to F + 39. T = lhu T2.
 *   X0 / Y0   sw A; T; sw A+8; [nop;] lw A+8       the word just stored (refresh-store3's T1 / T3, the control)
 *   X1 / Y1   ... lw A+12                          the next word
 *   X2 / Y2   ... lw A+72                          another 64-byte block, the same 1 KB
 *   X3 / Y3   ... lw A+2048                        another 1 KB block
 *   X4 / Y4   ... lw A+16K                         another page
 *   S0 / S1   sw A; T; sw SP+8; [nop;] lw SP+8     the second store and the load on the scratchpad: no queue at all
 *   W0 / W1   sw A; 4 nops; T; sw A+8; [nop;] lw A+8   the read that did not wait: no hold, the first store done
 *   B0 / B1   T; sw A+8; [nop;] lw A+8             no first store at all
 *   D0 / D1   sw A; T; sw A+8; [nop;] lw A         the FIRST store's word
 * Each prints its commonest cost, then the cost at each phase from the refresh, sixteen to a line ('.' where no trial landed).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-store6 --frames=3000 --quiet=600`.
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
CASE(caseX0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseY0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseX1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 12(%1)\n")
CASE(caseY1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 12(%1)\n")
CASE(caseX2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 72(%1)\n")
CASE(caseY2, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 72(%1)\n")
CASE(caseX3, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 2048(%1)\n")
CASE(caseY3, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 2048(%1)\n")
CASE(caseX4, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 0x4000(%1)\n")
CASE(caseY4, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 0x4000(%1)\n")
CASE(caseS0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8($t4)\n" "lw $t2, 8($t4)\n")
CASE(caseS1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8($t4)\n" "nop\n" "lw $t2, 8($t4)\n")
CASE(caseW0, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseW1, "sw $zero, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseB0, "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 8(%1)\n")
CASE(caseB1, "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 8(%1)\n")
CASE(caseD0, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "lw $t2, 0(%1)\n")
CASE(caseD1, "sw $zero, 0(%1)\n" "lhu $t2, 0x1120($t4)\n" "sw $zero, 8(%1)\n" "nop\n" "lw $t2, 0(%1)\n")

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
    printf("\ncpu/refresh-store6\n");
    printf("A load behind a delay-slot store, by where it goes, at each phase from a refresh (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    sweep(calib, 0, 256, 16);
    const int base = modeAll(0, 256);
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; lw RAM; lhu T2: commonest %d, highest %d first at phase %d (F)\n\n", base, hi, F);

    report("X0 lw the word stored         ", caseX0, F);
    report("Y0 lw the word stored (nop)   ", caseY0, F);
    report("X1 lw the next word           ", caseX1, F);
    report("Y1 lw the next word (nop)     ", caseY1, F);
    report("X2 lw other 64 bytes          ", caseX2, F);
    report("Y2 lw other 64 bytes (nop)    ", caseY2, F);
    report("X3 lw other 1 KB              ", caseX3, F);
    report("Y3 lw other 1 KB (nop)        ", caseY3, F);
    report("X4 lw other page              ", caseX4, F);
    report("Y4 lw other page (nop)        ", caseY4, F);
    report("S0 scratchpad                 ", caseS0, F);
    report("S1 scratchpad (nop)           ", caseS1, F);
    report("W0 read did not wait          ", caseW0, F);
    report("W1 read did not wait (nop)    ", caseW1, F);
    report("B0 no first store             ", caseB0, F);
    report("B1 no first store (nop)       ", caseB1, F);
    report("D0 lw the first word          ", caseD0, F);
    report("D1 lw the first word (nop)    ", caseD1, F);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/refresh-store: Done.\n");
    for (;;) VSync(0);
}
