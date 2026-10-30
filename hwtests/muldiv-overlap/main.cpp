/*
 * cpu/muldiv-overlap - what a multiply or divide does to the one still running.
 *
 * WHY (v0.377.26). hwtests/load-pairs' MULT row on the SCPH-5501, 7501 and 9001: `mult t6,t6` (t6 = 5A5A5A5Ah, a 14-
 * cycle multiply) once in a six-instruction loop, nothing reading HI or LO, costs 0.05-0.10 cycles above a nop - each
 * multiply starts while the last is still running, and does not wait for it. This core waited (DuckStation does; the
 * rule was marked UNMEASURED since M208) and read 7.84; mednafen does not. v0.377.26 took the consoles' answer and
 * applied it to the divide too, the same unit - which nothing has measured. This asks, and asks which operation's
 * result HI and LO hold when MFLO finally reads them.
 *
 * WHAT IT DOES. Each row is a per-iteration cost above a loop of nops in the same slots, 100 iterations (load-use's
 * method); t6 = 5A5A5A5Ah (a slow multiply, 14 cycles to MFLO), t5 = 3 (a fast one, 7), a divide 37 (M208):
 *   ALONE      mult (slow) alone; div alone - one every six instructions, nothing reading the result
 *   CONTROL    div; mflo  /  mult slow; mflo  /  mult fast; mflo - the full latency, MFLO waiting
 *   OVERLAP    div; mult fast; mflo  /  mult slow; div; mflo  /  mult slow; mult fast; mflo  /  div; div; mflo
 *              - the second issued right behind the first: does MFLO wait for the second alone (it replaced the first)
 *              or for the first's end plus the second (it waited)?
 *   MTLO       mult slow; mtlo; mflo - what a write to LO does to a running multiply (hwtests/cpu-extras answered this
 *              on three consoles: no wait, the MTLO's value - v0.377.28, MUL_LAST); then mtlo; mfhi and mthi; mflo -
 *              does a write to one half end the wait for the other?
 * Then each OVERLAP and MTLO sequence once more, printing HI and LO: whose result they hold.
 *
 * READ IT LIKE THIS. ALONE ~0 = the new operation does not wait. An OVERLAP row near its second operation's CONTROL
 * = the first was replaced; near the sum = the second waited.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js muldiv-overlap`.
 * ON THIS CORE AT v0.377.26 (a new operation replaces the running one): ALONE 0.00 0.00 0.00; CONTROL 36.00 12.92 6.01;
 *   OVERLAP 6.06 36.01 6.01 36.01, MTLO 11.97 (MFLO still waits for the multiply); RESULTS the second operation's, and
 *   after MTLO, LO the written value with HI the multiply's.
 * AT v0.377.25 (a new one waited, DuckStation's): ALONE 7.83 0.89 30.60; OVERLAP 42.01 49.01 19.02 72.01, MTLO 12.01.
 * AT v0.377.28 (MUL_LAST: the last cycle free, MTLO / MTHI end the wait for both halves): CONTROL 36 12.92 6.01 as
 *   before; OVERLAP 6.06 36.01 6.06 36.01; mult; mtlo; mflo 0.01, mult; mtlo; mfhi 0.01, div; mthi; mflo 0.00.
 */
#include <common.h>
#include <measure.hpp>

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define MULS "mult $t6, $t6\n"
#define MULF "mult $t5, $t5\n"
#define DIV  "div  $zero, $t6, $t5\n"
#define MFLO "mflo $t4\n"
#define MTLO "mtlo $t7\n"
#define MTHI "mthi $t7\n"
#define MFHI "mfhi $t4\n"

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "li   $t6, 0x5a5a5a5a\n"                    \
    "li   $t5, 3\n"                             \
    "li   $t7, 0x12345678\n"                    \
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
                     :: [n] "i"(COUNT) : "t2","t3","t4","t5","t6","t7","hi","lo","memory"); \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    const uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-26s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n", name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

/* One sequence, then HI and LO. */
#define RESULT(NAME, INSTR) do {                                                         \
    uint32_t hi, lo;                                                                     \
    asm volatile(".set push\n.set noreorder\n.set noat\n"                                \
                 "li $t6, 0x5a5a5a5a\nli $t5, 3\nli $t7, 0x12345678\n" INSTR             \
                 "mfhi %0\nmflo %1\n.set pop\n"                                          \
                 : "=r"(hi), "=r"(lo) :: "t4","t5","t6","t7","hi","lo","memory");        \
    printf("  %-26s HI %08x  LO %08x\n", NAME, (unsigned)hi, (unsigned)lo);              \
} while (0)

int main() {
    printf("\ncpu/muldiv-overlap\n");
    printf("Per-iteration cost above a loop of nops in the same slots, %d iterations.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t b1 = MEASURE(N1), b2 = MEASURE(N2), b3 = MEASURE(N3);

    printf("ALONE (one every six instructions, nothing reading HI or LO):\n");
    row("mult slow", MEASURE(MULS), b1);
    row("mult fast", MEASURE(MULF), b1);
    row("div", MEASURE(DIV), b1);

    printf("\nCONTROL (MFLO right behind):\n");
    row("div; mflo", MEASURE(DIV MFLO), b2);
    row("mult slow; mflo", MEASURE(MULS MFLO), b2);
    row("mult fast; mflo", MEASURE(MULF MFLO), b2);

    printf("\nOVERLAP (the second right behind the first, then MFLO):\n");
    row("div; mult fast; mflo", MEASURE(DIV MULF MFLO), b3);
    row("mult slow; div; mflo", MEASURE(MULS DIV MFLO), b3);
    row("mult slow; mult fast; mflo", MEASURE(MULS MULF MFLO), b3);
    row("div; div; mflo", MEASURE(DIV DIV MFLO), b3);
    row("mult slow; mtlo; mflo", MEASURE(MULS MTLO MFLO), b3);
    row("mult slow; mtlo; mfhi", MEASURE(MULS MTLO MFHI), b3);
    row("div; mthi; mflo", MEASURE(DIV MTHI MFLO), b3);

    printf("\nRESULTS (each sequence once; t6 = 5A5A5A5Ah, t5 = 3, t7 = 12345678h):\n");
    RESULT("div", DIV);
    RESULT("mult fast", MULF);
    RESULT("mult slow", MULS);
    RESULT("div; mult fast", DIV MULF);
    RESULT("mult slow; div", MULS DIV);
    RESULT("mult slow; mult fast", MULS MULF);
    RESULT("mult slow; mtlo", MULS MTLO);
    RESULT("div; mtlo", DIV MTLO);

    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
