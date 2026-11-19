/*
 * cpu/at-memctrl3 (2026-11-19, the CPU session) - a cold on-die read behind k queued stores, with a nop or a store in its delay slot.
 *
 * WHY. hwtests/at-memctrl2 (cold, 64 in a row, against the shape with the read a nop; the SCPH-7001 and 1001 identical) reads an
 * on-die register at 2.4 a read with a store in its delay slot and one after (S), 3.3 with a nop there (N), 3.8 with no store at all
 * (L). The emulator (next df84fad400) reads 4.7 / 3.8 / 2.3: the order turned round. Its bases (the shapes with the read a nop) are
 * the consoles' to the cycle, so the stores and the line fills are right; what is wrong is the read beside them. In S the read has two
 * stores queued in front of it (the shape before) and here waits for them; in L nothing is queued and here it runs under the fill.
 * This takes the two terms apart: k = 0, 1 or 2 RAM stores right before the read, and its delay slot a nop (n) or a RAM store (s),
 * then three nops so every copy is six instructions (three copies to two cache lines). DPCR only: at-memctrl2's registers read alike
 * (RAM_SIZE aside).
 *
 * A row: k, slot, base cold / warm, read cold / warm, (read - base) over 64 cold and warm. READ THE COLD COLUMN (at-memctrl2's
 * header: the warm call does not stay in the I-cache).
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-memctrl3`.
 */
#include <common.h>
#include <measure.hpp>

static volatile uint32_t buf[4] __attribute__((aligned(16)));

#define REP8(x) x x x x x x x x
#define REP64(x) REP8(REP8(x))
#define ST0 "nop\n nop\n"
#define ST1 "nop\n sw $t1, 0($t7)\n"
#define ST2 "sw $t1, 0($t7)\n sw $t1, 8($t7)\n"
#define SLn "nop\n"
#define SLs "sw $t1, 4($t7)\n"
#define RD "lw $v0, 0x10F0($t8)\n"
#define NO "nop\n"
#define TAIL "nop\n nop\n nop\n"
#define BODY(seq) asm volatile(".set noreorder\n lui $t8, 0x1F80\n move $t7, %0\n move $t1, $zero\n" REP64(seq) "nop\n .set reorder\n" \
                               :: "r"(buf) : "v0", "t1", "t7", "t8", "memory")

struct Four { uint32_t bc, bw, rc, rw; };
static void show(int k, char slot, Four f) {
    const int32_t c = (int32_t)f.rc - (int32_t)f.bc, w = (int32_t)f.rw - (int32_t)f.bw;
    printf("DPCR k%d %c   base %4u %4u   read %4u %4u   cold %d/64 warm %d/64\n", k, slot, (unsigned)f.bc, (unsigned)f.bw,
           (unsigned)f.rc, (unsigned)f.rw, (int)c, (int)w);
}
#define CASE(k, ST, sl, SL) do { \
    auto b = [] { BODY(ST NO SL TAIL); }; auto r = [] { BODY(ST RD SL TAIL); }; Four f; \
    f.bc = measure<MeasureMethod::CpuClock>(b); f.bw = measure<MeasureMethod::CpuClock>(b); \
    f.rc = measure<MeasureMethod::CpuClock>(r); f.rw = measure<MeasureMethod::CpuClock>(r); show(k, sl, f); } while (0)

int main() {
    printf("\ncpu/at-memctrl3\n");
    printf("a cold DPCR read behind k queued RAM stores, its delay slot a nop (n) or a store (s), 64 in a row, against the read a nop; Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    CASE(0, ST0, 'n', SLn); CASE(0, ST0, 's', SLs);
    CASE(1, ST1, 'n', SLn); CASE(1, ST1, 's', SLs);
    CASE(2, ST2, 'n', SLn); CASE(2, ST2, 's', SLs);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
