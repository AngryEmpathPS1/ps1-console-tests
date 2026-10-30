/*
 * cpu/load-fill - does an instruction-cache line fill end a load's shadow?
 *
 * WHY (v0.377.25). load-use and load-pairs on the SCPH-5501, 7501 and 9001 put a RAM load's shadow at its third to
 * sixth instruction (four of the stall's six cycles given back), and every one of their loops runs from a WARM
 * instruction cache. Nothing says what happens when one of those instructions misses the cache: the fill reads RAM,
 * and the load is reading RAM. This core says the fill ends the shadow (an instruction that costs more than its cycle
 * does). The other rule - the fill's own cycles charged, the shadow running on - reads pcsx-redux's gte-latency probes
 * (cold, unrolled code; RESULTS.md, an SCPH-5501) 2-3 cells better (252/255 against 250/252 exact), but nothing
 * measured decides between them. An on-die register's load leaves the RAM bus free, so its shadow might survive a
 * fill where a RAM load's cannot.
 *
 * WHAT IT DOES. Each block is 8 KB of straight-line code - twice the instruction cache, so every line is fetched from
 * RAM on every pass - timed by Timer 2 at the system clock with interrupts off, fastest..slowest of four passes:
 *   NOPS       128 units of 16 nops (the baseline: 2,048 nops, 512 line fills)
 *   RAM w      128 units of w nops, `lw t1, 0(t0)` (main RAM), 15 - w nops: the load at word w of its line, so the
 *              next line starts at the load's (4 - w)th instruction - w = 0: its 4th (the shadow's second),
 *              w = 1: its 3rd (the first), w = 2: its 2nd, w = 3: its 1st (the delay slot)
 *   I_STAT w   the same with `lw t1, 0(t0)` from I_STAT (an on-die register)
 *   WARM       one unit (RAM w = 0) in a loop of 128 from a warm cache - the shadow as load-use measures it
 *   UNCACHED   NOPS, RAM 0 and I_STAT 0 run from KSEG1 (every instruction a RAM read)
 * Each row prints the block's cycles and, above NOPS, the cost of one load: 1.25 cycles of difference in a whole block
 * is 0.01 a load, so a draw of the DRAM refresh (5 cycles every 256) moves the second decimal.
 *
 * READ IT LIKE THIS. WARM is the shadow's 2 cycles (load-use's "no reader" 2.02-2.08). A cold RAM row that still
 * reads ~2 says a fill does not end the shadow; one that reads more says it does, and by w which positions it covers.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js load-fill`.
 * ON THIS CORE AT v0.377.25 (the fill ends the shadow), a load above the nops (NOPS 4,737 cycles a block):
 *   RAM w = 0..3 7.10 7.08 5.94 5.96; I_STAT 2.88 3.91 3.91 3.91; WARM RAM 2.26, I_STAT 2.00; UNCACHED RAM 6.13,
 *   I_STAT 3.01 (NOPS 12,583).
 * AT v0.377.26 (a load in a shadow issues there; the uncached fetch behind an on-die load runs in its stall): RAM 7.08
 *   7.08 5.95 5.95; I_STAT 2.93 3.89 3.91 3.90; WARM RAM 2.18, I_STAT 2.00; UNCACHED RAM 6.13, I_STAT -0.05.
 * THE OTHER RULE (the fill's cycles charged, the shadow running on - scratch/_cores/c25f, FILL_MODE=c):
 *   RAM 5.17 5.17 3.12 2.20; I_STAT 1.95 1.91 1.95 1.93; UNCACHED the same.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

uint32_t ramWord;

typedef uint32_t (*Block)(uint32_t);

/* t9: the timer's high half; the counter is read after a 64-byte .balign so the block starts on a line, and the
 * 15 nops after the first read fill that line out. */
#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\n.set pop\n"

#define UNIT(W) ".rept " #W "\nnop\n.endr\nlw $t1, 0(%1)\n.rept 15-" #W "\nnop\n.endr\n"

#define LOADBLOCK(NAME, W) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" UNIT(W) ".endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }

static uint32_t __attribute__((noinline, aligned(64))) nops(uint32_t) {
    uint32_t r;
    __asm__ volatile(HEAD ".rept 2048\nnop\n.endr\n" TAIL : "=&r"(r) : : "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}
LOADBLOCK(load0, 0) LOADBLOCK(load1, 1) LOADBLOCK(load2, 2) LOADBLOCK(load3, 3)

/* WARM: one 16-instruction unit (the load at word 0) in a loop, so every pass after the first runs from the cache;
 * its loop control (addiu, bnez, nop) sits in the unit's last three slots. */
static uint32_t __attribute__((noinline, aligned(64))) warm(uint32_t p) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\nli $t6, 128\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
                     "1:\nlw $t1, 0(%1)\n.rept 12\nnop\n.endr\naddiu $t6, $t6, -1\nbnez $t6, 1b\nnop\n" TAIL
                     : "=&r"(r) : "r"(p) : "t1", "t6", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}
static uint32_t __attribute__((noinline, aligned(64))) warmNops(uint32_t) {
    uint32_t r;
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\nli $t6, 128\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
                     "1:\nnop\n.rept 12\nnop\n.endr\naddiu $t6, $t6, -1\nbnez $t6, 1b\nnop\n" TAIL
                     : "=&r"(r) : : "t6", "t7", "t8", "t9", "memory");
    return r & 0xFFFF;
}

static Block uncached(Block f) { return (Block)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

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
    printf("  %-14s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a load\n", name, (unsigned)s.lo, (unsigned)s.hi, d,
           d < 0 ? "-" : "", per / 100, per % 100);
}

int main() {
    printf("\ncpu/load-fill\n");
    printf("8 KB blocks from a cold instruction cache (Timer 2, fastest..slowest of four), a load every 16 instructions.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t ram = (uint32_t)&ramWord, istat = 0xBF801070u;
    static const Block lb[4] = { load0, load1, load2, load3 };

    const Span n = time4(nops, 0);
    printf("CACHED, cold (every line a fill):\n");
    row("NOPS", n, n);
    Span r[4], io[4];
    for (int w = 0; w < 4; w++) { r[w] = time4(lb[w], ram); io[w] = time4(lb[w], istat); }
    static const char* const rn[4] = { "RAM w=0", "RAM w=1", "RAM w=2", "RAM w=3" };
    static const char* const in[4] = { "I_STAT w=0", "I_STAT w=1", "I_STAT w=2", "I_STAT w=3" };
    for (int w = 0; w < 4; w++) row(rn[w], r[w], n);
    for (int w = 0; w < 4; w++) row(in[w], io[w], n);

    printf("\nWARM (one unit in a loop of 128, from the cache):\n");
    const Span wn = time4(warmNops, 0);
    row("NOPS", wn, wn);
    row("RAM w=0", time4(warm, ram), wn);
    row("I_STAT w=0", time4(warm, istat), wn);

    printf("\nUNCACHED (KSEG1: every instruction a RAM read):\n");
    const Span un = time4(uncached(nops), 0);
    row("NOPS", un, un);
    row("RAM w=0", time4(uncached(load0), ram), un);
    row("I_STAT w=0", time4(uncached(load0), istat), un);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
