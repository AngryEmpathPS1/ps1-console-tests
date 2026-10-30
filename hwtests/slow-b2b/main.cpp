/*
 * cpu/slow-b2b - is a slow region's back-to-back cost a CPU stall, or a sub-bus
 * recovery that only the NEXT slow access waits for?
 *
 * nocash's DIAG (MEM ACCESS TIMING) times loads from the BIOS ROM, the CD, the
 * SPU and Expansion 1 BACK TO BACK, and they cost 2 cycles more than the same
 * load alone (cpu/access-time's isolated rows). The emulator this was written
 * for (STATUS M230) takes the 2 as a CPU stall under the load shadow - the same
 * 2 RAM's shadow hides: a load costs 1 + S when memory comes next, and 1 + S - 2
 * once enough independent instructions follow. The other reading is a recovery
 * on the sub-bus: then only another SLOW access waits for it, and a store to
 * RAM straight after a ROM load does not. Both fit every console row there is.
 * They part in the shape the kernel's own ROM copy loop has (BFC02B68h): a
 * load from ROM, then a store of what it loaded, 426,460 times a boot.
 *
 * Every row is a per-iteration cost above a loop of nops in the same slots, 100
 * iterations, from cached code. The ROM is read through KSEG1 at BFC00100h.
 * There are no assertions - compare against your emulator.
 *
 * READ IT LIKE THIS. `lbu` alone is the isolated cost; `lbu;lbu` is the
 * back-to-back one, and the rows with fillers between show where the 2 is
 * hidden. THE QUESTION is the `sb` rows: if `lbu;sb` costs what `lbu;lbu`
 * does, the 2 is paid by the CPU whatever comes next (the stall); if `lbu;sb`
 * costs what `lbu` alone does, only a slow access waits for it (the recovery).
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js slow-b2b`): lbu alone
 * 5.95, lbu;lbu 12.97, then 13.95 / 13.98 / 12.96 / 11.99 with 1-4 nops between;
 * lbu;sb loaded 7.93, lbu;sb other 7.96, lbu;3alu;sb 6.94, sb alone 0.00. Its
 * source predicts 6 / 13 / 14 / 14 / 13 / 12 / 8 / 8 / 7 / 0 exactly: the ROM's
 * 6 at the kernel's 0013243Fh (slowPenalties), the 2 under the shadow, refunded
 * at shadow positions 3 and 4 only while nothing there touches memory, and 1
 * less for a load behind a load (M209). The test is shown to read the model.
 * The recovery reading predicts lbu;sb other 6 and lbu;3alu;sb 6 - a console
 * that reads 6 there puts 852,920 cycles of the kernel's ROM copy back (M230).
 */
#include <common.h>
#include <measure.hpp>

/* Not static: the inline asm addresses it by name. */
uint32_t storeBuf[16] __attribute__((aligned(16)));

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define N4 N2 N2
#define N5 N4 N1
#define N6 N4 N2
#define N7 N4 N3
#define N8 N4 N4
#define ROM  "lbu  $t1, 0($t7)\n"
#define ROM2 "lbu  $t4, 1($t7)\n"
#define SBL  "sb   $t1, 0($t0)\n"   /* a store OF the loaded byte (the kernel's copy) */
#define SBO  "sb   $t6, 1($t0)\n"   /* a store of an unrelated register */
#define ALU3 "addu $t5, $t6, $t6\n" "addu $t3, $t3, $t6\n" "xor $t5, $t5, $t6\n"

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "la   $t0, storeBuf\n"                      \
    "li   $t7, 0xBFC00100\n"                    \
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
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t4","t5","t6","t7","memory"); \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-18s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n",
           name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

int main() {
    printf("\ncpu/slow-b2b\n");
    printf("A ROM load's back-to-back cost: a CPU stall, or a sub-bus recovery? (STATUS M230)\n");
    printf("Per-iteration cost above a loop of nops in the same slots, %d iterations.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t b8 = MEASURE(N8);

    printf("ROM LOADS (lbu from BFC00100h, above 8 nops)\n");
    row("lbu alone", MEASURE(ROM N7), b8);
    row("lbu;lbu", MEASURE(ROM ROM2 N6), b8);
    row("lbu;nop;lbu", MEASURE(ROM N1 ROM2 N5), b8);
    row("lbu;2nop;lbu", MEASURE(ROM N2 ROM2 N4), b8);
    row("lbu;3nop;lbu", MEASURE(ROM N3 ROM2 N3), b8);
    row("lbu;4nop;lbu", MEASURE(ROM N4 ROM2 N2), b8);

    printf("\nTHE QUESTION: a RAM store straight after (above 8 nops)\n");
    row("lbu;sb loaded", MEASURE(ROM SBL N6), b8);
    row("lbu;sb other", MEASURE(ROM SBO N6), b8);
    row("lbu;3alu;sb loaded", MEASURE(ROM ALU3 SBL N3), b8);
    row("sb other alone", MEASURE(SBO N7), b8);

    printf("\nDone.\n");
    for (;;) {}
}
