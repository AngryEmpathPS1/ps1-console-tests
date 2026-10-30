/*
 * cpu/load-fill2 - what a load in cold code costs when its value is used, beside another load or a store, from the
 * scratchpad, and from the SPU (2026-10-10).
 *
 * WHY. hwtests/load-fill on all four consoles, identical: in 8 KB of straight-line code (every line a fill) a RAM load
 * costs 4.15 cycles above the nops at every word of its line, an I_STAT load nothing (1.09 at word 1), and warm 2.00 -
 * where this core charges 7.08 / 5.95 and 2.9-3.9. An instruction-streaming model (the CPU runs the line's words as
 * they arrive; a load's bus read queues behind the line's burst and in front of the next fill, ~4 cycles; its stall
 * hides in the fill) reads load-fill exactly (scratchpad experiment, scratch/patch_stream.py) but moves the GTE latency
 * probes (cold, unrolled) away from Redux's table. Which part of it is wrong is what this asks: when the loaded value is
 * used, the CPU must wait for the data, not the fill; two loads should cost two bus reads; a store goes through the
 * write buffer and may cost nothing; the scratchpad has no bus read; the SPU is on the other bus.
 *
 * WHAT IT DOES. As load-fill: 128 units of 16 instructions (2,048 instructions, twice the cache), a 64-byte aligned
 * start, Timer 2 at the system clock, interrupts off, fastest..slowest of four passes; each row's cost a unit above
 * the nops block:
 *   RAM          lw at word 0 (load-fill's RAM w=0, the control)
 *   USE k        lw at word 0, the loaded register read k+1 instructions on (k = 1, 2, 4, 8: words 2, 3, 5, 9)
 *   TWO          lw; lw (words 0, 1)          SPLIT   lw at word 0 and at word 8 (two lines apart)
 *   STORE        sw at word 0                 LW+SW   lw at word 0, sw at word 2
 *   SCRATCH      lw from the scratchpad at word 0;   SCRATCH USE  the same, read at word 2
 *   I_STAT 1 USE lw from I_STAT at word 1, read at word 3 (load-fill's 1.09 at word 1, with a reader)
 *   SPU          lhu SPUSTAT at word 0 (the SPU's bus, not RAM's)
 *
 * READ IT LIKE THIS. Streaming says: USE k pays until the data is back (more for small k), TWO and SPLIT twice RAM,
 * STORE little, SCRATCH 0, SPU its own access with the fills going on beside it or not. This core's rows against them
 * say which of the model's parts the consoles have.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js load-fill2`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

uint32_t ramWords[4];

typedef uint32_t (*Block)(uint32_t);

#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\n.set pop\n"
#define NOPS(n) ".rept " #n "\nnop\n.endr\n"

#define BLOCK(NAME, UNIT) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" UNIT ".endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t2", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }

BLOCK(nops,     NOPS(16))
BLOCK(ram0,     "lw $t1, 0(%1)\n" NOPS(15))
BLOCK(use1,     "lw $t1, 0(%1)\n" NOPS(1) "addu $t2, $t1, $t1\n" NOPS(13))
BLOCK(use2,     "lw $t1, 0(%1)\n" NOPS(2) "addu $t2, $t1, $t1\n" NOPS(12))
BLOCK(use4,     "lw $t1, 0(%1)\n" NOPS(4) "addu $t2, $t1, $t1\n" NOPS(10))
BLOCK(use8,     "lw $t1, 0(%1)\n" NOPS(8) "addu $t2, $t1, $t1\n" NOPS(6))
BLOCK(two,      "lw $t1, 0(%1)\nlw $t2, 4(%1)\n" NOPS(14))
BLOCK(split,    "lw $t1, 0(%1)\n" NOPS(7) "lw $t2, 4(%1)\n" NOPS(7))
BLOCK(store,    "sw $zero, 8(%1)\n" NOPS(15))
BLOCK(lwsw,     "lw $t1, 0(%1)\nnop\nsw $zero, 8(%1)\n" NOPS(13))
BLOCK(scruse,   "lw $t1, 0(%1)\nnop\naddu $t2, $t1, $t1\n" NOPS(13))
BLOCK(is1use,   "nop\nlw $t1, 0(%1)\nnop\naddu $t2, $t1, $t1\n" NOPS(12))
BLOCK(spu0,     "lhu $t1, 0(%1)\n" NOPS(15))

struct Span { uint32_t lo, hi; };
static Span time4(Block f, uint32_t p) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) {
        const uint32_t t = f(p);
        if (t < s.lo) s.lo = t;
        if (t > s.hi) s.hi = t;
    }
    return s;
}

static void row(const char* name, Span s, Span base) {
    const int d = (int)s.lo - (int)base.lo;
    const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
    printf("  %-14s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit\n", name, (unsigned)s.lo, (unsigned)s.hi, d,
           d < 0 ? "-" : "", per / 100, per % 100);
}

int main() {
    printf("\ncpu/load-fill2\n");
    printf("8 KB blocks from a cold instruction cache (Timer 2, fastest..slowest of four): loads used, paired, stored,\n");
    printf("from the scratchpad and the SPU. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t ram = (uint32_t)&ramWords[0], istat = 0xBF801070u, scratch = 0x1F800000u, spustat = 0xBF801DAEu;

    const Span n = time4(nops, 0);
    row("NOPS", n, n);
    row("RAM", time4(ram0, ram), n);
    row("USE 1", time4(use1, ram), n);
    row("USE 2", time4(use2, ram), n);
    row("USE 4", time4(use4, ram), n);
    row("USE 8", time4(use8, ram), n);
    row("TWO", time4(two, ram), n);
    row("SPLIT", time4(split, ram), n);
    row("STORE", time4(store, ram), n);
    row("LW+SW", time4(lwsw, ram), n);
    row("SCRATCH", time4(ram0, scratch), n);
    row("SCRATCH USE", time4(scruse, scratch), n);
    row("I_STAT 1 USE", time4(is1use, istat), n);
    row("SPU", time4(spu0, spustat), n);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
