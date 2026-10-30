/*
 * cpu/store-chain - a store behind a CHAIN of RAM loads, and two stores in a row, inside the last load's shadow.
 *
 * WHY (v0.377.58). hwtests/store-shadow on the SCPH-5501 and 7501: a store right behind ONE RAM load (lw t1; sw t6 to
 * another word of the same buffer) costs 5.22 cycles above the nops where the load alone costs 1.98 - it waits for the
 * load's access - and v0.377.58 models exactly that. But hwtests/spu-latch's "cycles from the edge to the write" then
 * read 106 here where the consoles read 104 (v0.377.57: 105), on every row. Its path holds one such store, in edge()'s
 * tail: lw s3 / lw s2 / lw s1 / lw s0 (the stack, each load in the last one's shadow), sw v0 to a global at the last
 * load's position 1, sh a0 to another at 2, jr ra, its delay slot, an ALU op, then the next lw - and between the two
 * timer reads that sequence costs 18 cycles on v0.377.57, 19 on v0.377.58, ~17 on the consoles. So something there is
 * cheaper than one load and one store: the chain before it, the second store, the stores' DRAM page (the stack and the
 * globals are 2 MB apart; store-shadow's store is 8 bytes from its load), or the jump after.
 *
 * WHAT IT DOES. store-shadow's method: per-iteration costs above a loop of 12 nops, 100 iterations, each row 12 slots.
 * Loads from buf[0..3]; NEAR stores to buf[8..9] (the load's DRAM page), FAR stores 4 KB on (another page).
 *   lw alone / 2 lw / 4 lw (each a different register)
 *   lw; sw NEAR at 1 / sw FAR at 1 / sw; sh NEAR at 1,2 / sh NEAR at 1 / lw; nop; sw; sh at 2,3
 *   4 lw; sw NEAR at 1 / sw FAR at 1 / sw; sh FAR at 1,2 / the same and a taken branch with its delay slot (edge()'s
 *   tail with its jr) / 2 lw; sw FAR at 1
 *
 * READ IT LIKE THIS. Each row against "alone" for its chain: the store's own cost. A FAR row below its NEAR row says a
 * store to another page does not wait for the load's page; a 4 lw row below the 1 lw row says the chain's last access
 * ends sooner (the loads overlap); the two-store row at the single store's says the second store rides behind the first.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.377.58: one load alone 2.04; a store at 1 behind it 5.01 NEAR, 5.02 FAR, sh 4.98; sw, sh at 1, 2
 *   5.24 NEAR / 5.16 FAR; at 2, 3 6.23. Two loads alone 7.14, a FAR sw at 1 10.12. Four loads alone 17.42; a sw at 1
 *   20.31 NEAR / 20.28 FAR; sw, sh 20.42; with the branch 20.35; the branch alone 17.41. (v0.377.57, a store free in a
 *   RAM load's shadow: every store row within 0.25 of its "alone" - 2.05 ... 17.64.) The consoles' 17-cycle edge() tail
 *   asks for the four-load rows at most a cycle above "4 lw alone" with both stores.
 * THE RULE: run it here first: `node hwtests/run.js store-chain`.
 */
#include <common.h>
#include <measure.hpp>

uint32_t scBuf[2048] __attribute__((aligned(4096)));

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
#define N11 N8 N3
#define N12 N8 N4
#define L1   "lw   $t1, 0($t0)\n"
#define L2   L1 "lw   $t4, 4($t0)\n"
#define L4   L2 "lw   $t5, 8($t0)\n" "lw   $t9, 12($t0)\n"
#define SWN  "sw   $t6, 32($t0)\n"      /* NEAR: the loads' page */
#define SHN  "sh   $t6, 36($t0)\n"
#define SWF  "sw   $t6, 0($t7)\n"       /* FAR: 4 KB on */
#define SHF  "sh   $t6, 4($t7)\n"
#define BR   "b    2f\n" "nop\n" "2:\n"  /* a taken branch and its delay slot, edge()'s jr ra */

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "la   $t0, scBuf\n"                         \
    "addiu $t7, $t0, 4096\n"                    \
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
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t4","t5","t6","t7","t9","memory"); \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-30s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n", name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

int main() {
    printf("\ncpu/store-chain\n");
    printf("Per-iteration cost above a loop of 12 nops, %d iterations. No assertions - compare against your emulator.\n\n", COUNT);
    const uint32_t b12 = MEASURE(N12);

    printf("ONE RAM load\n");
    row("lw alone", MEASURE(L1 N11), b12);
    row("lw; sw NEAR at 1", MEASURE(L1 SWN N10), b12);
    row("lw; sw FAR at 1", MEASURE(L1 SWF N10), b12);
    row("lw; sh NEAR at 1", MEASURE(L1 SHN N10), b12);
    row("lw; sw, sh NEAR at 1, 2", MEASURE(L1 SWN SHN N9), b12);
    row("lw; sw, sh FAR at 1, 2", MEASURE(L1 SWF SHF N9), b12);
    row("lw; nop; sw, sh NEAR at 2, 3", MEASURE(L1 N1 SWN SHN N8), b12);

    printf("\nTWO RAM loads\n");
    row("2 lw alone", MEASURE(L2 N10), b12);
    row("2 lw; sw FAR at 1", MEASURE(L2 SWF N9), b12);

    printf("\nFOUR RAM loads (edge()'s lw s3/s2/s1/s0)\n");
    row("4 lw alone", MEASURE(L4 N8), b12);
    row("4 lw; sw NEAR at 1", MEASURE(L4 SWN N7), b12);
    row("4 lw; sw FAR at 1", MEASURE(L4 SWF N7), b12);
    row("4 lw; sw, sh FAR at 1, 2", MEASURE(L4 SWF SHF N6), b12);
    row("4 lw; sw, sh FAR; branch", MEASURE(L4 SWF SHF BR N4), b12);
    row("4 lw; branch alone", MEASURE(L4 BR N6), b12);

    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
