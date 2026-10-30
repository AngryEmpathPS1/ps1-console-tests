/*
 * cpu/ram-store-io6 - behind a read that went ahead of the write queue, and the width of its own-block check
 * (2026-10-15 small hours).
 *
 * WHY. ram-store-io4/5 on the SCPH-1001 (two runs each, identical): 373 of 375 rows' undisturbed values are one rule -
 * stores leave a 4-deep queue for the bus FIFO (a store to another 1 KB block than the last a new 6-cycle transaction, the
 * same block's +2 as a burst), and when the bus comes free with stores still queued a read already waiting goes FIRST
 * (the LR33300's read priority), +2 (+4 when the next two queued stores are one block's), unless a queued store is in the
 * read's own 1 KB block (ram-store-io3's M rows). What the stores it went ahead of do next, and how wide "own block" is,
 * no test has asked.
 *
 * WHAT IT DOES. ram-store-io5's harness (each row run twice inside its own code, the second timed; 16 samples printed,
 * 16 cycles apart over a 256-cycle refresh period). A is 4 KB aligned, B = A + 1 KB, C = A + 2 KB.
 *   A<p><n>j<j>  n stores (S one block, P alternating A / B, Q A, A+4, B, B+4, A+8), lw GPUSTAT, j nops, lw GPUSTAT
 *   B<p><n>j<j>  the same with the second read from RAM at C
 *   CA<d>k<k>    A, B, A+4, k nops, lw A+d (d 4 = the queued word itself ... 1020, the block's last word)
 *   CB<d>k<k>    the same, lw B+d (B the second store)
 *   CX<d>k<k>    the same, lw A+d for d = 2 KB, 4 KB, 8 KB, 16 KB + 4 (A+4 in the low bits, other blocks)
 *   CCk<k>       the same, lw C (no store near it: ram-store-io4's O row)
 *
 * READ IT LIKE THIS. Cycles: fastest..slowest, then the 16 in order; the most common is the undisturbed value. No
 * assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-store-io6 --ntsc`.
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
FN(f_AP3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP3j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP3j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP4j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP4j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP5j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AP5j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS3j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS3j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS4j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS4j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS5j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AS5j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ3j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ3j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ4j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ4j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ5j6, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_AQ5j12, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(12) "lw $t3, 0x1814($t9)\n" T2)
FN(f_BP3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0($t5)\n" T2)
FN(f_BP3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_BP3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_BP3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_BP4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0($t5)\n" T2)
FN(f_BP4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_BP4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_BP4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_BP5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0($t5)\n" T2)
FN(f_BP5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_BP5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_BP5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_BQ3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0($t5)\n" T2)
FN(f_BQ3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_BQ3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_BQ3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_BQ4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0($t5)\n" T2)
FN(f_BQ4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_BQ4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_BQ4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_BQ5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" "lw $t3, 0($t5)\n" T2)
FN(f_BQ5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_BQ5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(4) "lw $t3, 0($t5)\n" T2)
FN(f_BQ5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0x1814($t9)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_CA4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 4($t6)\n" T2)
FN(f_CA8k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 8($t6)\n" T2)
FN(f_CA12k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 12($t6)\n" T2)
FN(f_CA16k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 16($t6)\n" T2)
FN(f_CA32k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 32($t6)\n" T2)
FN(f_CA64k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 64($t6)\n" T2)
FN(f_CA128k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 128($t6)\n" T2)
FN(f_CA256k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 256($t6)\n" T2)
FN(f_CA512k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 512($t6)\n" T2)
FN(f_CA1020k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 1020($t6)\n" T2)
FN(f_CB0k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t7)\n" T2)
FN(f_CB4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 4($t7)\n" T2)
FN(f_CB8k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 8($t7)\n" T2)
FN(f_CB64k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 64($t7)\n" T2)
FN(f_CB512k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 512($t7)\n" T2)
FN(f_CB1020k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 1020($t7)\n" T2)
FN(f_CX2052k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 2052($t6)\n" T2)
FN(f_CX4100k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 4100($t6)\n" T2)
FN(f_CX8196k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 8196($t6)\n" T2)
FN(f_CX16388k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 16388($t6)\n" T2)
FN(f_CCk0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" T2)
FN(f_CA4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 4($t6)\n" T2)
FN(f_CA8k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 8($t6)\n" T2)
FN(f_CA12k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 12($t6)\n" T2)
FN(f_CA16k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 16($t6)\n" T2)
FN(f_CA32k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 32($t6)\n" T2)
FN(f_CA64k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 64($t6)\n" T2)
FN(f_CA128k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 128($t6)\n" T2)
FN(f_CA256k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 256($t6)\n" T2)
FN(f_CA512k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 512($t6)\n" T2)
FN(f_CA1020k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1020($t6)\n" T2)
FN(f_CB0k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t7)\n" T2)
FN(f_CB4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 4($t7)\n" T2)
FN(f_CB8k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 8($t7)\n" T2)
FN(f_CB64k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 64($t7)\n" T2)
FN(f_CB512k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 512($t7)\n" T2)
FN(f_CB1020k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 1020($t7)\n" T2)
FN(f_CX2052k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 2052($t6)\n" T2)
FN(f_CX4100k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 4100($t6)\n" T2)
FN(f_CX8196k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 8196($t6)\n" T2)
FN(f_CX16388k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 16388($t6)\n" T2)
FN(f_CCk2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
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
    printf("\ncpu/ram-store-io6\n");
    printf("A<p><n>j<j>: n stores, lw GPUSTAT, j nops, lw GPUSTAT; B the second read RAM elsewhere; CA/CB/CX<d>k<k>:\n");
    printf("A, B, A+4, k nops, lw A+d / B+d / A+d far; CC lw elsewhere. Cycles, fastest..slowest, then the 16 in order.\n\n");
    printf("A %08X\n", (unsigned)(uint32_t)buf);
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("AP3j0", f_AP3j0);
    row("AP3j2", f_AP3j2);
    row("AP3j4", f_AP3j4);
    row("AP3j6", f_AP3j6);
    row("AP3j8", f_AP3j8);
    row("AP3j12", f_AP3j12);
    row("AP4j0", f_AP4j0);
    row("AP4j2", f_AP4j2);
    row("AP4j4", f_AP4j4);
    row("AP4j6", f_AP4j6);
    row("AP4j8", f_AP4j8);
    row("AP4j12", f_AP4j12);
    row("AP5j0", f_AP5j0);
    row("AP5j2", f_AP5j2);
    row("AP5j4", f_AP5j4);
    row("AP5j6", f_AP5j6);
    row("AP5j8", f_AP5j8);
    row("AP5j12", f_AP5j12);
    row("AS3j0", f_AS3j0);
    row("AS3j2", f_AS3j2);
    row("AS3j4", f_AS3j4);
    row("AS3j6", f_AS3j6);
    row("AS3j8", f_AS3j8);
    row("AS3j12", f_AS3j12);
    row("AS4j0", f_AS4j0);
    row("AS4j2", f_AS4j2);
    row("AS4j4", f_AS4j4);
    row("AS4j6", f_AS4j6);
    row("AS4j8", f_AS4j8);
    row("AS4j12", f_AS4j12);
    row("AS5j0", f_AS5j0);
    row("AS5j2", f_AS5j2);
    row("AS5j4", f_AS5j4);
    row("AS5j6", f_AS5j6);
    row("AS5j8", f_AS5j8);
    row("AS5j12", f_AS5j12);
    row("AQ3j0", f_AQ3j0);
    row("AQ3j2", f_AQ3j2);
    row("AQ3j4", f_AQ3j4);
    row("AQ3j6", f_AQ3j6);
    row("AQ3j8", f_AQ3j8);
    row("AQ3j12", f_AQ3j12);
    row("AQ4j0", f_AQ4j0);
    row("AQ4j2", f_AQ4j2);
    row("AQ4j4", f_AQ4j4);
    row("AQ4j6", f_AQ4j6);
    row("AQ4j8", f_AQ4j8);
    row("AQ4j12", f_AQ4j12);
    row("AQ5j0", f_AQ5j0);
    row("AQ5j2", f_AQ5j2);
    row("AQ5j4", f_AQ5j4);
    row("AQ5j6", f_AQ5j6);
    row("AQ5j8", f_AQ5j8);
    row("AQ5j12", f_AQ5j12);
    row("BP3j0", f_BP3j0);
    row("BP3j2", f_BP3j2);
    row("BP3j4", f_BP3j4);
    row("BP3j8", f_BP3j8);
    row("BP4j0", f_BP4j0);
    row("BP4j2", f_BP4j2);
    row("BP4j4", f_BP4j4);
    row("BP4j8", f_BP4j8);
    row("BP5j0", f_BP5j0);
    row("BP5j2", f_BP5j2);
    row("BP5j4", f_BP5j4);
    row("BP5j8", f_BP5j8);
    row("BQ3j0", f_BQ3j0);
    row("BQ3j2", f_BQ3j2);
    row("BQ3j4", f_BQ3j4);
    row("BQ3j8", f_BQ3j8);
    row("BQ4j0", f_BQ4j0);
    row("BQ4j2", f_BQ4j2);
    row("BQ4j4", f_BQ4j4);
    row("BQ4j8", f_BQ4j8);
    row("BQ5j0", f_BQ5j0);
    row("BQ5j2", f_BQ5j2);
    row("BQ5j4", f_BQ5j4);
    row("BQ5j8", f_BQ5j8);
    row("CA4k0", f_CA4k0);
    row("CA8k0", f_CA8k0);
    row("CA12k0", f_CA12k0);
    row("CA16k0", f_CA16k0);
    row("CA32k0", f_CA32k0);
    row("CA64k0", f_CA64k0);
    row("CA128k0", f_CA128k0);
    row("CA256k0", f_CA256k0);
    row("CA512k0", f_CA512k0);
    row("CA1020k0", f_CA1020k0);
    row("CB0k0", f_CB0k0);
    row("CB4k0", f_CB4k0);
    row("CB8k0", f_CB8k0);
    row("CB64k0", f_CB64k0);
    row("CB512k0", f_CB512k0);
    row("CB1020k0", f_CB1020k0);
    row("CX2052k0", f_CX2052k0);
    row("CX4100k0", f_CX4100k0);
    row("CX8196k0", f_CX8196k0);
    row("CX16388k0", f_CX16388k0);
    row("CCk0", f_CCk0);
    row("CA4k2", f_CA4k2);
    row("CA8k2", f_CA8k2);
    row("CA12k2", f_CA12k2);
    row("CA16k2", f_CA16k2);
    row("CA32k2", f_CA32k2);
    row("CA64k2", f_CA64k2);
    row("CA128k2", f_CA128k2);
    row("CA256k2", f_CA256k2);
    row("CA512k2", f_CA512k2);
    row("CA1020k2", f_CA1020k2);
    row("CB0k2", f_CB0k2);
    row("CB4k2", f_CB4k2);
    row("CB8k2", f_CB8k2);
    row("CB64k2", f_CB64k2);
    row("CB512k2", f_CB512k2);
    row("CB1020k2", f_CB1020k2);
    row("CX2052k2", f_CX2052k2);
    row("CX4100k2", f_CX4100k2);
    row("CX8196k2", f_CX8196k2);
    row("CX16388k2", f_CX16388k2);
    row("CCk2", f_CCk2);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-store-io6: Done.\n");
    for (;;) VSync(0);
}
