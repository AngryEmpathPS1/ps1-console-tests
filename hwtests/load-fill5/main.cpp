/*
 * cpu/load-fill5 - load-fill4's cold shapes as grids: where in its line a load sits against how far behind it its
 * reader, its second load or its store comes (2026-10-10)
 *
 * WHY. A model of code that misses the instruction cache (scratch/patch_v74.js; STATUS 2026-10-10 evening) reads load-fill
 * 1-4 to 1.27 cycles RMS, and what it misses comes in one-cycle families tied to the word a load sits at: a reader at
 * word 3 costs 7.13 behind a load at word 0 and 6.09 behind one at word 1 (load-fill4), the opposite of every bus rule
 * tried. A grid shows the shape where a few rows could not.
 *
 * WHAT IT DOES. As load-fill4: 128 units of 16 instructions (four lines, twice the cache), Timer 2, interrupts off,
 * fastest..slowest of four passes, each row a unit's cost above the nops block. Names: the first access at word w of the
 * unit's first line (L a RAM load into t1, I an I_STAT load into t1), then "U+d" an addu of t1 d instructions later, "I+d"
 * / "L+d" a second load into t2, "S+d" a store to a word no load reads.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js load-fill5`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
uint32_t ramWords[32];
typedef uint32_t (*Block)(uint32_t, uint32_t);
#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\n.set pop\n"
#define BLOCK(NAME, ...) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p, uint32_t q) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" __VA_ARGS__ ".endr\n" TAIL : "=&r"(r) : "r"(p), "r"(q) : "t1", "t2", "t3", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }

BLOCK(nops, ".rept 16\nnop\n.endr\n")
BLOCK(b0, "lw $t1, 0(%1)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 U+2 */
BLOCK(b1, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 U+3 */
BLOCK(b2, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 U+4 */
BLOCK(b3, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 U+5 */
BLOCK(b4, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 U+6 */
BLOCK(b5, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 U+2 */
BLOCK(b6, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 U+3 */
BLOCK(b7, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 U+4 */
BLOCK(b8, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 U+5 */
BLOCK(b9, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 U+6 */
BLOCK(b10, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 U+2 */
BLOCK(b11, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 U+3 */
BLOCK(b12, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 U+4 */
BLOCK(b13, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 U+5 */
BLOCK(b14, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 U+6 */
BLOCK(b15, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 U+2 */
BLOCK(b16, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 U+3 */
BLOCK(b17, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 U+4 */
BLOCK(b18, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 U+5 */
BLOCK(b19, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 U+6 */
BLOCK(b20, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+1 */
BLOCK(b21, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 */
BLOCK(b22, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+3 */
BLOCK(b23, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+1 */
BLOCK(b24, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+2 */
BLOCK(b25, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+3 */
BLOCK(b26, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 I+1 */
BLOCK(b27, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 I+2 */
BLOCK(b28, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 I+3 */
BLOCK(b29, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+1 */
BLOCK(b30, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+2 */
BLOCK(b31, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 */
BLOCK(b32, "lw $t1, 0(%2)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 U+2 */
BLOCK(b33, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 U+3 */
BLOCK(b34, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 U+4 */
BLOCK(b35, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 U+2 */
BLOCK(b36, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 U+3 */
BLOCK(b37, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 U+4 */
BLOCK(b38, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 U+2 */
BLOCK(b39, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 U+3 */
BLOCK(b40, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 U+4 */
BLOCK(b41, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 U+2 */
BLOCK(b42, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 U+3 */
BLOCK(b43, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 U+4 */
BLOCK(b44, "lw $t1, 0(%1)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 S+1 */
BLOCK(b45, "lw $t1, 0(%1)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 S+2 */
BLOCK(b46, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 S+3 */
BLOCK(b47, "nop\n" "lw $t1, 0(%1)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 S+1 */
BLOCK(b48, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 S+2 */
BLOCK(b49, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 S+3 */
BLOCK(b50, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 S+1 */
BLOCK(b51, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 S+2 */
BLOCK(b52, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 S+3 */
BLOCK(b53, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 S+1 */
BLOCK(b54, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 S+2 */
BLOCK(b55, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 S+3 */
BLOCK(b56, "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 L+1 */
BLOCK(b57, "lw $t1, 0(%1)\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L0 L+2 */
BLOCK(b58, "nop\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 L+1 */
BLOCK(b59, "nop\n" "lw $t1, 0(%1)\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L1 L+2 */
BLOCK(b60, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 L+1 */
BLOCK(b61, "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L2 L+2 */
BLOCK(b62, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 L+1 */
BLOCK(b63, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L3 L+2 */

static const Block B[64] = { b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25, b26, b27, b28, b29, b30, b31, b32, b33, b34, b35, b36, b37, b38, b39, b40, b41, b42, b43, b44, b45, b46, b47, b48, b49, b50, b51, b52, b53, b54, b55, b56, b57, b58, b59, b60, b61, b62, b63 };
static const char *const NAME[64] = { "L0 U+2", "L0 U+3", "L0 U+4", "L0 U+5", "L0 U+6", "L1 U+2", "L1 U+3", "L1 U+4", "L1 U+5", "L1 U+6", "L2 U+2", "L2 U+3", "L2 U+4", "L2 U+5", "L2 U+6", "L3 U+2", "L3 U+3", "L3 U+4", "L3 U+5", "L3 U+6", "I0 I+1", "I0 I+2", "I0 I+3", "I1 I+1", "I1 I+2", "I1 I+3", "I2 I+1", "I2 I+2", "I2 I+3", "I3 I+1", "I3 I+2", "I3 I+3", "I0 U+2", "I0 U+3", "I0 U+4", "I1 U+2", "I1 U+3", "I1 U+4", "I2 U+2", "I2 U+3", "I2 U+4", "I3 U+2", "I3 U+3", "I3 U+4", "L0 S+1", "L0 S+2", "L0 S+3", "L1 S+1", "L1 S+2", "L1 S+3", "L2 S+1", "L2 S+2", "L2 S+3", "L3 S+1", "L3 S+2", "L3 S+3", "L0 L+1", "L0 L+2", "L1 L+1", "L1 L+2", "L2 L+1", "L2 L+2", "L3 L+1", "L3 L+2" };

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("\ncpu/load-fill5\n");
    printf("Cold grids: the first access at word w of its line, the reader / second load / store d instructions behind.\n");
    printf("Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(nops);
    for (int i = 0; i < 64; i++) {
        const Span s = time4(B[i]);
        const int d = (int)s.lo - (int)base.lo;
        const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
        printf("  %-14s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit\n", NAME[i], (unsigned)s.lo, (unsigned)s.hi, d, d < 0 ? "-" : "", per / 100, per % 100);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
