/*
 * cpu/ram-store-io7 - around the write queue's read priority: its two odd cells, a RAM read that goes first, and an
 * on-die read (2026-10-15 small hours).
 *
 * WHY. ram-store-io4/5/6 on the SCPH-1001: a read waiting when the bus comes free between two of the queue's
 * transactions goes first unless a queued store is in its own 64 bytes, and what it went ahead of keeps its place
 * (v0.377.113's wqReadWait). Three things it left: (1) io5's GP6k4 41 and OX4k2 34, each 10 over its own two twins (P,
 * R, X differ only in the order and distance of the two blocks), which no timing rule separates - their neighbours and
 * the same rows moved inside the blocks; (2) "keeps its place" was measured behind a GPUSTAT read, and for a RAM read
 * only fitted on the core - a second read after a RAM read that went first asks it; (3) an on-die read (the rows' own
 * closing Timer 2 read) goes first at +0 by ram-store-io2's FASTEST rows - by mode here, Timer 1's counter.
 *
 * WHAT IT DOES. ram-store-io5's harness (each row twice inside its own code, the second timed; 16 samples printed, 16
 * cycles apart over a 256-cycle refresh period). A 4 KB aligned, B = A + 1 KB, C = A + 2 KB, D = A + 4 KB.
 *   EG<p>6k<k>     6 stores (P alternating A / B, R B / A, X A / D), k nops, lw GPUSTAT; o16 / o512: the stores 16 or 512
 *                  bytes into their blocks
 *   EO<p>4k<k>     4 stores, k nops, lw C
 *   F<p><n>j<j>    n stores (P, Q: A, A+4, B, B+4, A+8), lw C, j nops, lw GPUSTAT
 *   T<p><n>k<k>    n stores (S one block, P, Q), k nops, lhu Timer 1's counter
 *
 * READ IT LIKE THIS. Cycles: fastest..slowest, then the 16 in order; the most common is the undisturbed value. No
 * assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-store-io7 --ntsc`.
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
FN(f_EGP6k3, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(3) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGP6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGP6k5, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(5) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGP6k6, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOP4k1, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(1) "lw $t3, 0($t5)\n" T2)
FN(f_EOP4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EOP4k3, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(3) "lw $t3, 0($t5)\n" T2)
FN(f_EGP6k4o16, T1 "sw $zero, 16($t6)\n" "sw $zero, 16($t7)\n" "sw $zero, 20($t6)\n" "sw $zero, 20($t7)\n" "sw $zero, 24($t6)\n" "sw $zero, 24($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOP4k2o16, T1 "sw $zero, 16($t6)\n" "sw $zero, 16($t7)\n" "sw $zero, 20($t6)\n" "sw $zero, 20($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EGP6k4o512, T1 "sw $zero, 512($t6)\n" "sw $zero, 512($t7)\n" "sw $zero, 516($t6)\n" "sw $zero, 516($t7)\n" "sw $zero, 520($t6)\n" "sw $zero, 520($t7)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOP4k2o512, T1 "sw $zero, 512($t6)\n" "sw $zero, 512($t7)\n" "sw $zero, 516($t6)\n" "sw $zero, 516($t7)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EGR6k3, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(3) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGR6k4, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGR6k5, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(5) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGR6k6, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t7)\n" "sw $zero, 8($t6)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOR4k1, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(1) "lw $t3, 0($t5)\n" T2)
FN(f_EOR4k2, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EOR4k3, T1 "sw $zero, 0($t7)\n" "sw $zero, 0($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 4($t6)\n" NOPS(3) "lw $t3, 0($t5)\n" T2)
FN(f_EGR6k4o16, T1 "sw $zero, 16($t7)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t7)\n" "sw $zero, 20($t6)\n" "sw $zero, 24($t7)\n" "sw $zero, 24($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOR4k2o16, T1 "sw $zero, 16($t7)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t7)\n" "sw $zero, 20($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EGR6k4o512, T1 "sw $zero, 512($t7)\n" "sw $zero, 512($t6)\n" "sw $zero, 516($t7)\n" "sw $zero, 516($t6)\n" "sw $zero, 520($t7)\n" "sw $zero, 520($t6)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOR4k2o512, T1 "sw $zero, 512($t7)\n" "sw $zero, 512($t6)\n" "sw $zero, 516($t7)\n" "sw $zero, 516($t6)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EGX6k3, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(3) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGX6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGX6k5, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(5) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EGX6k6, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t4)\n" NOPS(6) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOX4k1, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(1) "lw $t3, 0($t5)\n" T2)
FN(f_EOX4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EOX4k3, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t4)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t4)\n" NOPS(3) "lw $t3, 0($t5)\n" T2)
FN(f_EGX6k4o16, T1 "sw $zero, 16($t6)\n" "sw $zero, 16($t4)\n" "sw $zero, 20($t6)\n" "sw $zero, 20($t4)\n" "sw $zero, 24($t6)\n" "sw $zero, 24($t4)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOX4k2o16, T1 "sw $zero, 16($t6)\n" "sw $zero, 16($t4)\n" "sw $zero, 20($t6)\n" "sw $zero, 20($t4)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_EGX6k4o512, T1 "sw $zero, 512($t6)\n" "sw $zero, 512($t4)\n" "sw $zero, 516($t6)\n" "sw $zero, 516($t4)\n" "sw $zero, 520($t6)\n" "sw $zero, 520($t4)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_EOX4k2o512, T1 "sw $zero, 512($t6)\n" "sw $zero, 512($t4)\n" "sw $zero, 516($t6)\n" "sw $zero, 516($t4)\n" NOPS(2) "lw $t3, 0($t5)\n" T2)
FN(f_FP3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lw $t3, 0($t5)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FP5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ3j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ3j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ3j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ3j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lw $t3, 0($t5)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ4j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ4j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ4j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ4j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lw $t3, 0($t5)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ5j0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ5j2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" NOPS(2) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ5j4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" NOPS(4) "lw $t3, 0x1814($t9)\n" T2)
FN(f_FQ5j8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lw $t3, 0($t5)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_TS2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TS6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "sw $zero, 16($t6)\n" "sw $zero, 20($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TP6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t6)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 8($t7)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ2k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ2k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ2k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ2k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ3k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ3k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ3k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ3k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ4k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ4k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ4k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ4k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ5k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ5k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ5k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ5k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ6k0, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ6k2, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(2) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ6k4, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(4) "lhu $t3, 0x1110($t9)\n" T2)
FN(f_TQ6k8, T1 "sw $zero, 0($t6)\n" "sw $zero, 4($t6)\n" "sw $zero, 0($t7)\n" "sw $zero, 4($t7)\n" "sw $zero, 8($t6)\n" "sw $zero, 12($t6)\n" NOPS(8) "lhu $t3, 0x1110($t9)\n" T2)
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
    printf("\ncpu/ram-store-io7\n");
    printf("EG/EO: P/R/X stores, k nops, lw GPUSTAT / RAM (o16/o512 moved into the blocks); F: stores, lw RAM, j nops,\n");
    printf("lw GPUSTAT; T: stores, k nops, lhu Timer 1. Cycles, fastest..slowest, then the 16 in order.\n\n");
    printf("A %08X\n", (unsigned)(uint32_t)buf);
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("EGP6k3", f_EGP6k3);
    row("EGP6k4", f_EGP6k4);
    row("EGP6k5", f_EGP6k5);
    row("EGP6k6", f_EGP6k6);
    row("EOP4k1", f_EOP4k1);
    row("EOP4k2", f_EOP4k2);
    row("EOP4k3", f_EOP4k3);
    row("EGP6k4o16", f_EGP6k4o16);
    row("EOP4k2o16", f_EOP4k2o16);
    row("EGP6k4o512", f_EGP6k4o512);
    row("EOP4k2o512", f_EOP4k2o512);
    row("EGR6k3", f_EGR6k3);
    row("EGR6k4", f_EGR6k4);
    row("EGR6k5", f_EGR6k5);
    row("EGR6k6", f_EGR6k6);
    row("EOR4k1", f_EOR4k1);
    row("EOR4k2", f_EOR4k2);
    row("EOR4k3", f_EOR4k3);
    row("EGR6k4o16", f_EGR6k4o16);
    row("EOR4k2o16", f_EOR4k2o16);
    row("EGR6k4o512", f_EGR6k4o512);
    row("EOR4k2o512", f_EOR4k2o512);
    row("EGX6k3", f_EGX6k3);
    row("EGX6k4", f_EGX6k4);
    row("EGX6k5", f_EGX6k5);
    row("EGX6k6", f_EGX6k6);
    row("EOX4k1", f_EOX4k1);
    row("EOX4k2", f_EOX4k2);
    row("EOX4k3", f_EOX4k3);
    row("EGX6k4o16", f_EGX6k4o16);
    row("EOX4k2o16", f_EOX4k2o16);
    row("EGX6k4o512", f_EGX6k4o512);
    row("EOX4k2o512", f_EOX4k2o512);
    row("FP3j0", f_FP3j0);
    row("FP3j2", f_FP3j2);
    row("FP3j4", f_FP3j4);
    row("FP3j8", f_FP3j8);
    row("FP4j0", f_FP4j0);
    row("FP4j2", f_FP4j2);
    row("FP4j4", f_FP4j4);
    row("FP4j8", f_FP4j8);
    row("FP5j0", f_FP5j0);
    row("FP5j2", f_FP5j2);
    row("FP5j4", f_FP5j4);
    row("FP5j8", f_FP5j8);
    row("FQ3j0", f_FQ3j0);
    row("FQ3j2", f_FQ3j2);
    row("FQ3j4", f_FQ3j4);
    row("FQ3j8", f_FQ3j8);
    row("FQ4j0", f_FQ4j0);
    row("FQ4j2", f_FQ4j2);
    row("FQ4j4", f_FQ4j4);
    row("FQ4j8", f_FQ4j8);
    row("FQ5j0", f_FQ5j0);
    row("FQ5j2", f_FQ5j2);
    row("FQ5j4", f_FQ5j4);
    row("FQ5j8", f_FQ5j8);
    row("TS2k0", f_TS2k0);
    row("TS2k2", f_TS2k2);
    row("TS2k4", f_TS2k4);
    row("TS2k8", f_TS2k8);
    row("TS3k0", f_TS3k0);
    row("TS3k2", f_TS3k2);
    row("TS3k4", f_TS3k4);
    row("TS3k8", f_TS3k8);
    row("TS4k0", f_TS4k0);
    row("TS4k2", f_TS4k2);
    row("TS4k4", f_TS4k4);
    row("TS4k8", f_TS4k8);
    row("TS5k0", f_TS5k0);
    row("TS5k2", f_TS5k2);
    row("TS5k4", f_TS5k4);
    row("TS5k8", f_TS5k8);
    row("TS6k0", f_TS6k0);
    row("TS6k2", f_TS6k2);
    row("TS6k4", f_TS6k4);
    row("TS6k8", f_TS6k8);
    row("TP2k0", f_TP2k0);
    row("TP2k2", f_TP2k2);
    row("TP2k4", f_TP2k4);
    row("TP2k8", f_TP2k8);
    row("TP3k0", f_TP3k0);
    row("TP3k2", f_TP3k2);
    row("TP3k4", f_TP3k4);
    row("TP3k8", f_TP3k8);
    row("TP4k0", f_TP4k0);
    row("TP4k2", f_TP4k2);
    row("TP4k4", f_TP4k4);
    row("TP4k8", f_TP4k8);
    row("TP5k0", f_TP5k0);
    row("TP5k2", f_TP5k2);
    row("TP5k4", f_TP5k4);
    row("TP5k8", f_TP5k8);
    row("TP6k0", f_TP6k0);
    row("TP6k2", f_TP6k2);
    row("TP6k4", f_TP6k4);
    row("TP6k8", f_TP6k8);
    row("TQ2k0", f_TQ2k0);
    row("TQ2k2", f_TQ2k2);
    row("TQ2k4", f_TQ2k4);
    row("TQ2k8", f_TQ2k8);
    row("TQ3k0", f_TQ3k0);
    row("TQ3k2", f_TQ3k2);
    row("TQ3k4", f_TQ3k4);
    row("TQ3k8", f_TQ3k8);
    row("TQ4k0", f_TQ4k0);
    row("TQ4k2", f_TQ4k2);
    row("TQ4k4", f_TQ4k4);
    row("TQ4k8", f_TQ4k8);
    row("TQ5k0", f_TQ5k0);
    row("TQ5k2", f_TQ5k2);
    row("TQ5k4", f_TQ5k4);
    row("TQ5k8", f_TQ5k8);
    row("TQ6k0", f_TQ6k0);
    row("TQ6k2", f_TQ6k2);
    row("TQ6k4", f_TQ6k4);
    row("TQ6k8", f_TQ6k8);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-store-io7: Done.\n");
    for (;;) VSync(0);
}
