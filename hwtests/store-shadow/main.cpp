/*
 * cpu/store-shadow - what a store does inside a load's shadow, by position.
 *
 * WHY (v0.377.29). hwtests/slow-b2b on three consoles: a store of an unrelated register right behind a ROM load costs
 * 5 cycles more than the load alone (6.96 against 1.87 above the nops) - it waits for the load's access - and v0.377.29
 * models that for the slow regions (SLOW_SHADOW: a store ends the shadow, a cycle sooner right behind the load). For a
 * RAM load nothing measures it: load-pairs' STORELOAD meets the store at the shadow's last position (the sixth), where
 * "the store runs free" (this core since v0.377.26) and "the store ends the shadow" give the same number. A store right
 * behind a load of another register is everywhere in compiled code; this asks where in the shadow it costs.
 *
 * WHAT IT DOES. Rows are per-iteration costs above a loop of 8 nops (load-use's method, 100 iterations):
 *   RAM   lw t1; p - 1 nops; sw t6 to another word; nops to 8 slots - p = 1..7, and lw alone, and the store reading t1
 *   SPAD  the same with the store to the scratchpad (no bus at all)
 *   ROM   lbu t1 from BFC00100h; p - 1 nops; sb t6 to RAM; nops to 8 slots - p = 1..7, and lbu alone
 *
 * READ IT LIKE THIS. A row at the "alone" value says the store ran free there; one above it says it waited for the load,
 * by how much. A RAM row at p = 1 near 6 says a store needs the bus a RAM load holds; near 2, that the write buffer took it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js store-shadow`.
 * ON THIS CORE AT v0.377.29: RAM lw alone 1.99, the store at p = 1..7 2.05-2.17 (it runs free), storing t1 5.95;
 *   the scratchpad 1.98-2.07; ROM lbu alone 2.04, p = 1..7 6.98 7.95 7.94 6.99 5.99 4.98 3.94. slow-b2b measured p = 1
 *   only (6.96). Here a store at p >= 2 ends the shadow without the cycle p = 1 takes, so p = 2 reads above p = 1; a
 *   console whose rows fall from p = 1 on (7 6 5 4 ...) says the store waits for the ACCESS to end, not for the shadow.
 */
#include <common.h>
#include <measure.hpp>

uint32_t ssBuf[16] __attribute__((aligned(16)));

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define N4 N2 N2
#define N5 N4 N1
#define N6 N4 N2
#define N7 N4 N3
#define N8 N4 N4
#define LW   "lw   $t1, 0($t0)\n"
#define ROM  "lbu  $t1, 0($t7)\n"
#define SWO  "sw   $t6, 8($t0)\n"   /* another word, another register */
#define SWL  "sw   $t1, 8($t0)\n"   /* the loaded register */
#define SBO  "sb   $t6, 9($t0)\n"
#define SPO  "sw   $t6, 0($t8)\n"   /* the scratchpad */

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "la   $t0, ssBuf\n"                         \
    "li   $t7, 0xBFC00100\n"                    \
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
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t6","t7","t8","memory"); \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-22s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n", name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

int main() {
    printf("\ncpu/store-shadow\n");
    printf("Per-iteration cost above a loop of 8 nops, %d iterations. No assertions - compare against your emulator.\n\n", COUNT);
    const uint32_t b8 = MEASURE(N8);

    printf("RAM: lw t1, then a store of another register at position p (8 slots in all)\n");
    row("lw alone", MEASURE(LW N7), b8);
    row("p=1", MEASURE(LW SWO N6), b8);
    row("p=2", MEASURE(LW N1 SWO N5), b8);
    row("p=3", MEASURE(LW N2 SWO N4), b8);
    row("p=4", MEASURE(LW N3 SWO N3), b8);
    row("p=5", MEASURE(LW N4 SWO N2), b8);
    row("p=6", MEASURE(LW N5 SWO N1), b8);
    row("p=7", MEASURE(LW N6 SWO), b8);
    row("p=1 storing t1", MEASURE(LW SWL N6), b8);
    row("store alone", MEASURE(SWO N7), b8);

    printf("\nSCRATCHPAD store after a RAM load\n");
    row("p=1", MEASURE(LW SPO N6), b8);
    row("p=3", MEASURE(LW N2 SPO N4), b8);
    row("p=5", MEASURE(LW N4 SPO N2), b8);

    printf("\nROM: lbu t1 (BFC00100h), then sb of another register to RAM at position p\n");
    row("lbu alone", MEASURE(ROM N7), b8);
    row("p=1", MEASURE(ROM SBO N6), b8);
    row("p=2", MEASURE(ROM N1 SBO N5), b8);
    row("p=3", MEASURE(ROM N2 SBO N4), b8);
    row("p=4", MEASURE(ROM N3 SBO N3), b8);
    row("p=5", MEASURE(ROM N4 SBO N2), b8);
    row("p=6", MEASURE(ROM N5 SBO N1), b8);
    row("p=7", MEASURE(ROM N6 SBO), b8);

    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
