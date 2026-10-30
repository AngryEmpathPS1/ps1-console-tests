/*
 * cpu/load-pairs - what a RAM load's shadow covers, mapped finer than load-use samples it.
 *
 * WHY (console day, 2026-10-05). load-use on the SCPH-5501 and SCPH-7501 (within 0.1 cycle of each other) showed a
 * RAM load nothing reads costing 2 cycles above a nop, a reader in slot p paying 9 - p from p = 4 to 7, and lw + s
 * nops falling to 2 from s = 1 on. This emulator charged 4 from slot 5 on (M207's shadow: slots 3 and 4 only). A
 * shadow that runs to the end of the load's own stall - slots 3..6 for RAM - with a branch in it refunded, brings
 * every READER, SHADOW and DRAIN row to within ~0.1 (RMS 2.32 -> 0.53 cycles) but the PAIRS: two loads 0, 1 and 2
 * nops apart read 7.28, 8.37 and 7.26 on both consoles - not monotonic - where that rule says 8.2, 9.3, 9.3, and
 * "sw; 5 nops; lw" 2.2 where it says 3.1. This maps the cases between, so the rule is read off a console instead of
 * fitted to three points:
 *   PAIRS     lw t1 ; k nops ; lw t4 (another register, the same line), k = 0..7
 *   SAMEREG   lw t1 ; k nops ; lw t1 (the same register - M209 gives a different register the bus a cycle sooner)
 *   FARPAGE   lw t1 ; k nops ; lw t4 from 4 KB on (another DRAM page)
 *   BRANCH    lw t1 ; a never-taken `bne $zero,$zero` in slot p (its delay slot a nop) ; nops to 8 slots, p = 2..7,
 *             each beside its CONTROL - the same with a nop for the lw - so the branch's own cost cancels
 *   TAKEN     the same with `beq $zero,$zero` to the instruction after its delay slot (taken, nothing skipped)
 *   STORELOAD sw ; k nops ; lw of the same word, k = 0..8
 *   SCRATCH   lw t1 ; k nops ; lw t4 from the scratchpad (on the CPU's own bus: does it wait for the RAM load?)
 *   MULT      lw t1 ; k nops ; mult (nothing reads LO/HI): does a multiply in the shadow end it?
 * Every row is a per-iteration cost above a loop holding a nop in each slot it fills, as load-use's, 100 iterations.
 *
 * READ IT LIKE THIS. PAIRS against k says when a second load may start after the first: a stall that ends at a
 * fixed cycle falls a cycle a nop; load-use's 7.28 / 8.37 / 7.26 says it does not. BRANCH minus CONTROL is zero where
 * a branch in that slot runs free inside the shadow and one where it does not. STORELOAD's tail says where the
 * store's burst ends.
 *
 * ANSWERED (v0.377.25-.26, src/psx.js SHADOW_RAM and shadowAhead) by the SCPH-5501, 7501 and 9001, two runs each: every
 * section within ~0.15 of all six. AT v0.377.26: PAIRS 7.27 8.33 7.19 6.25 5.27 4.22 4.31 4.38; SAMEREG 9.20 9.31 9.32
 * 8.34; FARPAGE 7.33 8.21 7.28 6.28; BRANCH and TAKEN 2.08-2.13 (controls 0.03-0.08); STORELOAD 7.32 6.40 5.22 4.24 3.23
 * 2.21 2.11 2.22 2.12; SCRATCH alone 0.00, then 5.20 6.16 5.11 4.15 3.25; MULT alone 0.00 (a multiply does not wait for
 * the one running), then 2.13 2.08 2.12 2.05.
 * THE RULE: run it here first: `node hwtests/run.js load-pairs`. ON THIS CORE AT v0.362 (M207's shadow, slots 3-4):
 *   PAIRS k=0..7 10.05 11.22 11.19 10.22 9.30 9.32 9.27 9.34; SAMEREG 11.28 11.18 11.21 10.24; FARPAGE as PAIRS;
 *   BRANCH lw 4.2-5.1 by p, every CONTROL 0.02; TAKEN the same; STORELOAD k=0..8 10.07 9.09 8.08 7.15 6.13 5.06 5.31
 *   5.28 5.06; SCRATCH alone 0, after the lw 6.1-4.0; MULT alone 7.85, after the lw 4.0-4.1.
 * THE CANDIDATE (the shadow to the end of the stall, a branch in it refunded - scratch's experiment core): PAIRS
 *   8.24 9.28 9.29 8.41 7.35 6.30 5.40 5.32; BRANCH and TAKEN 2.07-2.12 at every p; STORELOAD 7.22 6.43 5.24 4.23
 *   3.34 3.12 3.13 3.17 3.27; SCRATCH as above; MULT after the lw 2.1. load-use's consoles read PAIRS k=0..2 7.28,
 *   8.37, 7.26 and STORELOAD k=0..5 7.23 6.33 5.36 4.25 3.26 2.20.
 */
#include <common.h>
#include <measure.hpp>

/* Not static: the inline asm addresses them by name. loadFar sits 4 KB on, on another DRAM page. */
uint32_t loadBuf[16] __attribute__((aligned(16)));
uint32_t loadFar[1024 + 16] __attribute__((aligned(4096)));

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define N4 N2 N2
#define N5 N4 N1
#define N6 N4 N2
#define N7 N4 N3
#define N8 N4 N4
#define N9 N8 N1
#define N10 N8 N2
#define LW  "lw   $t1, 0($t0)\n"
#define LW2 "lw   $t4, 4($t0)\n"
#define LWS "lw   $t1, 4($t0)\n"
#define LWF "lw   $t4, 0($t7)\n"
#define LWP "lw   $t4, 0($t8)\n"
#define SW  "sw   $t6, 0($t0)\n"
#define MUL "mult $t6, $t6\n"
#define BNE "bne  $zero, $zero, 2f\n"
#define BEQ "beq  $zero, $zero, 2f\n"
#define L2  "2:\n"

/* One measured loop, `.set noreorder` so nothing moves into the delay slot. t7: 4 KB on; t8: the scratchpad. */
#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "la   $t0, loadBuf\n"                       \
    "la   $t7, loadFar + 4096\n"                \
    "lui  $t8, 0x1f80\n"                        \
    "li   $t6, 0x5a5a5a5a\n"                    \
    "li   $t2, %[n]\n"                          \
    "1:\n"                                      \
    "nop\n"                                     \
    INSTR                                       \
    "addiu $t3, $t3, 1\n"                       \
    "addiu $t2, $t2, -1\n"                      \
    "bnez  $t2, 1b\n"                           \
    "nop\n"                                     \
    ".set pop\n"

#define MEASURE(INSTR)                                                          \
    measure<MeasureMethod::CpuClock>([] {                                       \
        asm volatile(LOOP_BODY(INSTR)                                           \
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t4","t5","t6","t7","t8","hi","lo","memory"); \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-18s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n",
           name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

/* A branch in slot p of 8 after the lw (or after a nop, the control): p - 1 nops, the branch, its delay-slot nop,
 * then nops to 8 slots in all, the label after them. */
#define BR8(FIRST, BR, P1, REST) FIRST P1 BR N1 REST L2
/* ...and a TAKEN branch whose target is the instruction after its delay slot: the label there, nothing skipped. */
#define BRT8(FIRST, P1, REST) FIRST P1 BEQ N1 L2 REST

int main() {
    printf("\ncpu/load-pairs\n");
    printf("Per-iteration cost above a loop of nops in the same slots, %d iterations.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t b1 = MEASURE(N1), b2 = MEASURE(N2), b3 = MEASURE(N3), b4 = MEASURE(N4), b5 = MEASURE(N5),
                   b6 = MEASURE(N6), b7 = MEASURE(N7), b8 = MEASURE(N8), b9 = MEASURE(N9), b10 = MEASURE(N10);

    printf("PAIRS: lw t1 ; k nops ; lw t4 (the same line)\n");
    row("k=0", MEASURE(LW LW2), b2);
    row("k=1", MEASURE(LW N1 LW2), b3);
    row("k=2", MEASURE(LW N2 LW2), b4);
    row("k=3", MEASURE(LW N3 LW2), b5);
    row("k=4", MEASURE(LW N4 LW2), b6);
    row("k=5", MEASURE(LW N5 LW2), b7);
    row("k=6", MEASURE(LW N6 LW2), b8);
    row("k=7", MEASURE(LW N7 LW2), b9);

    printf("\nSAMEREG: lw t1 ; k nops ; lw t1\n");
    row("k=0", MEASURE(LW LWS), b2);
    row("k=1", MEASURE(LW N1 LWS), b3);
    row("k=2", MEASURE(LW N2 LWS), b4);
    row("k=3", MEASURE(LW N3 LWS), b5);

    printf("\nFARPAGE: lw t1 ; k nops ; lw t4 4 KB on\n");
    row("k=0", MEASURE(LW LWF), b2);
    row("k=1", MEASURE(LW N1 LWF), b3);
    row("k=2", MEASURE(LW N2 LWF), b4);
    row("k=3", MEASURE(LW N3 LWF), b5);

    printf("\nBRANCH: lw t1, a never-taken bne in slot p, 8 slots in all (CONTROL: a nop for the lw)\n");
    row("p=2 lw",   MEASURE(BR8(LW, BNE, N1, N5)), b9);
    row("p=2 ctl",  MEASURE(BR8(N1, BNE, N1, N5)), b9);
    row("p=3 lw",   MEASURE(BR8(LW, BNE, N2, N4)), b9);
    row("p=3 ctl",  MEASURE(BR8(N1, BNE, N2, N4)), b9);
    row("p=4 lw",   MEASURE(BR8(LW, BNE, N3, N3)), b9);
    row("p=4 ctl",  MEASURE(BR8(N1, BNE, N3, N3)), b9);
    row("p=5 lw",   MEASURE(BR8(LW, BNE, N4, N2)), b9);
    row("p=5 ctl",  MEASURE(BR8(N1, BNE, N4, N2)), b9);
    row("p=6 lw",   MEASURE(BR8(LW, BNE, N5, N1)), b9);
    row("p=6 ctl",  MEASURE(BR8(N1, BNE, N5, N1)), b9);
    row("p=7 lw",   MEASURE(BR8(LW, BNE, N6, "")), b9);
    row("p=7 ctl",  MEASURE(BR8(N1, BNE, N6, "")), b9);

    printf("\nTAKEN: the same with a taken beq to the instruction after its delay slot\n");
    row("p=3 lw",   MEASURE(BRT8(LW, N2, N4)), b9);
    row("p=3 ctl",  MEASURE(BRT8(N1, N2, N4)), b9);
    row("p=4 lw",   MEASURE(BRT8(LW, N3, N3)), b9);
    row("p=4 ctl",  MEASURE(BRT8(N1, N3, N3)), b9);
    row("p=5 lw",   MEASURE(BRT8(LW, N4, N2)), b9);
    row("p=5 ctl",  MEASURE(BRT8(N1, N4, N2)), b9);
    row("p=6 lw",   MEASURE(BRT8(LW, N5, N1)), b9);
    row("p=6 ctl",  MEASURE(BRT8(N1, N5, N1)), b9);

    printf("\nSTORELOAD: sw ; k nops ; lw of the same word\n");
    row("k=0", MEASURE(SW LW), b2);
    row("k=1", MEASURE(SW N1 LW), b3);
    row("k=2", MEASURE(SW N2 LW), b4);
    row("k=3", MEASURE(SW N3 LW), b5);
    row("k=4", MEASURE(SW N4 LW), b6);
    row("k=5", MEASURE(SW N5 LW), b7);
    row("k=6", MEASURE(SW N6 LW), b8);
    row("k=7", MEASURE(SW N7 LW), b9);
    row("k=8", MEASURE(SW N8 LW), b10);

    printf("\nSCRATCH: lw t1 (RAM) ; k nops ; lw t4 (scratchpad)   and the scratchpad load alone\n");
    row("alone", MEASURE(LWP), b1);
    row("k=0", MEASURE(LW LWP), b2);
    row("k=1", MEASURE(LW N1 LWP), b3);
    row("k=2", MEASURE(LW N2 LWP), b4);
    row("k=3", MEASURE(LW N3 LWP), b5);
    row("k=4", MEASURE(LW N4 LWP), b6);

    printf("\nMULT: lw t1 ; k nops ; mult (LO/HI never read)   and the mult alone\n");
    row("alone", MEASURE(MUL), b1);
    row("k=1", MEASURE(LW N1 MUL N5), b8);
    row("k=2", MEASURE(LW N2 MUL N4), b8);
    row("k=3", MEASURE(LW N3 MUL N3), b8);
    row("k=4", MEASURE(LW N4 MUL N2), b8);

    printf("\nDone.\n");
    /* Stay here after Done., as every program here does on a console (console day, 2026-10-05). */
    for (;;) {}
}
