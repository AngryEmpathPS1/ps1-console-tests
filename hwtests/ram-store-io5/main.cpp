/*
 * cpu/ram-store-io5 - ram-store-io4 with every sample printed: the write queue's timeline over the DRAM refresh
 * (2026-10-15 small hours).
 *
 * WHY THIS ONE. ram-store-io4 on the SCPH-1001 (two runs identical, ranges and all) has rows whose FASTEST is below
 * every neighbour's - X4k0 14..19 where P4k0 is 19..19, R4k2 16..29 - and its 16 samples are 16 cycles apart in the
 * spin, so they sit at the 16 phases of one 256-cycle refresh period. A refresh holding the bus while the stores queue
 * would let the read go first (the LR33300's read priority), cheaper than no refresh at all; then a row's fastest is
 * not its undisturbed value, its most common is. This prints all 16, in spin order, beside fastest..slowest.
 *
 * ram-store-io4's own header follows.
 *
 * cpu/ram-store-io4 - the write queue's timeline: GPUSTAT and RAM reads after 2-6 stores in five patterns, at 0, 2, 4
 * and 8 nops before the read and with a nop between the stores.
 *
 * WHY. ram-store-io2 on the SCPH-9001 (the SCPH-1001 and 7501 byte for byte): a GPUSTAT or RAM read after n stores
 * alternating between the two 1 KB halves of a 2 KB page waits as if they went to the bus in A/B PAIRS - 6 + 2 for the
 * first, 4 + 2 for each next - and the read waits for every queued store but a trailing one that would begin a new pair
 * (scratch/scratch_wqsim2.js: all 24 such rows exact). But the same model puts the queue two deep where wb-drain
 * measured four, and misses the rows with 8 nops before the read by up to 5. When each queued store leaves, and whether
 * the pair is the 2 KB page's or any two blocks', is what this maps. And the order: ram-store-io2's first store went
 * to the upper 1 KB half of its page (A 80012C00, B 80012800) and a RAM read after four from the other page's upper
 * half took 24; ram-banks' went to the lower half first and its read 5 KB on took 19.
 *
 * WHAT IT DOES. Cached code, interrupts off, Timer 2 at the system clock; each row (its second run) lhu T2; n stores; [k nops]; a read;
 * lhu T2 - fastest..slowest of 16 over the refresh. The read: G lw GPUSTAT, O lw from a block no store went to (C, the
 * next 2 KB page's lower half), U from C + 1 KB (its upper half). A is 4 KB aligned. The stores: S one 1 KB block (A,
 * A+4, ...); P alternating A / B (B = A + 1 KB, the upper half of A's 2 KB page); R alternating B / A (the upper half
 * first); Q A, A+4, B, B+4, A+8, A+12 (pairs of one block); X alternating A / D (4 KB from A, another page). Rows
 * <read><pattern><n>k<k> for n = 2..6, k = 0, 2, 4, 8, and <read><pattern><n>g1 with one nop between the stores.
 *
 * READ IT LIKE THIS. Cycles, fastest..slowest; no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-store-io5 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t buf[2048] __attribute__((aligned(4096)));   /* A at 0, B at +1 KB, C at +2 KB, D at +4 KB */

/* t9 = 1F800000h, t6 = A, t7 = B, t5 = C, t4 = D, a2 the spin (set by the caller); each runs its row TWICE, a spin of
 * a2 before each, and returns the second's count in v0 - the first fetches the row's code into the cache, so nothing
 * between them can evict it (200 rows' code is 3.5 times the 4 KB cache: with ram-store-io2's harness a harness line
 * beside a row's code evicted it, and this core cache-missed GQ6k8 by 27) */
#define NOPS(K) ".rept " #K "\nnop\n.endr\n"
#define FN(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\nli $t0, 2\n0:\nmove $a3, $a2\n1:\nbnez $a3, 1b\naddiu $a3, $a3, -1\n" body \
            "addiu $t0, $t0, -1\nbnez $t0, 0b\nnop\nsubu $v0, $t2, $t1\njr $ra\nandi $v0, $v0, 0xffff\n.set pop\n");
#define T1 "lhu $t1, 0x1120($t9)\n"
#define T2 "lhu $t2, 0x1120($t9)\n"
FN(f_GS2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" NOPS(1) "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GS6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" NOPS(1) "sw $zero, 16($t6)\n" NOPS(1) "sw $zero, 20($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR2k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR2k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR2k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR2k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR2g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR3k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR3k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR3k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR3k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR3g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR4k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR4k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR4k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR4k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR4g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR5k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR5k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR5k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR5k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR5g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR6k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR6k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR6k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR6k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GR6g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GX6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 8($t4)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_OS2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OS2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OS2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OS2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OS3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OS3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OS3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OS4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OS4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OS4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OS5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OS5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OS5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" NOPS(1) "sw $zero, 16($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OS6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OS6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OS6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OS6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" NOPS(1) "sw $zero, 16($t6)\n" NOPS(1) "sw $zero, 20($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OP2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OP2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OP2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OP3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OP3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OP3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OP4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OP4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OP4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OP5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OP5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OP5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OP6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OP6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OP6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OP6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR2k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR2k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OR2k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OR2k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OR2g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR3k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR3k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OR3k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OR3k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OR3g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR4k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR4k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OR4k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OR4k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OR4g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR5k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR5k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OR5k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OR5k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OR5g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR6k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OR6k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OR6k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OR6k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OR6g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OQ2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OQ2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OQ2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OQ3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OQ3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OQ3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OQ4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OQ4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OQ4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OQ5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OQ5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OQ5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OQ6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OQ6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OQ6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OQ6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OX2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OX2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OX2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OX3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OX3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OX3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OX4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OX4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OX4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OX5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OX5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OX5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" "lw $t3, 0($t5)\n" T2)
FN(f_OX6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_OX6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_OX6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OX6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 8($t4)\n" "lw $t3, 0($t5)\n" T2)
FN(f_US2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_US2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_US2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_US2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_US3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_US3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_US3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_US4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_US4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_US4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_US5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_US5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_US5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" NOPS(1) "sw $zero, 16($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_US6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_US6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_US6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_US6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" NOPS(1) "sw $zero, 16($t6)\n" NOPS(1) "sw $zero, 20($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UP2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UP2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UP2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UP3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UP3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UP3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UP4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UP4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UP4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UP5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UP5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UP5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UP6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UP6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UP6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UP6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR2k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR2k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UR2k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UR2k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UR2g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR3k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR3k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UR3k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UR3k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UR3g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR4k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR4k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UR4k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UR4k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UR4g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR5k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR5k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UR5k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UR5k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UR5g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR6k0, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UR6k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UR6k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UR6k8, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UR6g1, T1 "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 8($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UQ6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UQ6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 0($t7)\n" NOPS(1) "sw $zero, 4($t7)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 12($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UX2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UX2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UX2g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UX3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UX3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UX3g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UX4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UX4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UX4g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UX5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UX5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UX5g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" NOPS(1) "sw $zero, 8($t6)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" "lw $t3, 1024($t5)\n" T2)
FN(f_UX6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(2) "lw $t3, 1024($t5)\n" T2)
FN(f_UX6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(4) "lw $t3, 1024($t5)\n" T2)
FN(f_UX6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(8) "lw $t3, 1024($t5)\n" T2)
FN(f_UX6g1, T1 "sw $zero, 0($t6)\n" NOPS(1) "sw $zero, 0($t4)\n" NOPS(1) "sw $zero, 4($t6)\n" NOPS(1) "sw $zero, 4($t4)\n" NOPS(1) "sw $zero, 8($t6)\n" NOPS(1) "sw $zero, 8($t4)\n" "lw $t3, 1024($t5)\n" T2)

extern "C" uint32_t callRow(uint32_t (*f)(void), volatile uint32_t* a, uint32_t spin);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callRow\ncallRow:\nmove $t8, $ra\nlui $t9, 0x1f80\nmove $t6, $a1\naddiu $t7, $a1, 1024\naddiu $t5, $a1, 2048\naddiu $t4, $a1, 4096\njalr $a0\nnop\njr $t8\nnop\n"
        ".set pop\n");

typedef uint32_t (*Row)(void);
static void row(const char* name, Row f) {
    uint32_t lo = 0xFFFF, hi = 0, v[16];
    for (uint32_t k = 0; k < 16; k++) {
        v[k] = callRow(f, buf, 64 + k * 8);   /* long enough that the first run's stores have drained */
        if (v[k] < lo) lo = v[k];
        if (v[k] > hi) hi = v[k];
    }
    printf("  %-8s %4u..%-4u", name, (unsigned)lo, (unsigned)hi);
    for (uint32_t k = 0; k < 16; k++) printf(" %u", (unsigned)v[k]);
    printf("\n");
}

int main() {
    printf("\ncpu/ram-store-io5\n");
    printf("lhu T2; n stores (S one 1 KB block, P alternating its 2 KB page's halves, R the same upper half first, Q pairs of\n");
    printf("one block, X alternating 4 KB apart); [k nops or a nop between stores]; a read (G GPUSTAT, O RAM elsewhere, U its\n");
    printf("upper half); lhu T2 - cycles, fastest..slowest of 16, then the 16 in order (spin 64 + 8k). No assertions.\n\n");
    printf("A %08X\n", (unsigned)(uint32_t)buf);
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("GS2k0", f_GS2k0);
    row("GS2k2", f_GS2k2);
    row("GS2k4", f_GS2k4);
    row("GS2k8", f_GS2k8);
    row("GS2g1", f_GS2g1);
    row("GS3k0", f_GS3k0);
    row("GS3k2", f_GS3k2);
    row("GS3k4", f_GS3k4);
    row("GS3k8", f_GS3k8);
    row("GS3g1", f_GS3g1);
    row("GS4k0", f_GS4k0);
    row("GS4k2", f_GS4k2);
    row("GS4k4", f_GS4k4);
    row("GS4k8", f_GS4k8);
    row("GS4g1", f_GS4g1);
    row("GS5k0", f_GS5k0);
    row("GS5k2", f_GS5k2);
    row("GS5k4", f_GS5k4);
    row("GS5k8", f_GS5k8);
    row("GS5g1", f_GS5g1);
    row("GS6k0", f_GS6k0);
    row("GS6k2", f_GS6k2);
    row("GS6k4", f_GS6k4);
    row("GS6k8", f_GS6k8);
    row("GS6g1", f_GS6g1);
    row("GP2k0", f_GP2k0);
    row("GP2k2", f_GP2k2);
    row("GP2k4", f_GP2k4);
    row("GP2k8", f_GP2k8);
    row("GP2g1", f_GP2g1);
    row("GP3k0", f_GP3k0);
    row("GP3k2", f_GP3k2);
    row("GP3k4", f_GP3k4);
    row("GP3k8", f_GP3k8);
    row("GP3g1", f_GP3g1);
    row("GP4k0", f_GP4k0);
    row("GP4k2", f_GP4k2);
    row("GP4k4", f_GP4k4);
    row("GP4k8", f_GP4k8);
    row("GP4g1", f_GP4g1);
    row("GP5k0", f_GP5k0);
    row("GP5k2", f_GP5k2);
    row("GP5k4", f_GP5k4);
    row("GP5k8", f_GP5k8);
    row("GP5g1", f_GP5g1);
    row("GP6k0", f_GP6k0);
    row("GP6k2", f_GP6k2);
    row("GP6k4", f_GP6k4);
    row("GP6k8", f_GP6k8);
    row("GP6g1", f_GP6g1);
    row("GR2k0", f_GR2k0);
    row("GR2k2", f_GR2k2);
    row("GR2k4", f_GR2k4);
    row("GR2k8", f_GR2k8);
    row("GR2g1", f_GR2g1);
    row("GR3k0", f_GR3k0);
    row("GR3k2", f_GR3k2);
    row("GR3k4", f_GR3k4);
    row("GR3k8", f_GR3k8);
    row("GR3g1", f_GR3g1);
    row("GR4k0", f_GR4k0);
    row("GR4k2", f_GR4k2);
    row("GR4k4", f_GR4k4);
    row("GR4k8", f_GR4k8);
    row("GR4g1", f_GR4g1);
    row("GR5k0", f_GR5k0);
    row("GR5k2", f_GR5k2);
    row("GR5k4", f_GR5k4);
    row("GR5k8", f_GR5k8);
    row("GR5g1", f_GR5g1);
    row("GR6k0", f_GR6k0);
    row("GR6k2", f_GR6k2);
    row("GR6k4", f_GR6k4);
    row("GR6k8", f_GR6k8);
    row("GR6g1", f_GR6g1);
    row("GQ2k0", f_GQ2k0);
    row("GQ2k2", f_GQ2k2);
    row("GQ2k4", f_GQ2k4);
    row("GQ2k8", f_GQ2k8);
    row("GQ2g1", f_GQ2g1);
    row("GQ3k0", f_GQ3k0);
    row("GQ3k2", f_GQ3k2);
    row("GQ3k4", f_GQ3k4);
    row("GQ3k8", f_GQ3k8);
    row("GQ3g1", f_GQ3g1);
    row("GQ4k0", f_GQ4k0);
    row("GQ4k2", f_GQ4k2);
    row("GQ4k4", f_GQ4k4);
    row("GQ4k8", f_GQ4k8);
    row("GQ4g1", f_GQ4g1);
    row("GQ5k0", f_GQ5k0);
    row("GQ5k2", f_GQ5k2);
    row("GQ5k4", f_GQ5k4);
    row("GQ5k8", f_GQ5k8);
    row("GQ5g1", f_GQ5g1);
    row("GQ6k0", f_GQ6k0);
    row("GQ6k2", f_GQ6k2);
    row("GQ6k4", f_GQ6k4);
    row("GQ6k8", f_GQ6k8);
    row("GQ6g1", f_GQ6g1);
    row("GX2k0", f_GX2k0);
    row("GX2k2", f_GX2k2);
    row("GX2k4", f_GX2k4);
    row("GX2k8", f_GX2k8);
    row("GX2g1", f_GX2g1);
    row("GX3k0", f_GX3k0);
    row("GX3k2", f_GX3k2);
    row("GX3k4", f_GX3k4);
    row("GX3k8", f_GX3k8);
    row("GX3g1", f_GX3g1);
    row("GX4k0", f_GX4k0);
    row("GX4k2", f_GX4k2);
    row("GX4k4", f_GX4k4);
    row("GX4k8", f_GX4k8);
    row("GX4g1", f_GX4g1);
    row("GX5k0", f_GX5k0);
    row("GX5k2", f_GX5k2);
    row("GX5k4", f_GX5k4);
    row("GX5k8", f_GX5k8);
    row("GX5g1", f_GX5g1);
    row("GX6k0", f_GX6k0);
    row("GX6k2", f_GX6k2);
    row("GX6k4", f_GX6k4);
    row("GX6k8", f_GX6k8);
    row("GX6g1", f_GX6g1);
    row("OS2k0", f_OS2k0);
    row("OS2k2", f_OS2k2);
    row("OS2k4", f_OS2k4);
    row("OS2k8", f_OS2k8);
    row("OS2g1", f_OS2g1);
    row("OS3k0", f_OS3k0);
    row("OS3k2", f_OS3k2);
    row("OS3k4", f_OS3k4);
    row("OS3k8", f_OS3k8);
    row("OS3g1", f_OS3g1);
    row("OS4k0", f_OS4k0);
    row("OS4k2", f_OS4k2);
    row("OS4k4", f_OS4k4);
    row("OS4k8", f_OS4k8);
    row("OS4g1", f_OS4g1);
    row("OS5k0", f_OS5k0);
    row("OS5k2", f_OS5k2);
    row("OS5k4", f_OS5k4);
    row("OS5k8", f_OS5k8);
    row("OS5g1", f_OS5g1);
    row("OS6k0", f_OS6k0);
    row("OS6k2", f_OS6k2);
    row("OS6k4", f_OS6k4);
    row("OS6k8", f_OS6k8);
    row("OS6g1", f_OS6g1);
    row("OP2k0", f_OP2k0);
    row("OP2k2", f_OP2k2);
    row("OP2k4", f_OP2k4);
    row("OP2k8", f_OP2k8);
    row("OP2g1", f_OP2g1);
    row("OP3k0", f_OP3k0);
    row("OP3k2", f_OP3k2);
    row("OP3k4", f_OP3k4);
    row("OP3k8", f_OP3k8);
    row("OP3g1", f_OP3g1);
    row("OP4k0", f_OP4k0);
    row("OP4k2", f_OP4k2);
    row("OP4k4", f_OP4k4);
    row("OP4k8", f_OP4k8);
    row("OP4g1", f_OP4g1);
    row("OP5k0", f_OP5k0);
    row("OP5k2", f_OP5k2);
    row("OP5k4", f_OP5k4);
    row("OP5k8", f_OP5k8);
    row("OP5g1", f_OP5g1);
    row("OP6k0", f_OP6k0);
    row("OP6k2", f_OP6k2);
    row("OP6k4", f_OP6k4);
    row("OP6k8", f_OP6k8);
    row("OP6g1", f_OP6g1);
    row("OR2k0", f_OR2k0);
    row("OR2k2", f_OR2k2);
    row("OR2k4", f_OR2k4);
    row("OR2k8", f_OR2k8);
    row("OR2g1", f_OR2g1);
    row("OR3k0", f_OR3k0);
    row("OR3k2", f_OR3k2);
    row("OR3k4", f_OR3k4);
    row("OR3k8", f_OR3k8);
    row("OR3g1", f_OR3g1);
    row("OR4k0", f_OR4k0);
    row("OR4k2", f_OR4k2);
    row("OR4k4", f_OR4k4);
    row("OR4k8", f_OR4k8);
    row("OR4g1", f_OR4g1);
    row("OR5k0", f_OR5k0);
    row("OR5k2", f_OR5k2);
    row("OR5k4", f_OR5k4);
    row("OR5k8", f_OR5k8);
    row("OR5g1", f_OR5g1);
    row("OR6k0", f_OR6k0);
    row("OR6k2", f_OR6k2);
    row("OR6k4", f_OR6k4);
    row("OR6k8", f_OR6k8);
    row("OR6g1", f_OR6g1);
    row("OQ2k0", f_OQ2k0);
    row("OQ2k2", f_OQ2k2);
    row("OQ2k4", f_OQ2k4);
    row("OQ2k8", f_OQ2k8);
    row("OQ2g1", f_OQ2g1);
    row("OQ3k0", f_OQ3k0);
    row("OQ3k2", f_OQ3k2);
    row("OQ3k4", f_OQ3k4);
    row("OQ3k8", f_OQ3k8);
    row("OQ3g1", f_OQ3g1);
    row("OQ4k0", f_OQ4k0);
    row("OQ4k2", f_OQ4k2);
    row("OQ4k4", f_OQ4k4);
    row("OQ4k8", f_OQ4k8);
    row("OQ4g1", f_OQ4g1);
    row("OQ5k0", f_OQ5k0);
    row("OQ5k2", f_OQ5k2);
    row("OQ5k4", f_OQ5k4);
    row("OQ5k8", f_OQ5k8);
    row("OQ5g1", f_OQ5g1);
    row("OQ6k0", f_OQ6k0);
    row("OQ6k2", f_OQ6k2);
    row("OQ6k4", f_OQ6k4);
    row("OQ6k8", f_OQ6k8);
    row("OQ6g1", f_OQ6g1);
    row("OX2k0", f_OX2k0);
    row("OX2k2", f_OX2k2);
    row("OX2k4", f_OX2k4);
    row("OX2k8", f_OX2k8);
    row("OX2g1", f_OX2g1);
    row("OX3k0", f_OX3k0);
    row("OX3k2", f_OX3k2);
    row("OX3k4", f_OX3k4);
    row("OX3k8", f_OX3k8);
    row("OX3g1", f_OX3g1);
    row("OX4k0", f_OX4k0);
    row("OX4k2", f_OX4k2);
    row("OX4k4", f_OX4k4);
    row("OX4k8", f_OX4k8);
    row("OX4g1", f_OX4g1);
    row("OX5k0", f_OX5k0);
    row("OX5k2", f_OX5k2);
    row("OX5k4", f_OX5k4);
    row("OX5k8", f_OX5k8);
    row("OX5g1", f_OX5g1);
    row("OX6k0", f_OX6k0);
    row("OX6k2", f_OX6k2);
    row("OX6k4", f_OX6k4);
    row("OX6k8", f_OX6k8);
    row("OX6g1", f_OX6g1);
    row("US2k0", f_US2k0);
    row("US2k2", f_US2k2);
    row("US2k4", f_US2k4);
    row("US2k8", f_US2k8);
    row("US2g1", f_US2g1);
    row("US3k0", f_US3k0);
    row("US3k2", f_US3k2);
    row("US3k4", f_US3k4);
    row("US3k8", f_US3k8);
    row("US3g1", f_US3g1);
    row("US4k0", f_US4k0);
    row("US4k2", f_US4k2);
    row("US4k4", f_US4k4);
    row("US4k8", f_US4k8);
    row("US4g1", f_US4g1);
    row("US5k0", f_US5k0);
    row("US5k2", f_US5k2);
    row("US5k4", f_US5k4);
    row("US5k8", f_US5k8);
    row("US5g1", f_US5g1);
    row("US6k0", f_US6k0);
    row("US6k2", f_US6k2);
    row("US6k4", f_US6k4);
    row("US6k8", f_US6k8);
    row("US6g1", f_US6g1);
    row("UP2k0", f_UP2k0);
    row("UP2k2", f_UP2k2);
    row("UP2k4", f_UP2k4);
    row("UP2k8", f_UP2k8);
    row("UP2g1", f_UP2g1);
    row("UP3k0", f_UP3k0);
    row("UP3k2", f_UP3k2);
    row("UP3k4", f_UP3k4);
    row("UP3k8", f_UP3k8);
    row("UP3g1", f_UP3g1);
    row("UP4k0", f_UP4k0);
    row("UP4k2", f_UP4k2);
    row("UP4k4", f_UP4k4);
    row("UP4k8", f_UP4k8);
    row("UP4g1", f_UP4g1);
    row("UP5k0", f_UP5k0);
    row("UP5k2", f_UP5k2);
    row("UP5k4", f_UP5k4);
    row("UP5k8", f_UP5k8);
    row("UP5g1", f_UP5g1);
    row("UP6k0", f_UP6k0);
    row("UP6k2", f_UP6k2);
    row("UP6k4", f_UP6k4);
    row("UP6k8", f_UP6k8);
    row("UP6g1", f_UP6g1);
    row("UR2k0", f_UR2k0);
    row("UR2k2", f_UR2k2);
    row("UR2k4", f_UR2k4);
    row("UR2k8", f_UR2k8);
    row("UR2g1", f_UR2g1);
    row("UR3k0", f_UR3k0);
    row("UR3k2", f_UR3k2);
    row("UR3k4", f_UR3k4);
    row("UR3k8", f_UR3k8);
    row("UR3g1", f_UR3g1);
    row("UR4k0", f_UR4k0);
    row("UR4k2", f_UR4k2);
    row("UR4k4", f_UR4k4);
    row("UR4k8", f_UR4k8);
    row("UR4g1", f_UR4g1);
    row("UR5k0", f_UR5k0);
    row("UR5k2", f_UR5k2);
    row("UR5k4", f_UR5k4);
    row("UR5k8", f_UR5k8);
    row("UR5g1", f_UR5g1);
    row("UR6k0", f_UR6k0);
    row("UR6k2", f_UR6k2);
    row("UR6k4", f_UR6k4);
    row("UR6k8", f_UR6k8);
    row("UR6g1", f_UR6g1);
    row("UQ2k0", f_UQ2k0);
    row("UQ2k2", f_UQ2k2);
    row("UQ2k4", f_UQ2k4);
    row("UQ2k8", f_UQ2k8);
    row("UQ2g1", f_UQ2g1);
    row("UQ3k0", f_UQ3k0);
    row("UQ3k2", f_UQ3k2);
    row("UQ3k4", f_UQ3k4);
    row("UQ3k8", f_UQ3k8);
    row("UQ3g1", f_UQ3g1);
    row("UQ4k0", f_UQ4k0);
    row("UQ4k2", f_UQ4k2);
    row("UQ4k4", f_UQ4k4);
    row("UQ4k8", f_UQ4k8);
    row("UQ4g1", f_UQ4g1);
    row("UQ5k0", f_UQ5k0);
    row("UQ5k2", f_UQ5k2);
    row("UQ5k4", f_UQ5k4);
    row("UQ5k8", f_UQ5k8);
    row("UQ5g1", f_UQ5g1);
    row("UQ6k0", f_UQ6k0);
    row("UQ6k2", f_UQ6k2);
    row("UQ6k4", f_UQ6k4);
    row("UQ6k8", f_UQ6k8);
    row("UQ6g1", f_UQ6g1);
    row("UX2k0", f_UX2k0);
    row("UX2k2", f_UX2k2);
    row("UX2k4", f_UX2k4);
    row("UX2k8", f_UX2k8);
    row("UX2g1", f_UX2g1);
    row("UX3k0", f_UX3k0);
    row("UX3k2", f_UX3k2);
    row("UX3k4", f_UX3k4);
    row("UX3k8", f_UX3k8);
    row("UX3g1", f_UX3g1);
    row("UX4k0", f_UX4k0);
    row("UX4k2", f_UX4k2);
    row("UX4k4", f_UX4k4);
    row("UX4k8", f_UX4k8);
    row("UX4g1", f_UX4g1);
    row("UX5k0", f_UX5k0);
    row("UX5k2", f_UX5k2);
    row("UX5k4", f_UX5k4);
    row("UX5k8", f_UX5k8);
    row("UX5g1", f_UX5g1);
    row("UX6k0", f_UX6k0);
    row("UX6k2", f_UX6k2);
    row("UX6k4", f_UX6k4);
    row("UX6k8", f_UX6k8);
    row("UX6g1", f_UX6g1);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-store-io5: Done.\n");
    for (;;) VSync(0);
}
