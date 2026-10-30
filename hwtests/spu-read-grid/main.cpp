/*
 * spu/spu-read-grid - does a read of an SPU register wait for the SPU's own clock?
 *
 * WHY (2026-10-09). hwtests/spu-busy on the SCPH-5501 and 7501: the CPU polls SPUSTAT for bit 11 to fall, writes SPUCNT,
 * and polls again until the mode shows - 688-701 cycles later on every row of both consoles (13 cycles of spread), where
 * this core reads 662, 668 or 734 (72): its poll loop meets the edge at whatever phase its own loop length gives. A
 * console whose SPU register reads wait for a slot of the SPU's (psx-spx's state machine: every SPU RAM access is 8
 * cycles, 96 slots a sample) would quantise the poll to that grid and land every row within one slot. Nothing measures
 * an SPU read's cost: io-pairs times thirteen registers, none of them the SPU's.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock. Cached loops, 256 iterations each, cycles an iteration:
 *   lhu SPUSTAT; k nops (k = 0..31)        - a grid G shows as the iteration time stepping by G as k grows
 *   lhu I_STAT; k nops (k = 0..31)         - the control: an ordinary I/O register
 *   lhu SPUCNT and lhu 1F801C00h (voice 0's volume) for k = 0..15 - the same for other SPU registers
 *   two SPU reads back to back (SPUSTAT, SPUSTAT) and (SPUSTAT, I_STAT)
 *
 * READ IT LIKE THIS. Each row is cycles an iteration less the empty loop's. A register with no grid reads the same
 * cost at every k (the nops add 1 each, and the row is k + its cost); one on a G-cycle grid reads a staircase: flat
 * while the k nops fit inside one slot, then up by G. The control row must be the plain line.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.377.59: an SPU register read 18.98-19.14 cycles for k = 0..16 - the nops run inside the slow read's
 *   shadow (SLOW_SHADOW) - then k + 3.2 from 17 on (20.16 ... 34.21), the same for SPUSTAT, SPUCNT and voice 0's volume;
 *   I_STAT k + 1.0-1.2 throughout (no shadow to hide them in: a 4-cycle read); SPUSTAT twice 39.01, then I_STAT 21.01,
 *   I_STAT then SPUSTAT 19.99. No grid: a console's staircase, or its flat run ending elsewhere, is the finding.
 * THE RULE: run it here first: `node hwtests/run.js spu-read-grid`.
 */
#include <common.h>
#include <measure.hpp>

#define COUNT 256

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
#define K13 N8 N4 N1
#define K14 N8 N4 N2
#define K15 N8 N4 N2 N1
#define K16 N16
#define K17 N16 N1
#define K18 N16 N2
#define K19 N16 N2 N1
#define K20 N16 N4
#define K21 N16 N4 N1
#define K22 N16 N4 N2
#define K23 N16 N4 N2 N1
#define K24 N16 N8
#define K25 N16 N8 N1
#define K26 N16 N8 N2
#define K27 N16 N8 N2 N1
#define K28 N16 N8 N4
#define K29 N16 N8 N4 N1
#define K30 N16 N8 N4 N2
#define K31 N16 N8 N4 N2 N1

#define RSTAT "lhu  $t1, 0x1ae($t0)\n"      /* SPUSTAT 1F801DAEh */
#define RCNT  "lhu  $t1, 0x1aa($t0)\n"      /* SPUCNT  1F801DAAh */
#define RVOL  "lhu  $t1, 0x000($t5)\n"      /* voice 0 volume 1F801C00h */
#define RIST  "lhu  $t1, 0x070($t4)\n"      /* I_STAT  1F801070h */

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "lui  $t4, 0x1f80\n"                        \
    "addiu $t5, $t4, 0x1c00\n"                  \
    "addiu $t0, $t4, 0x1c00\n"                  \
    "li   $t2, %[n]\n"                          \
    "1:\n"                                      \
    INSTR                                       \
    "addiu $t2, $t2, -1\n"                      \
    "bnez  $t2, 1b\n"                           \
    "nop\n"                                     \
    ".set pop\n"

#define MEASURE(INSTR)                                                          \
    measure<MeasureMethod::CpuClock>([] {                                       \
        asm volatile(LOOP_BODY(INSTR)                                           \
                     :: [n] "i"(COUNT) : "t0","t1","t2","t4","t5","memory"); \
    })

static void row(const char* name, int k, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-8s k=%2d  %6u - %6u = %6u over %d  ->  %u.%02u\n", name, k, cycles, base, d, COUNT, d / COUNT, (d % COUNT) * 100 / COUNT);
}

#define ROWS(NAME, R, B)                                                                                     \
    row(NAME, 0, MEASURE(R K0), B);   row(NAME, 1, MEASURE(R K1), B);   row(NAME, 2, MEASURE(R K2), B);       \
    row(NAME, 3, MEASURE(R K3), B);   row(NAME, 4, MEASURE(R K4), B);   row(NAME, 5, MEASURE(R K5), B);       \
    row(NAME, 6, MEASURE(R K6), B);   row(NAME, 7, MEASURE(R K7), B);   row(NAME, 8, MEASURE(R K8), B);       \
    row(NAME, 9, MEASURE(R K9), B);   row(NAME, 10, MEASURE(R K10), B); row(NAME, 11, MEASURE(R K11), B);     \
    row(NAME, 12, MEASURE(R K12), B); row(NAME, 13, MEASURE(R K13), B); row(NAME, 14, MEASURE(R K14), B);     \
    row(NAME, 15, MEASURE(R K15), B)
#define ROWS_HI(NAME, R, B)                                                                                  \
    row(NAME, 16, MEASURE(R K16), B); row(NAME, 17, MEASURE(R K17), B); row(NAME, 18, MEASURE(R K18), B);     \
    row(NAME, 19, MEASURE(R K19), B); row(NAME, 20, MEASURE(R K20), B); row(NAME, 21, MEASURE(R K21), B);     \
    row(NAME, 22, MEASURE(R K22), B); row(NAME, 23, MEASURE(R K23), B); row(NAME, 24, MEASURE(R K24), B);     \
    row(NAME, 25, MEASURE(R K25), B); row(NAME, 26, MEASURE(R K26), B); row(NAME, 27, MEASURE(R K27), B);     \
    row(NAME, 28, MEASURE(R K28), B); row(NAME, 29, MEASURE(R K29), B); row(NAME, 30, MEASURE(R K30), B);     \
    row(NAME, 31, MEASURE(R K31), B)

int main() {
    printf("\nspu/spu-read-grid\n");
    printf("Cycles an iteration above the empty loop, %d iterations, cached: an SPU register read and k nops.\n", COUNT);
    printf("No assertions - compare against your emulator.\n\n");
    const uint32_t b = MEASURE(K0);

    printf("SPUSTAT (1F801DAEh)\n");
    ROWS("SPUSTAT", RSTAT, b);
    ROWS_HI("SPUSTAT", RSTAT, b);
    printf("\nI_STAT (1F801070h) - the control\n");
    ROWS("I_STAT", RIST, b);
    ROWS_HI("I_STAT", RIST, b);
    printf("\nSPUCNT (1F801DAAh)\n");
    ROWS("SPUCNT", RCNT, b);
    printf("\nvoice 0 volume (1F801C00h)\n");
    ROWS("VOL0", RVOL, b);
    printf("\npairs\n");
    row("STAT2", 0, MEASURE(RSTAT RSTAT), b);
    row("STAT+IS", 0, MEASURE(RSTAT RIST), b);
    row("IS+STAT", 0, MEASURE(RIST RSTAT), b);

    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
