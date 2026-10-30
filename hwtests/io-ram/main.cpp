/*
 * cpu/io-ram - does a RAM load inside an ON-DIE load's shadow issue there? (2026-10-11)
 *
 * WHY. hwtests/refresh-warm (the SCPH-5501 x3): every block's first load sits three instructions after the harness's
 * Timer 2 read (lhu T2; jalr; nop; lw RAM). This core lets a load at a refunded place of another load's shadow issue
 * inside it, a cycle sooner (src/psx.js shadowAhead, "A LOAD INSIDE ANOTHER LOAD'S SHADOW ISSUES THERE": measured for
 * RAM loads behind RAM loads, hwtests/load-pairs) - and an on-die load's shadow counts. With that cycle, refresh-warm's
 * refresh pattern sits a phase early in every block (the second read 8 after the first, the consoles 9); without it
 * the pattern is the consoles' to the cell but every block and the calibration read a cycle long. So the cycle is
 * taken somewhere else, or not by the load. This asks directly, warm, with nothing else near.
 *
 * WHAT IT DOES. load-use's harness: 100 turns of a loop - nop, eight slots, two addiu, bnez, nop - Timer 2 at the system
 * clock around it; each row's cycles a turn above the same loop with nops in the eight slots. Rows:
 *   I_STAT alone / T2 alone / lw RAM alone     an on-die load (lw I_STAT, lhu Timer 2's counter) or a RAM load in slot 1
 *   I_STAT then lw RAM, k = 1..7              the RAM load k slots after the I_STAT read (k = 3, 4: inside its shadow
 *                                            as this core has it)
 *   T2 then lw RAM, k = 1..7                  the same behind a Timer 2 read
 *   lw RAM then I_STAT, k = 1..7              the reverse: the on-die load inside the RAM load's shadow
 *   T2; jalr stub; nop (stub: lw RAM; jr; nop)   the harness's own shape, and the same with the stub's load a nop
 * Each row also prints its cost less the two loads' "alone" rows: 0 where they do not meet, -1 where the second issues
 * inside the first's shadow.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js io-ram`.
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
#define LW  "lw   $t4, 0($t0)\n"
#define IS  "lw   $t1, 0x1070($t7)\n"
#define T2  "lhu  $t1, 0x1120($t7)\n"
#define JS  "jal  ioStub\n"
#define JN  "jal  ioStubNop\n"

/* One measured loop, `.set noreorder` so nothing moves into the delay slot; ra saved around the stub calls. */
#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "move $t8, $ra\n"                           \
    "la   $t0, loadBuf\n"                       \
    "lui  $t7, 0x1f80\n"                        \
    "li   $t2, %[n]\n"                          \
    "1:\n"                                      \
    "nop\n"                                     \
    INSTR                                       \
    "addiu $t3, $t3, 1\n"                       \
    "addiu $t2, $t2, -1\n"                      \
    "bnez  $t2, 1b\n"                           \
    "nop\n"                                     \
    "move $ra, $t8\n"                           \
    ".set pop\n"

#define MEASURE(INSTR)                                                          \
    measure<MeasureMethod::CpuClock>([] {                                       \
        asm volatile(LOOP_BODY(INSTR)                                           \
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t4","t5","t6","t7","t8","ra","memory"); \
    })

__asm__(".text\n.set push\n.set noreorder\n"
        ".global ioStub\nioStub:\n" LW "jr $ra\nnop\n"
        ".global ioStubNop\nioStubNop:\nnop\njr $ra\nnop\n"
        ".set pop\n");

static int32_t per(uint32_t cycles, uint32_t base) { return (int32_t)cycles - (int32_t)base; }
static void show(const char* name, int32_t d, int32_t alone) {
    const int32_t a = d < 0 ? -d : d, x = d - alone, ax = x < 0 ? -x : x;
    printf("  %-24s %s%d.%02d a turn   less the loads alone %s%d.%02d\n", name, d < 0 ? "-" : "", a / COUNT, a % COUNT,
           x < 0 ? "-" : "", ax / COUNT, ax % COUNT);
}

int main() {
    printf("\ncpu/io-ram\n");
    printf("Per-turn cost above a loop of nops in the same eight slots, %d turns.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t b8 = MEASURE(N8);
    const int32_t is = per(MEASURE(IS N7), b8), t2 = per(MEASURE(T2 N7), b8), lw = per(MEASURE(LW N7), b8);
    show("I_STAT alone", is, 0);
    show("T2 alone", t2, 0);
    show("lw RAM alone", lw, 0);

    printf("\nI_STAT then lw RAM, k slots after:\n");
    show("k=1", per(MEASURE(IS LW N6), b8), is + lw);
    show("k=2", per(MEASURE(IS N1 LW N5), b8), is + lw);
    show("k=3", per(MEASURE(IS N2 LW N4), b8), is + lw);
    show("k=4", per(MEASURE(IS N3 LW N3), b8), is + lw);
    show("k=5", per(MEASURE(IS N4 LW N2), b8), is + lw);
    show("k=6", per(MEASURE(IS N5 LW N1), b8), is + lw);
    show("k=7", per(MEASURE(IS N6 LW), b8), is + lw);

    printf("\nT2 then lw RAM, k slots after:\n");
    show("k=1", per(MEASURE(T2 LW N6), b8), t2 + lw);
    show("k=2", per(MEASURE(T2 N1 LW N5), b8), t2 + lw);
    show("k=3", per(MEASURE(T2 N2 LW N4), b8), t2 + lw);
    show("k=4", per(MEASURE(T2 N3 LW N3), b8), t2 + lw);
    show("k=5", per(MEASURE(T2 N4 LW N2), b8), t2 + lw);
    show("k=6", per(MEASURE(T2 N5 LW N1), b8), t2 + lw);
    show("k=7", per(MEASURE(T2 N6 LW), b8), t2 + lw);

    printf("\nlw RAM then I_STAT, k slots after:\n");
    show("k=1", per(MEASURE(LW IS N6), b8), is + lw);
    show("k=2", per(MEASURE(LW N1 IS N5), b8), is + lw);
    show("k=3", per(MEASURE(LW N2 IS N4), b8), is + lw);
    show("k=4", per(MEASURE(LW N3 IS N3), b8), is + lw);
    show("k=5", per(MEASURE(LW N4 IS N2), b8), is + lw);
    show("k=6", per(MEASURE(LW N5 IS N1), b8), is + lw);
    show("k=7", per(MEASURE(LW N6 IS), b8), is + lw);

    printf("\nThe harness's shape (T2; jal stub; nop - the stub's lw at the T2 read's third place):\n");
    const int32_t jn = per(MEASURE(T2 JN N1 N5), b8);
    show("T2; jal stub(lw); nop", per(MEASURE(T2 JS N1 N5), b8), jn + lw);
    show("T2; jal stub(nop); nop", jn, t2);

    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
