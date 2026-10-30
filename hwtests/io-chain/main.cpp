/*
 * cpu/io-chain - chains of on-die loads in code that misses the cache: n = 1..8 I_STAT loads back to back from word w of
 * the line, into one register or two alternately (2026-10-11 night)
 *
 * WHY. v0.377.75's stream (src/psx.js, "IN CODE THAT MISSES THE INSTRUCTION CACHE") fits 488 console rows to 0.98 cycles
 * RMS, and its worst misses are chains of I_STAT loads: hwtests/load-fill3's sixteen to a unit 71.59 on the 9001, 63.65
 * here, and every second word 39.67 against 36.31 - where load-fill5's pairs fit. The chain costs more than its pairs.
 * A grid of chain lengths at each word of the line shows where.
 *
 * WHAT IT DOES. As load-fill5: 128 units of 16 instructions, Timer 2, interrupts off, fastest..slowest of four passes,
 * each row a unit's cost above the nops block. ISAME: lw I_STAT into t1, n of them from word w of the unit's first line;
 * IDIFF: into t1 and t2 alternately. The rest of the unit nops.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js io-chain`.
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
BLOCK(b0, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x1 */
BLOCK(b1, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x2 */
BLOCK(b2, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x3 */
BLOCK(b3, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x4 */
BLOCK(b4, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x5 */
BLOCK(b5, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x6 */
BLOCK(b6, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x7 */
BLOCK(b7, "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w0 x8 */
BLOCK(b8, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x1 */
BLOCK(b9, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x2 */
BLOCK(b10, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x3 */
BLOCK(b11, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x4 */
BLOCK(b12, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x5 */
BLOCK(b13, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x6 */
BLOCK(b14, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x7 */
BLOCK(b15, "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w1 x8 */
BLOCK(b16, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x1 */
BLOCK(b17, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x2 */
BLOCK(b18, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x3 */
BLOCK(b19, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x4 */
BLOCK(b20, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x5 */
BLOCK(b21, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x6 */
BLOCK(b22, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x7 */
BLOCK(b23, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w2 x8 */
BLOCK(b24, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x1 */
BLOCK(b25, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x2 */
BLOCK(b26, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x3 */
BLOCK(b27, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x4 */
BLOCK(b28, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x5 */
BLOCK(b29, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x6 */
BLOCK(b30, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x7 */
BLOCK(b31, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* ISAME w3 x8 */
BLOCK(b32, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x1 */
BLOCK(b33, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x2 */
BLOCK(b34, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x3 */
BLOCK(b35, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x4 */
BLOCK(b36, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x5 */
BLOCK(b37, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x6 */
BLOCK(b38, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x7 */
BLOCK(b39, "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w0 x8 */
BLOCK(b40, "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x1 */
BLOCK(b41, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x2 */
BLOCK(b42, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x3 */
BLOCK(b43, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x4 */
BLOCK(b44, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x5 */
BLOCK(b45, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x6 */
BLOCK(b46, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x7 */
BLOCK(b47, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w1 x8 */
BLOCK(b48, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x1 */
BLOCK(b49, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x2 */
BLOCK(b50, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x3 */
BLOCK(b51, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x4 */
BLOCK(b52, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x5 */
BLOCK(b53, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x6 */
BLOCK(b54, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x7 */
BLOCK(b55, "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w2 x8 */
BLOCK(b56, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x1 */
BLOCK(b57, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x2 */
BLOCK(b58, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x3 */
BLOCK(b59, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x4 */
BLOCK(b60, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x5 */
BLOCK(b61, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x6 */
BLOCK(b62, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x7 */
BLOCK(b63, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* IDIFF w3 x8 */

static const Block B[64] = { b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25, b26, b27, b28, b29, b30, b31, b32, b33, b34, b35, b36, b37, b38, b39, b40, b41, b42, b43, b44, b45, b46, b47, b48, b49, b50, b51, b52, b53, b54, b55, b56, b57, b58, b59, b60, b61, b62, b63 };
static const char *const NAME[64] = { "ISAME w0 x1", "ISAME w0 x2", "ISAME w0 x3", "ISAME w0 x4", "ISAME w0 x5", "ISAME w0 x6", "ISAME w0 x7", "ISAME w0 x8", "ISAME w1 x1", "ISAME w1 x2", "ISAME w1 x3", "ISAME w1 x4", "ISAME w1 x5", "ISAME w1 x6", "ISAME w1 x7", "ISAME w1 x8", "ISAME w2 x1", "ISAME w2 x2", "ISAME w2 x3", "ISAME w2 x4", "ISAME w2 x5", "ISAME w2 x6", "ISAME w2 x7", "ISAME w2 x8", "ISAME w3 x1", "ISAME w3 x2", "ISAME w3 x3", "ISAME w3 x4", "ISAME w3 x5", "ISAME w3 x6", "ISAME w3 x7", "ISAME w3 x8", "IDIFF w0 x1", "IDIFF w0 x2", "IDIFF w0 x3", "IDIFF w0 x4", "IDIFF w0 x5", "IDIFF w0 x6", "IDIFF w0 x7", "IDIFF w0 x8", "IDIFF w1 x1", "IDIFF w1 x2", "IDIFF w1 x3", "IDIFF w1 x4", "IDIFF w1 x5", "IDIFF w1 x6", "IDIFF w1 x7", "IDIFF w1 x8", "IDIFF w2 x1", "IDIFF w2 x2", "IDIFF w2 x3", "IDIFF w2 x4", "IDIFF w2 x5", "IDIFF w2 x6", "IDIFF w2 x7", "IDIFF w2 x8", "IDIFF w3 x1", "IDIFF w3 x2", "IDIFF w3 x3", "IDIFF w3 x4", "IDIFF w3 x5", "IDIFF w3 x6", "IDIFF w3 x7", "IDIFF w3 x8" };

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("\ncpu/io-chain\n");
    printf("Cold I_STAT chains: n loads back to back from word w of the line, one register (ISAME) or two (IDIFF).\n");
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
