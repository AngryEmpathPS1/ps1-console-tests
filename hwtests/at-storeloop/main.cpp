/*
 * cpu/at-storeloop (2026-11-23, the cold-io session) - crt0's two memory-clearing loops, cached, at every refresh phase.
 *
 * WHY. experiments/fuzz-all/2026-11-19-rfanchor: the runner's start to the fuzz host's main splits as +3 (to the program's first
 * instruction), crt0 -4, main's setup; the cold-stub grid (STATUS "The cold-stub grid") shows cold straight code exact here, and crt0
 * spends ~3,900 of its cycles in two cached byte-store loops (PSn00bSDK's: 800154b8h, 760 x `sb zero; slt; bne; addiu` at 4 cycles a
 * byte; 800156e4h, 132 x a 6-instruction memset) across ~15 refreshes. The fuzzer's sweep programs' setup copies their case table in a
 * cached store loop the same way, and their cases' start phase moves with it (cold-io to the console fuzzer, 2026-11-23). No test
 * times a sustained cached store loop over the refresh: this does.
 *
 * WHAT IT DOES. Interrupts masked (I_MASK 0) while timed, Timer 2 at the system clock. Each loop runs once to warm its lines, then for d = 0, 8 .. 248 runs after
 * a cached slide of d nops (so it starts d cycles later on the 256-cycle refresh grid), Timer 2 read right before and after:
 *   SB4  crt0's first: sb zero, 0(a0); slt at, a0, a1; bne at, zero; addiu a0, 1 - 760 bytes
 *   SB6  crt0's second: move v0, a0; blez a2; addi a2, -1; sb a1, 0(a0); b; addiu a0, 1 - 132 bytes
 *   SW4  SB4 with sw / addiu 4 - 190 words
 *   NO4  SB4 with the store a nop - the loop's own cost
 * A row: the loop, then its 32 times (d = 0, 8 .. 248), then min / max.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-storeloop`.
 */

#include <common.h>

static volatile uint8_t area[1024] __attribute__((aligned(16)));

/* sl_run(fn, d, buf): slide d nops (cached, d < 256), read Timer 2, call fn(buf), read Timer 2; returns the difference */
extern "C" uint32_t sl_run(uint32_t fn, uint32_t d, uint32_t buf);
extern "C" void sl_sb4(void), sl_sb6(void), sl_sw4(void), sl_no4(void);
__asm__(".set push\n.set noreorder\n"
        ".balign 16\n.global sl_run\nsl_run:\n"
        "  addiu $sp, $sp, -8\n  sw $ra, 4($sp)\n  move $t9, $a0\n  la $t0, 3f\n  sll $t1, $a1, 2\n  subu $t0, $t0, $t1\n  lui $t8, 0x1F80\n"
        "  jr $t0\n  nop\n"
        "  .rept 256\n  nop\n  .endr\n"
        "3:\n  lhu $t6, 0x1120($t8)\n  jalr $t9\n  move $a0, $a2\n  lhu $t7, 0x1120($t8)\n  nop\n"
        "  subu $v0, $t7, $t6\n  andi $v0, $v0, 0xFFFF\n  lw $ra, 4($sp)\n  nop\n  jr $ra\n  addiu $sp, $sp, 8\n"   /* the nop: jr would see ra before the load lands (the load delay) */
        /* SB4: crt0's 800154b8h loop, a0 = buf, a1 = buf + 760 */
        ".balign 16\n.global sl_sb4\nsl_sb4:\n  addiu $a1, $a0, 760\n"
        "1:\n  sb $zero, 0($a0)\n  slt $at, $a0, $a1\n  bne $at, $zero, 1b\n  addiu $a0, $a0, 1\n  jr $ra\n  nop\n"
        /* SB6: crt0's 800156e4h memset, a1 = 0 the value, a2 = 132 the count */
        ".balign 16\n.global sl_sb6\nsl_sb6:\n  move $a1, $zero\n  li $a2, 132\n"
        "1:\n  move $v0, $a0\n  blez $a2, 2f\n  addi $a2, $a2, -1\n  sb $a1, 0($a0)\n  b 1b\n  addiu $a0, $a0, 1\n2:\n  jr $ra\n  nop\n"
        /* SW4: SB4 with words */
        ".balign 16\n.global sl_sw4\nsl_sw4:\n  addiu $a1, $a0, 760\n"
        "1:\n  sw $zero, 0($a0)\n  slt $at, $a0, $a1\n  bne $at, $zero, 1b\n  addiu $a0, $a0, 4\n  jr $ra\n  nop\n"
        /* NO4: SB4 with the store a nop */
        ".balign 16\n.global sl_no4\nsl_no4:\n  addiu $a1, $a0, 760\n"
        "1:\n  nop\n  slt $at, $a0, $a1\n  bne $at, $zero, 1b\n  addiu $a0, $a0, 1\n  jr $ra\n  nop\n"
        ".set pop\n");

static void row(const char *name, void (*f)(void)) {
    const uint32_t fn = (uint32_t)f, buf = (uint32_t)area;
    volatile uint32_t *imask = (volatile uint32_t *)0x1F801074u; const uint32_t im = *imask; *imask = 0;   // no interrupts while timed
    sl_run(fn, 0, buf); sl_run(fn, 0, buf);   // warm the loop's lines and the slide
    uint32_t t[32], mn = ~0u, mx = 0;
    for (int i = 0; i < 32; i++) { t[i] = sl_run(fn, i * 8, buf); if (t[i] < mn) mn = t[i]; if (t[i] > mx) mx = t[i]; }
    *imask = im;
    printf("%s", name); for (int i = 0; i < 32; i++) printf(" %u", (unsigned)t[i]); printf("  min %u max %u\n", (unsigned)mn, (unsigned)mx);
}

int main() {
    printf("\ncpu/at-storeloop\n");
    printf("crt0's two clearing loops and two controls, cached, after a slide of d = 0, 8 .. 248 nops; Timer 2 cycles a run.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    *(volatile uint16_t *)0x1F801124u = 0;   // Timer 2: system clock, free run
    row("SB4", sl_sb4); row("SB6", sl_sb6); row("SW4", sl_sw4); row("NO4", sl_no4);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
