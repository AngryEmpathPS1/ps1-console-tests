/*
 * cpu/load-use — when does the instruction that READS a RAM load's register
 * issue, and does anything else wait for the load?
 *
 * WHY THIS TEST EXISTS (M184). Sony's bus analyzer capture (TUTO3.PAD, a
 * DTL-H2700 dev board) shows the first instruction that reads a main-RAM
 * load's register - the delay slot excepted - issuing 7 cycles after the
 * load's bus access starts, where this core, charging the load in full and
 * nothing after it, issues it at 5: `lw ; x ; USE x x ; lw` is 11 on the bus
 * and 9 here (58 of 60 samples). Biddlecombe's DTL-H2000 loops (1997) agree.
 * Modelled (tools/experiments/load-use.py) it is 9.8% of every cycle of the
 * BIOS boot, because `lw ; nop ; use` is how every compiler of the day
 * filled a load delay slot - so it moves every game, and it was not landed on
 * a dev board's word. Nothing in ps1-tests puts a reader after a RAM load:
 * cpu/access-time's loop never reads what it loads. This asks a retail
 * console directly.
 *
 * WHAT IT MEASURES, four blocks, each row a per-iteration cost above a
 * baseline loop holding a nop in every slot the row fills:
 *
 *   READER    lw + 7 slots, the reader `addu t5,t1,t1` in slot p (2..7) or
 *             nowhere. Blocking load: every row the same. The interlock this
 *             core does not model: +2 at p=2, +1 at p=3, nothing from p=4.
 *   SHADOW    lw + s independent nops (s = 0..7). Blocking: flat. A load whose
 *             bus access overlaps the instructions after it: falling with s.
 *             (Redux's load-timings shows exactly that shape on I_STAT, an
 *             ON-DIE register - never on RAM.)
 *   PAIRS     lw;lw, lw;nop;lw, lw;nop;nop;lw - the capture reads 6 and 8
 *             bus cycles apart for the first two (the second on two samples).
 *   DRAIN     sw then lw of the SAME word, d nops apart (d = 0..5), and a
 *             different word as the control. When the load may start is where
 *             the store's bus burst ends: 6 bus cycles from the store's own bus
 *             start (the capture) or from after its pipeline cost (this core,
 *             M118's MEM_STORE_DRAIN = 6, fitted to dma/chain-looping).
 *
 * The answer is a handful of numbers per block and needs no interpretation
 * beyond the expectations above; run.js prints what this core says so the
 * comparison is in front of whoever runs it on a console.
 *
 * ANSWERED BY THREE CONSOLES (SCPH-5501, 7501, 9001, two runs each, console day): the shadow runs from the third
 * instruction to the stall's end, a branch among them, and a load there into another register issues inside it
 * (src/psx.js SHADOW_RAM, shadowAhead). AT v0.377.26 every row within ~0.15 of all six runs: READER 2.06 6.07 6.06 5.26
 * 4.05 3.04 2.05; SHADOW 3.19 2.17 2.16 2.19 2.20 2.17 2.10 2.10; PAIRS 7.27 8.33 7.25; DRAIN 7.20 6.35 5.28 4.25 3.31
 * 2.28, other 7.31.
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js load-use`):
 *   READER p=2 6.05, p=3 6.02, p=4 5.13, p>=5 4.02-4.12; SHADOW s=0 5.15, s=1 5.11, s>=2 4.10-4.21;
 *   PAIRS lw;lw 10.14, lw;nop;lw 11.20; DRAIN sw;lw 10.04 falling a cycle a nop to 5.99 at d=4.
 */
#include <common.h>
#include <measure.hpp>

/* Not static: the inline asm addresses it by name. */
uint32_t loadBuf[16] __attribute__((aligned(16)));

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define N4 N2 N2
#define N5 N4 N1
#define N6 N4 N2
#define N7 N4 N3
#define N8 N4 N4
#define LW  "lw   $t1, 0($t0)\n"
#define LW2 "lw   $t4, 4($t0)\n"   /* not t2: that is the loop counter */
#define USE "addu $t5, $t1, $t1\n"
#define SW  "sw   $t6, 0($t0)\n"

/* One measured loop, `.set noreorder` so nothing moves into the delay slot. */
#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "la   $t0, loadBuf\n"                       \
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
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t4","t5","t6","memory"); \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-16s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n",
           name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

int main() {
    printf("\ncpu/load-use\n");
    printf("Per-iteration cost above a loop of nops in the same slots, %d iterations.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t b2 = MEASURE(N2), b3 = MEASURE(N3), b4 = MEASURE(N4), b5 = MEASURE(N5),
                   b6 = MEASURE(N6), b7 = MEASURE(N7), b8 = MEASURE(N8), b1 = MEASURE(N1);

    printf("READER: lw t1 + 7 slots, `addu t5,t1,t1` in slot p (above 8 nops)\n");
    row("no reader", MEASURE(LW N7), b8);
    row("reader p=2", MEASURE(LW N1 USE N5), b8);
    row("reader p=3", MEASURE(LW N2 USE N4), b8);
    row("reader p=4", MEASURE(LW N3 USE N3), b8);
    row("reader p=5", MEASURE(LW N4 USE N2), b8);
    row("reader p=6", MEASURE(LW N5 USE N1), b8);
    row("reader p=7", MEASURE(LW N6 USE), b8);

    printf("\nSHADOW: lw + s independent nops (above 1 + s nops)\n");
    row("s=0", MEASURE(LW), b1);
    row("s=1", MEASURE(LW N1), b2);
    row("s=2", MEASURE(LW N2), b3);
    row("s=3", MEASURE(LW N3), b4);
    row("s=4", MEASURE(LW N4), b5);
    row("s=5", MEASURE(LW N5), b6);
    row("s=6", MEASURE(LW N6), b7);
    row("s=7", MEASURE(LW N7), b8);

    printf("\nPAIRS: two loads (above the same count of nops)\n");
    row("lw;lw", MEASURE(LW LW2), b2);
    row("lw;nop;lw", MEASURE(LW N1 LW2), b3);
    row("lw;2nop;lw", MEASURE(LW N2 LW2), b4);

    printf("\nDRAIN: sw then lw of the same word, d nops apart (above d + 2 nops)\n");
    row("sw;lw same", MEASURE(SW LW), b2);
    row("sw;1;lw same", MEASURE(SW N1 LW), b3);
    row("sw;2;lw same", MEASURE(SW N2 LW), b4);
    row("sw;3;lw same", MEASURE(SW N3 LW), b5);
    row("sw;4;lw same", MEASURE(SW N4 LW), b6);
    row("sw;5;lw same", MEASURE(SW N5 LW), b7);
    row("sw;lw other", MEASURE(SW LW2), b2);

    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
