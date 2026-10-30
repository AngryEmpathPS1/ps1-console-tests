/*
 * cpu/io-pair2 - load-fill5's I_STAT pairs I3 I+3 and I0 I+2 with each kind of instruction after the second load, a
 * not-taken branch at the line's first word or not, and cold-bracket's own shape (2026-10-11)
 *
 * WHY. v0.377.77 (src/psx.js, "A LOAD BEHIND AN ON-DIE READ") charges a load behind an on-die read one cycle more at
 * word 2 or 3 of its line when the next line is not cached (CB_IPW): load-fill5's I1 I+1, I0 I+2 and I3 I+3 read 3 4 4
 * on the 9001, 2 3 3 without it. But cold-bracket's joy1 w2 - its counter read at word 2, three behind a JOY_STAT read
 * in the line before, a Timer 2 mode read after it and a not-taken bnez at the line's word 0 - reads 20 on the
 * consoles, 21 with that cycle. The shapes differ in what follows the second load, the branch, and which registers are
 * read. Each is varied here alone.
 *
 * WHAT IT DOES. As load-fill5: 128 units of 16 instructions (four lines, twice the cache, so every line misses), Timer
 * 2, interrupts off, fastest..slowest of four passes, each row a unit's cost above the nops block. Names: the first load
 * and its word (I an I_STAT word load, J a JOY_STAT byte load, T Timer 2's counter, halfword), then "+d" the second d
 * on, then "br" when the second load's line starts with a not-taken bne, then what follows the second load: n a nop, I3
 * another I_STAT load (a third register), R a RAM load, S a RAM store, A an addu, M Timer 2's mode (halfword).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js io-pair2`.
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
    __asm__ volatile(HEAD ".rept 128\n" __VA_ARGS__ ".endr\n" TAIL : "=&r"(r) : "r"(p), "r"(q) : "t1", "t2", "t3", "t4", "t5", "t7", "t8", "t9", "a0", "memory"); \
    return r & 0xFFFF; }

BLOCK(nops, ".rept 16\nnop\n.endr\n")
BLOCK(b0, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 n */
BLOCK(b1, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "lw $t3, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 I3 */
BLOCK(b2, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "lw $t3, 8(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 R */
BLOCK(b3, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 S */
BLOCK(b4, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "addu $t4, $t5, $t5\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 A */
BLOCK(b5, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t2, 0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 M */
BLOCK(b6, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 br n */
BLOCK(b7, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lw $t2, 0(%2)\n" "lw $t3, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 br I3 */
BLOCK(b8, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lw $t2, 0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 I+3 br M */
BLOCK(b9, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "nop\n" "nop\n" "lhu $t2, 0xb0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 T+3 n */
BLOCK(b10, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "nop\n" "nop\n" "lhu $t2, 0xb0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 T+3 M */
BLOCK(b11, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "nop\n" "nop\n" "lhu $t2, 0xb0(%2)\n" "lw $t3, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 T+3 I3 */
BLOCK(b12, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lhu $t2, 0xb0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 T+3 br n */
BLOCK(b13, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lhu $t2, 0xb0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 T+3 br M */
BLOCK(b14, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lhu $t2, 0xb0(%2)\n" "lw $t3, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 T+3 br I3 */
BLOCK(b15, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lhu $t2, 0xb0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 T+3 br n */
BLOCK(b16, "nop\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lhu $t2, 0xb0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I3 T+3 br M */
BLOCK(b17, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 I+3 br n */
BLOCK(b18, "nop\n" "nop\n" "nop\n" "lbu $a0, -0x2c(%2)\n" "bne $zero, $zero, 1f\n" "nop\n" "1:\n" "lw $t2, 0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J3 I+3 br M */
BLOCK(b19, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 n */
BLOCK(b20, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "lw $t3, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 I3 */
BLOCK(b21, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "lw $t3, 8(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 R */
BLOCK(b22, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 S */
BLOCK(b23, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "addu $t4, $t5, $t5\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 A */
BLOCK(b24, "lw $t1, 0(%2)\n" "nop\n" "lw $t2, 0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I0 I+2 M */
BLOCK(b25, "lbu $a0, -0x2c(%2)\n" "nop\n" "lhu $t2, 0xb0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J0 T+2 n */
BLOCK(b26, "lbu $a0, -0x2c(%2)\n" "nop\n" "lhu $t2, 0xb0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* J0 T+2 M */
BLOCK(b27, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+1 n */
BLOCK(b28, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t3, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+1 I3 */
BLOCK(b29, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lw $t3, 8(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+1 R */
BLOCK(b30, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+1 S */
BLOCK(b31, "nop\n" "lw $t1, 0(%2)\n" "lw $t2, 0(%2)\n" "lhu $t3, 0xb4(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I1 I+1 M */

static const Block B[32] = { b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25, b26, b27, b28, b29, b30, b31 };
static const char *const NAME[32] = { "I3 I+3 n", "I3 I+3 I3", "I3 I+3 R", "I3 I+3 S", "I3 I+3 A", "I3 I+3 M", "I3 I+3 br n", "I3 I+3 br I3", "I3 I+3 br M", "J3 T+3 n", "J3 T+3 M", "J3 T+3 I3", "J3 T+3 br n", "J3 T+3 br M", "J3 T+3 br I3", "I3 T+3 br n", "I3 T+3 br M", "J3 I+3 br n", "J3 I+3 br M", "I0 I+2 n", "I0 I+2 I3", "I0 I+2 R", "I0 I+2 S", "I0 I+2 A", "I0 I+2 M", "J0 T+2 n", "J0 T+2 M", "I1 I+1 n", "I1 I+1 I3", "I1 I+1 R", "I1 I+1 S", "I1 I+1 M" };

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("\ncpu/io-pair2\n");
    printf("I_STAT pairs at word 2 with what follows, a not-taken branch, and cold-bracket's JOY/Timer 2 shape.\n");
    printf("Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(nops);
    for (int i = 0; i < 32; i++) {
        const Span s = time4(B[i]);
        const int d = (int)s.lo - (int)base.lo;
        const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
        printf("  %-16s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit\n", NAME[i], (unsigned)s.lo, (unsigned)s.hi, d, d < 0 ? "-" : "", per / 100, per % 100);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
