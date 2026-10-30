/*
 * cpu/store-cost — what does a store actually cost, and does a PARTIAL one
 * cost less?
 *
 * WHY THIS TEST EXISTS. ps1-tests' cpu/access-time measures READS only -
 * `readAccessTime`, checked in its own source - so nothing in the corpus times
 * a store at all, and nothing anywhere times an UNALIGNED store. That leaves
 * the biggest open timing question in our emulator unanswerable:
 *
 *   src/psx.js swl(): "a partial store reads nothing on the bus, so the read
 *   is an extra RAM access (9 cycles against a sw's 5), and every SWL/SWR the
 *   BIOS and FF7 do is a whole word (SWL k=3 / SWR k=0)."
 *
 * That comment was written when the evidence was five games. STATUS M131
 * counted the library: **608 of 1,719 discs execute a partial swl, 52,279,209
 * of them**, and 557 a partial swr. This core emulates every swl/swr as
 * read32 + write32 and charges for both. If hardware does no bus read for a
 * store, we are ~4 cycles too dear on tens of millions of instructions.
 *
 * WHAT IT MEASURES. For each of sw / swl / swr, at every byte offset 0..3, the
 * per-instruction cost above a nop - the same baseline-subtracted method
 * access-time uses, so the numbers are comparable with its table. Then the
 * same for lwl/lwr, because access-time only ever loads ALIGNED.
 *
 * READ THE ANSWER LIKE THIS. If hardware charges a partial store the same as
 * an aligned `sw`, this core's extra read is wrong and the fix is to drop it.
 * If a partial store costs a store PLUS a read, this core is already right and
 * the KNOWN-OPEN closes the other way. Either way it is one number per row and
 * no interpretation.
 *
 * ANSWERED by the SCPH-5501, 7501 and 9001 (console day; v0.377.28, src/psx.js swlrWord): SWL and SWR to RAM cost what
 * sw costs, 1.01-1.12 cycles at every offset, and a whole-word SWR +0 / SWL +3 to Expansion 2 what sw does there
 * (57.92) - a partial store reads nothing on the bus. AT v0.377.28: sw 1.00, swl and swr 1.00 at every offset (one
 * 1.05), lw/lwl/lwr 5.10-5.18, SPU 11.06 (consoles 10.39), EXP2 56.00 all three.
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js store-cost`):
 *   sw, sb and sh 1.04 cycles (the write queue takes a store since M207, WRITE_QUEUE_FIRST 6 / NEXT 2 on the bus);
 *   swl and swr 8.18-8.26 at every offset; lw, lwl and lwr 5.16-5.17; to the SPU, sw and swr+0 both 39.04.
 */
#include <common.h>
#include <measure.hpp>

/* Not static: the inline asm below addresses it by name. Word aligned so the
 * offsets 0..3 are exactly the four byte lanes of one word. */
uint32_t storeBuf[16] __attribute__((aligned(16)));

#define COUNT 100

/*
 * One measured loop. Both loops are written with `.set noreorder` and identical
 * structure, so the assembler cannot move the instruction under test into the
 * branch delay slot and change what is being counted - the baseline holds a
 * nop exactly where the measured loop holds the instruction, and the delay slot
 * is filled by hand in both.
 */
#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "la   $t0, storeBuf\n"                      \
    "li   $t1, 0x5a5a5a5a\n"                    \
    "lui  $t3, 0x1f80\n"                        \
    "ori  $t3, $t3, 0x1c00\n"                   \
    "lui  $t4, 0x1f80\n"                        \
    "ori  $t4, $t4, 0x2000\n"                   \
    "li   $t2, %[n]\n"                          \
    "1:\n"                                      \
    "nop\n"                                     \
    INSTR                                       \
    "addiu $t2, $t2, -1\n"                      \
    "bnez  $t2, 1b\n"                           \
    "nop\n"                                     \
    ".set pop\n"

#define MEASURE(INSTR)                                                  \
    measure<MeasureMethod::CpuClock>([] {                               \
        asm volatile(LOOP_BODY(INSTR)                                   \
                     :: [n] "i"(COUNT) : "t0","t1","t2","t3","t4","memory"); \
    })

/* The baseline: the same loop with nothing where the instruction goes. */
static uint32_t baseline() {
    return MEASURE("");
}

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-14s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n",
           name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

int main() {
    /* Timer2 at the system clock is the instrument; measure() puts the machine
     * in a critical section for the duration. */
    printf("\ncpu/store-cost\n");
    printf("Per-instruction cost above a nop, %d iterations, baseline subtracted.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");

    const uint32_t base = baseline();
    printf("  baseline loop: %u cycles for %d iterations\n\n", base, COUNT);

    printf("STORES to RAM (aligned word at storeBuf, offset = byte lane)\n");
    row("sw   +0", MEASURE("sw   $t1, 0($t0)\n"), base);
    row("swl  +0", MEASURE("swl  $t1, 0($t0)\n"), base);
    row("swl  +1", MEASURE("swl  $t1, 1($t0)\n"), base);
    row("swl  +2", MEASURE("swl  $t1, 2($t0)\n"), base);
    row("swl  +3", MEASURE("swl  $t1, 3($t0)\n"), base);
    row("swr  +0", MEASURE("swr  $t1, 0($t0)\n"), base);
    row("swr  +1", MEASURE("swr  $t1, 1($t0)\n"), base);
    row("swr  +2", MEASURE("swr  $t1, 2($t0)\n"), base);
    row("swr  +3", MEASURE("swr  $t1, 3($t0)\n"), base);
    row("sb   +0", MEASURE("sb   $t1, 0($t0)\n"), base);
    row("sh   +0", MEASURE("sh   $t1, 0($t0)\n"), base);

    printf("\nLOADS from RAM (access-time only ever loads ALIGNED)\n");
    row("lw   +0", MEASURE("lw   $t1, 0($t0)\n"), base);
    row("lwl  +0", MEASURE("lwl  $t1, 0($t0)\n"), base);
    row("lwl  +1", MEASURE("lwl  $t1, 1($t0)\n"), base);
    row("lwl  +2", MEASURE("lwl  $t1, 2($t0)\n"), base);
    row("lwl  +3", MEASURE("lwl  $t1, 3($t0)\n"), base);
    row("lwr  +0", MEASURE("lwr  $t1, 0($t0)\n"), base);
    row("lwr  +1", MEASURE("lwr  $t1, 1($t0)\n"), base);
    row("lwr  +2", MEASURE("lwr  $t1, 2($t0)\n"), base);
    row("lwr  +3", MEASURE("lwr  $t1, 3($t0)\n"), base);

    /*
     * A store followed immediately by a load of the SAME word. psx-spx gives
     * the R3000A a write buffer; if there is one, this pair costs more than the
     * two apart because the load has to wait for the buffer to drain. M104's
     * store cost and MEM_STORE_DRAIN in src/psx.js both turn on that question.
     */
    /*
     * DOES THE READ HAPPEN AT ALL? The rows above cannot tell a read-modify-
     * write apart from an expensive store, because in RAM a read is 5 and a
     * store is 3 and the gap is small. So ask the same question where a read is
     * EXPENSIVE. An access to the SPU region costs ~18 cycles and to EXPANSION2
     * ~11, against ~3 for RAM - so if swl/swr really do read the word first, a
     * whole-word swl there costs about TWICE a plain sw, and if hardware drives
     * byte enables and never reads, the two are the same number. A 15-cycle gap
     * is not a measurement anyone has to squint at.
     *
     * The three rows in each block are the KILLER COMPARISON: sw, swr at offset
     * 0 and swl at offset 3 all write the SAME FOUR BYTES. Anything that
     * separates them is the mechanism, not the data.
     *
     * psx-spx on this: "Results on unaligned I/O port writes (via SWL/SWR
     * opcodes) are unknown." Nobody has published the answer, which is why the
     * test asks.
     *
     * Safe to write: voice 0's volume with nothing keyed on, and the Expansion
     * 2 POST region the BIOS itself writes.
     */
    printf("\nIS THE READ REAL? the same full word, where a READ is expensive\n");
    printf("  (sw / swr+0 / swl+3 all write the same 4 bytes - a gap is the mechanism)\n");
    row("SPU  sw   ", MEASURE("sw   $t1, 0($t3)\n"), base);
    row("SPU  swr+0", MEASURE("swr  $t1, 0($t3)\n"), base);
    row("SPU  swl+3", MEASURE("swl  $t1, 3($t3)\n"), base);
    row("EXP2 sw   ", MEASURE("sw   $t1, 0($t4)\n"), base);
    row("EXP2 swr+0", MEASURE("swr  $t1, 0($t4)\n"), base);
    row("EXP2 swl+3", MEASURE("swl  $t1, 3($t4)\n"), base);

    printf("\nWRITE BUFFER: a store and a dependent load\n");
    row("sw;lw same", MEASURE("sw   $t1, 0($t0)\n" "lw   $t3, 0($t0)\n"), base);
    row("sw;lw far ", MEASURE("sw   $t1, 0($t0)\n" "lw   $t3, 32($t0)\n"), base);
    row("sw;nop;lw ", MEASURE("sw   $t1, 0($t0)\n" "nop\n" "lw   $t3, 0($t0)\n"), base);

    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
