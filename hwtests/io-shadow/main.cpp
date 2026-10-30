/*
 * cpu/io-shadow - how many instructions run free behind an I/O register read that follows another read (2026-10-09).
 *
 * WHY. hwtests/spu-read-trace on the SCPH-7501: its loop - lhu <register>; lhu Timer 2; k nops; sll; or; sw; addiu;
 * bnez; addiu - is 31 cycles a pass after an SPU read where this core takes 32, 40 at k = 12 where this core takes 42,
 * and after an I_STAT read 16 at k = 0 on both but 25 at k = 12 where this core takes 26. Twelve nops cost the console 9
 * cycles and this core 10, behind either first read: the console runs one instruction more in the shadow of the second
 * read (or of the pair), and this core charges it. spu-read-grid's single SPU read with k nops behind it matched within
 * 0.05, and io-pairs times the pairs alone, so the case is the pair FOLLOWED by independent work.
 *
 * WHAT IT DOES. Cached loops of 100 passes timed against the empty loop, cycles a pass printed:
 *   T2       lhu T2; k nops                      k = 0..10   (one I/O read's shadow)
 *   IS+T2    lhu I_STAT; lhu T2; k nops          k = 0..12
 *   SPU+T2   lhu SPUSTAT; lhu T2; k nops         k = 0..20
 *   TRACE    spu-read-trace's own body after I_STAT (lhu I_STAT; lhu T2; k nops; sll; or; sw) k = 0..12
 * READ IT LIKE THIS. Each row is flat while the nops fit the shadow and rises by one a nop after: where it bends is the
 * shadow's length. A console's IS+T2 bending a nop later than this core's is spu-read-trace's cycle.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first. ON THIS CORE AT v0.377.60: T2 3.97 then 4.04 at k = 1 (one nop free), +1 a nop after;
 * IS+T2 7.99 and +1 for EVERY nop from k = 1 (none free behind the pair); SPU+T2 21.99, +1 a nop; TRACE 13.05 at k = 0,
 * 23.30 at k = 12.
 * THE RULE: run it here first: `node hwtests/run.js io-shadow`.
 */
#include <common.h>
#include <measure.hpp>

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N4 N2 N2
#define N8 N4 N4
#define N16 N8 N8
#define K0 ""
#define K1 N1
#define K2 N2
#define K3 N2 N1
#define K4 N4
#define K5 N4 N1
#define K6 N4 N2
#define K7 N4 N2 N1
#define K8 N8
#define K9 N8 N1
#define K10 N8 N2
#define K11 N8 N2 N1
#define K12 N8 N4
#define K14 N8 N4 N2
#define K16 N16
#define K18 N16 N2
#define K20 N16 N4

#define RT2   "lhu  $t3, 0x1120($t4)\n"
#define RIST  "lhu  $t1, 0x1070($t4)\n"
#define RSTAT "lhu  $t1, 0x1dae($t4)\n"
#define TAIL  "sll  $t1, $t1, 16\n" "or   $t3, $t3, $t1\n" "sw   $t3, 0($t6)\n"

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "lui  $t4, 0x1f80\n"                        \
    "la   $t6, scratchWord\n"                   \
    "li   $t2, %[n]\n"                          \
    "1:\n"                                      \
    INSTR                                       \
    "addiu $t2, $t2, -1\n"                      \
    "bnez  $t2, 1b\n"                           \
    "nop\n"                                     \
    ".set pop\n"

volatile uint32_t scratchWord;

#define MEASURE(INSTR)                                                          \
    measure<MeasureMethod::CpuClock>([] {                                       \
        asm volatile(LOOP_BODY(INSTR)                                           \
                     :: [n] "i"(COUNT) : "t1","t2","t3","t4","t6","memory");     \
    })

static void row(const char* name, int k, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-7s k=%2d  %6u - %6u = %5u over %d  ->  %u.%02u\n", name, k, cycles, base, d, COUNT, d / COUNT, (d % COUNT) * 100 / COUNT);
}

int main() {
    printf("\ncpu/io-shadow\n");
    printf("Cycles a pass above the empty loop, %d passes, cached: register reads with k nops behind them.\n", COUNT);
    printf("No assertions - compare against your emulator.\n\n");
    const uint32_t b = MEASURE(K0);

    printf("T2 (one read of Timer 2's count)\n");
    row("T2", 0, MEASURE(RT2 K0), b);  row("T2", 1, MEASURE(RT2 K1), b);  row("T2", 2, MEASURE(RT2 K2), b);
    row("T2", 3, MEASURE(RT2 K3), b);  row("T2", 4, MEASURE(RT2 K4), b);  row("T2", 5, MEASURE(RT2 K5), b);
    row("T2", 6, MEASURE(RT2 K6), b);  row("T2", 8, MEASURE(RT2 K8), b);  row("T2", 10, MEASURE(RT2 K10), b);

    printf("\nIS+T2 (I_STAT, then Timer 2)\n");
    row("IS+T2", 0, MEASURE(RIST RT2 K0), b);  row("IS+T2", 1, MEASURE(RIST RT2 K1), b);  row("IS+T2", 2, MEASURE(RIST RT2 K2), b);
    row("IS+T2", 3, MEASURE(RIST RT2 K3), b);  row("IS+T2", 4, MEASURE(RIST RT2 K4), b);  row("IS+T2", 5, MEASURE(RIST RT2 K5), b);
    row("IS+T2", 6, MEASURE(RIST RT2 K6), b);  row("IS+T2", 7, MEASURE(RIST RT2 K7), b);  row("IS+T2", 8, MEASURE(RIST RT2 K8), b);
    row("IS+T2", 10, MEASURE(RIST RT2 K10), b); row("IS+T2", 12, MEASURE(RIST RT2 K12), b);

    printf("\nSPU+T2 (SPUSTAT, then Timer 2)\n");
    row("SPU+T2", 0, MEASURE(RSTAT RT2 K0), b);   row("SPU+T2", 2, MEASURE(RSTAT RT2 K2), b);   row("SPU+T2", 4, MEASURE(RSTAT RT2 K4), b);
    row("SPU+T2", 6, MEASURE(RSTAT RT2 K6), b);   row("SPU+T2", 8, MEASURE(RSTAT RT2 K8), b);   row("SPU+T2", 10, MEASURE(RSTAT RT2 K10), b);
    row("SPU+T2", 12, MEASURE(RSTAT RT2 K12), b); row("SPU+T2", 14, MEASURE(RSTAT RT2 K14), b); row("SPU+T2", 16, MEASURE(RSTAT RT2 K16), b);
    row("SPU+T2", 18, MEASURE(RSTAT RT2 K18), b); row("SPU+T2", 20, MEASURE(RSTAT RT2 K20), b);

    printf("\nTRACE (spu-read-trace's body after I_STAT: lhu; lhu T2; k nops; sll; or; sw)\n");
    row("TRACE", 0, MEASURE(RIST RT2 K0 TAIL), b);   row("TRACE", 2, MEASURE(RIST RT2 K2 TAIL), b);   row("TRACE", 4, MEASURE(RIST RT2 K4 TAIL), b);
    row("TRACE", 6, MEASURE(RIST RT2 K6 TAIL), b);   row("TRACE", 8, MEASURE(RIST RT2 K8 TAIL), b);   row("TRACE", 10, MEASURE(RIST RT2 K10 TAIL), b);
    row("TRACE", 12, MEASURE(RIST RT2 K12 TAIL), b);

    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
