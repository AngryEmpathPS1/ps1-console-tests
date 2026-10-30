/*
 * cpu/unc-load - a RAM load in UNCACHED code: alone, in pairs and runs, with its reader, beside I_STAT loads and stores
 * (2026-10-12 night)
 *
 * WHY. Two measurements part. nocash's DIAG (MEM ACCESS TIMING, tests/nocash-diag/DIAG.A22 diag_mem_access_timing) times
 * sixteen `lw` from one RAM word back to back in uncached code and wants 0Ch cycles each - this core gives 12. hwtests/
 * load-fill's UNCACHED row times ONE `lw` among fifteen nops: 4.30 cycles over the nops on every one of four
 * consoles, where this core charges 6.09 (12 less a nop's 6). Both can hold - a lone load's bus access overlapping the
 * fetches around it where a run of loads cannot - but no row says how. This times the shapes between the two.
 *
 * WHAT IT DOES. load-fill's method run from KSEG1 (every instruction a RAM read): 128 units of 16 instructions, Timer 2
 * at the system clock, interrupts off, fastest..slowest of four passes, each row a unit's cost above the nops block run
 * the same way. Names: L a RAM load (t1 / t2, two words of a buffer), I an I_STAT load into t1, S a store (a word no load
 * reads), U a reader of t1 (`addu t3, t1, t1`); "+d" the second instruction d after the first; LL, 4L, 8L, 16L loads
 * back to back.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js unc-load`.
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
BLOCK(b0, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L */
BLOCK(b1, "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+1 */
BLOCK(b2, "lw $t1, 0(%1)\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+2 */
BLOCK(b3, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+3 */
BLOCK(b4, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+4 */
BLOCK(b5, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+5 */
BLOCK(b6, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+6 */
BLOCK(b7, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L L+8 */
BLOCK(b8, "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* LL */
BLOCK(b9, "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* 4L */
BLOCK(b10, "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* 8L */
BLOCK(b11, "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n" "lw $t1, 0(%1)\n" "lw $t2, 4(%1)\n") /* 16L */
BLOCK(b12, "lw $t1, 0(%1)\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L U+1 */
BLOCK(b13, "lw $t1, 0(%1)\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L U+2 */
BLOCK(b14, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L U+3 */
BLOCK(b15, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "addu $t3, $t1, $t1\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L U+4 */
BLOCK(b16, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I */
BLOCK(b17, "lw $t1, 0(%1)\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L I+1 */
BLOCK(b18, "lw $t1, 0(%1)\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L I+2 */
BLOCK(b19, "lw $t1, 0(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%2)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L I+3 */
BLOCK(b20, "lw $t1, 0(%2)\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I L+1 */
BLOCK(b21, "lw $t1, 0(%2)\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I L+2 */
BLOCK(b22, "lw $t1, 0(%2)\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* I L+3 */
BLOCK(b23, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S */
BLOCK(b24, "sw $zero, 16(%1)\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S L+1 */
BLOCK(b25, "sw $zero, 16(%1)\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S L+2 */
BLOCK(b26, "sw $zero, 16(%1)\n" "nop\n" "nop\n" "lw $t1, 0(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* S L+3 */
BLOCK(b27, "lw $t1, 0(%1)\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L S+1 */
BLOCK(b28, "lw $t1, 0(%1)\n" "nop\n" "sw $zero, 16(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* L S+2 */
BLOCK(b29, "sw $zero, 16(%1)\n" "sw $zero, 20(%1)\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n") /* SS */

static const Block B[30] = { b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, b10, b11, b12, b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25, b26, b27, b28, b29 };
static const char *const NAME[30] = { "L", "L L+1", "L L+2", "L L+3", "L L+4", "L L+5", "L L+6", "L L+8", "LL", "4L", "8L", "16L", "L U+1", "L U+2", "L U+3", "L U+4", "I", "L I+1", "L I+2", "L I+3", "I L+1", "I L+2", "I L+3", "S", "S L+1", "S L+2", "S L+3", "L S+1", "L S+2", "SS" };
static Block uncached(Block f) { return (Block)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

struct Span { uint32_t lo, hi; };
static Span time4(Block f) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) { const uint32_t t = f((uint32_t)&ramWords[0], 0xBF801070u); if (t < s.lo) s.lo = t; if (t > s.hi) s.hi = t; }
    return s;
}

int main() {
    printf("\ncpu/unc-load\n");
    printf("RAM loads in uncached code (KSEG1): alone, in pairs at each distance, in runs, with a reader, beside I_STAT\n");
    printf("loads and stores. Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const Span base = time4(uncached(nops));
    printf("  %-14s %6u..%-6u\n", "NOPS", (unsigned)base.lo, (unsigned)base.hi);
    for (int i = 0; i < 30; i++) {
        const Span s = time4(uncached(B[i]));
        const int d = (int)s.lo - (int)base.lo;
        const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
        printf("  %-14s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit\n", NAME[i], (unsigned)s.lo, (unsigned)s.hi, d, d < 0 ? "-" : "", per / 100, per % 100);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
