/*
 * cpu/at-memctrl2 (2026-11-19, the CPU session) - an I/O read in the fuzz host's setup shapes, cold and warm.
 *
 * WHY. experiments/fuzz-all/2026-11-19-rfanchor: in the fuzz host's main, nopping the seven 1F801008h-1F801020h reads saves 8 cycles
 * on the SCPH-7001 and 30 on the emulator; DPCR / DICR / I_MASK 12 against 14. But hwtests/at-memctrl reads every one of those
 * registers at ~3.0 cycles a read in a `nop; lw` loop on the 7001, the 1001 and the emulator alike. So the difference is the shape:
 * the host's memory-control reads have a store in the load's delay slot and store the value next (`lw v0, R; sw x; sw v0`), its DMA
 * reads a nop there (`lw v0, R; nop; sw v0`), and the code is cold.
 *
 * WHAT IT DOES. For each register, 64 copies of a shape (enough to average over the 256-cycle refresh; eight read the refresh phase) in a row, timed cold (first call) and warm (second), against the same
 * shape with the lw a nop. The stores go to a RAM buffer.
 *   S: lw v0, R; sw t1, 0(buf); sw v0, 4(buf)     (main's memory-control reads)
 *   N: lw v0, R; nop;           sw v0, 4(buf)     (main's DMA / IRQ reads)
 *   L: lw v0, R; nop;           nop               (no store: the read alone in the same place)
 * A row: register, shape, base cold / warm, read cold / warm, and (read - base) over 64 cold and warm.
 * READ THE COLD COLUMN. The fuzz host's reads ran in cold code, and the cold numbers are steady on the emulator (next df84fad400:
 * S 289-311, N 228-260, L 135-168 per 64 for every register but RAM_SIZE). The warm call is NOT warm: the same 192 nops read
 * 205-381 on the second call, so the window does not stay in the I-cache through measure(); its column is kept, not read.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-memctrl2`.
 */
#include <common.h>
#include <measure.hpp>

static volatile uint32_t buf[4] __attribute__((aligned(16)));

#define REP8(x) x x x x x x x x
#define REP64(x) REP8(REP8(x))
#define S_RD(off) "lw $v0, " #off "($t8)\n sw $t1, 0($t7)\n sw $v0, 4($t7)\n"
#define S_NO(off) "nop\n sw $t1, 0($t7)\n sw $v0, 4($t7)\n"
#define N_RD(off) "lw $v0, " #off "($t8)\n nop\n sw $v0, 4($t7)\n"
#define N_NO(off) "nop\n nop\n sw $v0, 4($t7)\n"
#define L_RD(off) "lw $v0, " #off "($t8)\n nop\n nop\n"
#define L_NO(off) "nop\n nop\n nop\n"
#define BODY(seq) asm volatile(".set noreorder\n lui $t8, 0x1F80\n move $t7, %0\n move $t1, $zero\n" REP64(seq) "nop\n .set reorder\n" \
                               :: "r"(buf) : "v0", "t1", "t7", "t8", "memory")

struct Four { uint32_t bc, bw, rc, rw; };
#define CASE(name, shape, off) do { \
    auto b = [] { BODY(shape##_NO(off)); }; auto r = [] { BODY(shape##_RD(off)); }; Four f; \
    f.bc = measure<MeasureMethod::CpuClock>(b); f.bw = measure<MeasureMethod::CpuClock>(b); \
    f.rc = measure<MeasureMethod::CpuClock>(r); f.rw = measure<MeasureMethod::CpuClock>(r); show(name, #shape, f); } while (0)

static void show(const char* name, const char* shape, Four f) {
    const int32_t c = (int32_t)f.rc - (int32_t)f.bc, w = (int32_t)f.rw - (int32_t)f.bw;
    printf("%-10s %s   base %4u %4u   read %4u %4u   cold %d/64 warm %d/64\n", name, shape, (unsigned)f.bc, (unsigned)f.bw,
           (unsigned)f.rc, (unsigned)f.rw, (int)c, (int)w);
}

#define ROW(name, off) CASE(name, S, off); CASE(name, N, off); CASE(name, L, off)

int main() {
    printf("\ncpu/at-memctrl2\n");
    printf("an I/O read in three shapes, 64 in a row, cold (first call) and warm, against the shape with the read a nop; Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    ROW("EXP1_DELAY", 0x1008);
    ROW("BIOS_DELAY", 0x1010);
    ROW("COM_DELAY", 0x1020);
    ROW("RAM_SIZE", 0x1060);
    ROW("I_MASK", 0x1074);
    ROW("DPCR", 0x10F0);
    ROW("DICR", 0x10F4);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
