/*
 * cpu/load-fill3 - loads after loads and stores after stores in code that misses the instruction cache: when does the
 * CPU, not the bus, set the pace? (2026-10-10)
 *
 * WHY. hwtests/load-fill and load-fill2 (all four consoles, identical to the hundredth) say that in 8 KB of straight-line
 * code the bus takes fills, loads and stores one at a time and a lone load's own stall hides in the fill: a RAM load
 * 4.15 cycles above the nops, an I_STAT load 0, a store 5.66. A schedule built on that (scratch/coldbus) reads both tests
 * exactly but made pcsx-redux's load-timings probe - 32 x (lw I_STAT; s nops), the SAME register, fastest of eight runs -
 * run faster cold (84 cycles) than the console runs it warm (164): a load into the register an earlier load is still
 * filling must wait for it, as it does warm (M209), and four of them a line are CPU-bound, not bus-bound. This times
 * those shapes cold, beside their warm twins, so the rule is read off a console.
 *
 * WHAT IT DOES. As load-fill: 128 units of 16 instructions (twice the cache), Timer 2, interrupts off, fastest..slowest
 * of four passes, each row a unit's cost above the nops block. COLD:
 *   ISAME 16 / 4 / 2   lw I_STAT into ONE register 16 to a unit / every 4th word / every 2nd (load-timings' shape)
 *   IDIFF 16           lw I_STAT into two registers alternately, 16 to a unit
 *   RSAME 2 / RDIFF 2  two RAM loads back to back, into one register / two
 *   RSAME 4            four RAM loads into one register, back to back
 *   SW 2 / SW 4 / SW 8 stores back to back (the write queue is 4 deep)
 *   SW+LW              a store and a load of the same word right behind it
 * WARM: ISAME 16, ISAME 4, RSAME 2 and SW 4 as one unit in a loop of 128 from the cache.
 *
 * READ IT LIKE THIS. A CPU-bound shape costs at least its warm cost cold (16 same-register loads: ~5 a load warm); a
 * bus-bound one costs its bus slots (a load ~4, a drain ~6) and its stalls hide. IDIFF against ISAME says whether
 * different registers overlap where one register cannot.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js load-fill3`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

uint32_t ramWords[16];

typedef uint32_t (*Block)(uint32_t);

#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\n.set pop\n"
#define NOPS(n) ".rept " #n "\nnop\n.endr\n"
#define L1 "lw $t1, 0(%1)\n"
#define L2 "lw $t2, 0(%1)\n"
#define S(o) "sw $zero, " #o "(%1)\n"

#define BLOCK(NAME, UNIT) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" UNIT ".endr\n" TAIL : "=&r"(r) : "r"(p) : "t1", "t2", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }

/* the same unit in a loop of 128 from the cache; its loop control in the unit's last three slots */
#define WARM(NAME, UNIT13) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p) { \
    uint32_t r; \
    __asm__ volatile(".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\nli $t6, 128\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n" \
                     "1:\n" UNIT13 "addiu $t6, $t6, -1\nbnez $t6, 1b\nnop\n" TAIL \
                     : "=&r"(r) : "r"(p) : "t1", "t2", "t6", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }

BLOCK(nops,    NOPS(16))
BLOCK(isame16, ".rept 16\n" L1 ".endr\n")
BLOCK(isame4,  ".rept 4\n" L1 NOPS(3) ".endr\n")
BLOCK(isame2,  ".rept 8\n" L1 NOPS(1) ".endr\n")
BLOCK(idiff16, ".rept 8\n" L1 L2 ".endr\n")
BLOCK(rsame2,  L1 L1 NOPS(14))
BLOCK(rdiff2,  L1 L2 NOPS(14))
BLOCK(rsame4,  L1 L1 L1 L1 NOPS(12))
BLOCK(sw2,     S(0) S(4) NOPS(14))
BLOCK(sw4,     S(0) S(4) S(8) S(12) NOPS(12))
BLOCK(sw8,     S(0) S(4) S(8) S(12) S(16) S(20) S(24) S(28) NOPS(8))
BLOCK(swlw,    S(0) "lw $t1, 0(%1)\n" NOPS(14))
WARM(wnops,    NOPS(13))
WARM(wisame16, ".rept 13\n" L1 ".endr\n")
WARM(wisame4,  L1 NOPS(3) L1 NOPS(3) L1 NOPS(3) L1)
WARM(wrsame2,  L1 L1 NOPS(11))
WARM(wsw4,     S(0) S(4) S(8) S(12) NOPS(9))

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
    printf("\ncpu/load-fill3\n");
    printf("Loads after loads and stores after stores, cold (8 KB, every line a fill) and warm (a loop from the cache).\n");
    printf("Timer 2, fastest..slowest of four. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t ram = (uint32_t)&ramWords[0], istat = 0xBF801070u;

    printf("COLD (128 units of 16):\n");
    const Span n = time4(nops, 0);
    row("NOPS", n, n);
    row("ISAME 16", time4(isame16, istat), n);
    row("ISAME 4", time4(isame4, istat), n);
    row("ISAME 2", time4(isame2, istat), n);
    row("IDIFF 16", time4(idiff16, istat), n);
    row("RSAME 2", time4(rsame2, ram), n);
    row("RDIFF 2", time4(rdiff2, ram), n);
    row("RSAME 4", time4(rsame4, ram), n);
    row("SW 2", time4(sw2, ram), n);
    row("SW 4", time4(sw4, ram), n);
    row("SW 8", time4(sw8, ram), n);
    row("SW+LW", time4(swlw, ram), n);

    printf("\nWARM (one unit in a loop of 128):\n");
    const Span wn = time4(wnops, 0);
    row("NOPS", wn, wn);
    row("ISAME 13", time4(wisame16, istat), wn);
    row("ISAME 4", time4(wisame4, istat), wn);
    row("RSAME 2", time4(wrsame2, ram), wn);
    row("SW 4", time4(wsw4, ram), wn);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
