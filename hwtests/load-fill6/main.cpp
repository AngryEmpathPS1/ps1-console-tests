/*
 * cpu/load-fill6 - load-fill5's grids for stores: a store at each word of its line against what comes behind it - a second
 * store, a RAM load, an I_STAT load - and an I_STAT load with a store behind it (2026-10-10 night)
 *
 * WHY. The cold-stream model (scratch/coldbus/patch_v75.js; STATUS 2026-10-10 night) fits load-fill5's reader grid by one
 * rule, and misses stores: three in a line one burst (9.30) where its burst rule gives 11.8, S0 L1 and S3 L4 1.3 high,
 * an I_STAT load behind a store 1.3 high. A grid shows the shape.
 *
 * WHAT IT DOES. As load-fill5: 128 units of 16 instructions, Timer 2, interrupts off, fastest..slowest of four passes,
 * each row a unit's cost above the nops block. Names: S a store (words 4 and 5 of a buffer no load reads), L a RAM load
 * into t1, I an I_STAT load into t1, at word w of the unit's first line; "+d" the second access d instructions later.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js load-fill6`.
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
BLOCK(b0, "sw $zero, 16(%1)\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 S+1 */
BLOCK(b1, "sw $zero, 16(%1)\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 S+2 */
BLOCK(b2, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 S+3 */
BLOCK(b3, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 S+4 */
BLOCK(b4, "nop\n" "sw $zero, 16(%1)\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 S+1 */
BLOCK(b5, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 S+2 */
BLOCK(b6, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 S+3 */
BLOCK(b7, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 S+4 */
BLOCK(b8, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 S+1 */
BLOCK(b9, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 S+2 */
BLOCK(b10, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 S+3 */
BLOCK(b11, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 S+4 */
BLOCK(b12, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 S+1 */
BLOCK(b13, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 S+2 */
BLOCK(b14, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 S+3 */
BLOCK(b15, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 S+4 */
BLOCK(b16, "sw $zero, 16(%1)\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 L+1 */
BLOCK(b17, "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 L+2 */
BLOCK(b18, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 L+3 */
BLOCK(b19, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 L+4 */
BLOCK(b20, "nop\n" "sw $zero, 16(%1)\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 L+1 */
BLOCK(b21, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 L+2 */
BLOCK(b22, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 L+3 */
BLOCK(b23, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 L+4 */
BLOCK(b24, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 L+1 */
BLOCK(b25, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 L+2 */
BLOCK(b26, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 L+3 */
BLOCK(b27, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 L+4 */
BLOCK(b28, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 L+1 */
BLOCK(b29, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 L+2 */
BLOCK(b30, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 L+3 */
BLOCK(b31, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 L+4 */
BLOCK(b32, "sw $zero, 16(%1)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 I+1 */
BLOCK(b33, "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 I+2 */
BLOCK(b34, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 I+3 */
BLOCK(b35, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S0 I+4 */
BLOCK(b36, "nop\n" "sw $zero, 16(%1)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 I+1 */
BLOCK(b37, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 I+2 */
BLOCK(b38, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 I+3 */
BLOCK(b39, "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S1 I+4 */
BLOCK(b40, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 I+1 */
BLOCK(b41, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 I+2 */
BLOCK(b42, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 I+3 */
BLOCK(b43, "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S2 I+4 */
BLOCK(b44, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 I+1 */
BLOCK(b45, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 I+2 */
BLOCK(b46, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 I+3 */
BLOCK(b47, "nop\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S3 I+4 */
BLOCK(b48, "lw $t1, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 S+1 */
BLOCK(b49, "lw $t1, 0(%2)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 S+2 */
BLOCK(b50, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 S+3 */
BLOCK(b51, "nop\n" "lw $t1, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 S+1 */
BLOCK(b52, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 S+2 */
BLOCK(b53, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 S+3 */
BLOCK(b54, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 S+1 */
BLOCK(b55, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 S+2 */
BLOCK(b56, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I2 S+3 */
BLOCK(b57, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 S+1 */
BLOCK(b58, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 S+2 */
BLOCK(b59, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 S+3 */

static const Block B[60] = { b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25, b26, b27, b28, b29, b30, b31, b32, b33, b34, b35, b36, b37, b38, b39, b40, b41, b42, b43, b44, b45, b46, b47, b48, b49, b50, b51, b52, b53, b54, b55, b56, b57, b58, b59 };
static const char *const NAME[60] = { "S0 S+1", "S0 S+2", "S0 S+3", "S0 S+4", "S1 S+1", "S1 S+2", "S1 S+3", "S1 S+4", "S2 S+1", "S2 S+2", "S2 S+3", "S2 S+4", "S3 S+1", "S3 S+2", "S3 S+3", "S3 S+4", "S0 L+1", "S0 L+2", "S0 L+3", "S0 L+4", "S1 L+1", "S1 L+2", "S1 L+3", "S1 L+4", "S2 L+1", "S2 L+2", "S2 L+3", "S2 L+4", "S3 L+1", "S3 L+2", "S3 L+3", "S3 L+4", "S0 I+1", "S0 I+2", "S0 I+3", "S0 I+4", "S1 I+1", "S1 I+2", "S1 I+3", "S1 I+4", "S2 I+1", "S2 I+2", "S2 I+3", "S2 I+4", "S3 I+1", "S3 I+2", "S3 I+3", "S3 I+4", "I0 S+1", "I0 S+2", "I0 S+3", "I1 S+1", "I1 S+2", "I1 S+3", "I2 S+1", "I2 S+2", "I2 S+3", "I3 S+1", "I3 S+2", "I3 S+3" };

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("\ncpu/load-fill6\n");
    printf("Cold store grids: a store (or I_STAT load) at word w of its line, the second access d instructions behind.\n");
    printf("Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(nops);
    for (int i = 0; i < 60; i++) {
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
