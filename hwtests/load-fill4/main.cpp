/*
 * cpu/load-fill4 - where in its line a store or a load sits, in code that misses the instruction cache: which bus
 * request goes first when the line's burst ends? (2026-10-10)
 *
 * WHY. hwtests/load-fill, load-fill2 and load-fill3 (every console identical) give the cost of one shape per row. Read
 * against IDT's R3051 manual (chapter 6, the bus interface unit's service order: an ongoing transaction finishes, then
 * DMA, then an instruction cache miss, then pending writes, then data reads) they fit one schedule: the line streams
 * in, a load or a store waits for the burst's end, the next line's miss goes before any write or read that has not
 * started by then, and a second load or a store behind a load waits for the first load's value. SW 2 (7.30) and SW 4
 * (14.57) say a write is a PAIR of words (6 + 2) and the miss cuts in between pairs; SW+LW (9.29) says the load waits
 * for the next line. Those were read off rows at fixed places in the line; this moves each shape through the line, and
 * across a line boundary, so the schedule is read off the console rather than fitted.
 *
 * WHAT IT DOES. As load-fill3: 128 units of 16 instructions (four lines, twice the cache), Timer 2, interrupts off,
 * fastest..slowest of four passes, each row a unit's cost above the nops block. In the names a digit is the word of the
 * unit an access sits at (0-3 the first line, 4 the second line's first word); S a store (to a word no load reads), L a
 * RAM load, I an I_STAT load, U the loaded value read by an addu, n a nop. Every unrepeated row has its accesses in the
 * unit's first line or two and nops after.
 *
 * READ IT LIKE THIS. If the schedule is right: a lone store costs the same at every word, two stores in one line cost
 * one pair, two stores in two lines cost two writes, a store and a load each cost their own slot unless the load waits
 * behind the store's write, and a lone I_STAT load is free anywhere but word 1 (load-fill: 1.09).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js load-fill4`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

uint32_t ramWords[32];

typedef uint32_t (*Block)(uint32_t, uint32_t);

#define HEAD ".set push\n.set noreorder\n.set noat\nlui $t9, 0x1f80\n.balign 64\nlhu $t8, 0x1120($t9)\n.rept 15\nnop\n.endr\n"
#define TAIL "lhu $t7, 0x1120($t9)\nnop\nsubu %0, $t7, $t8\n.set pop\n"
#define N(n) ".rept " #n "\nnop\n.endr\n"
#define L1 "lw $t1, 0(%1)\n"           /* RAM loads: words 0 and 1 of ramWords */
#define L2 "lw $t2, 4(%1)\n"
#define I1 "lw $t1, 0(%2)\n"           /* I_STAT */
#define I2 "lw $t2, 0(%2)\n"
#define S(o) "sw $zero, " #o "(%1)\n"  /* stores: words 4.. of ramWords, never loaded */
#define U1 "addu $t3, $t1, $t1\n"

/* %1 = RAM, %2 = I_STAT */
#define BLOCK(NAME, UNIT) \
static uint32_t __attribute__((noinline, aligned(64))) NAME(uint32_t p, uint32_t q) { \
    uint32_t r; \
    __asm__ volatile(HEAD ".rept 128\n" UNIT ".endr\n" TAIL : "=&r"(r) : "r"(p), "r"(q) : "t1", "t2", "t3", "t7", "t8", "t9", "memory"); \
    return r & 0xFFFF; }

BLOCK(nops,     N(16))
/* one store at each word of the first line */
BLOCK(s0,       S(16) N(15))
BLOCK(s1,       N(1) S(16) N(14))
BLOCK(s2,       N(2) S(16) N(13))
BLOCK(s3,       N(3) S(16) N(12))
/* two stores: back to back early and late in the line, spaced, and either side of the line boundary */
BLOCK(s01,      S(16) S(20) N(14))
BLOCK(s23,      N(2) S(16) S(20) N(12))
BLOCK(s02,      S(16) N(1) S(20) N(13))
BLOCK(s34,      N(3) S(16) S(20) N(12))
BLOCK(s012,     S(16) S(20) S(24) N(13))
/* a store in every line, and a pair in every line */
BLOCK(sall,     S(16) N(3) S(20) N(3) S(24) N(3) S(28) N(3))
BLOCK(ssall,    S(16) S(20) N(2) S(24) S(28) N(2) S(32) S(36) N(2) S(40) S(44) N(2))
/* a RAM load and a store */
BLOCK(l0s1,     L1 S(16) N(14))
BLOCK(l0s3,     L1 N(2) S(16) N(12))
BLOCK(s0l1,     S(16) L1 N(14))
BLOCK(s0l3,     S(16) N(2) L1 N(12))
BLOCK(s0l4,     S(16) N(3) L1 N(11))
BLOCK(s3l4,     N(3) S(16) L1 N(11))
/* two RAM loads, spaced and across the boundary; the reader at each distance */
BLOCK(l0l2,     L1 N(1) L2 N(13))
BLOCK(l0l3,     L1 N(2) L2 N(12))
BLOCK(l3l4,     N(3) L1 L2 N(11))
BLOCK(l1u3,     N(1) L1 N(1) U1 N(12))
BLOCK(l2u4,     N(2) L1 N(1) U1 N(11))
BLOCK(l3u5,     N(3) L1 N(1) U1 N(10))
BLOCK(l0u3,     L1 N(2) U1 N(12))
/* I_STAT: lone at word 1 again, pairs at each place, one line only (load-fill3's ISAME 2 had two in every line) */
BLOCK(i1,       N(1) I1 N(14))
BLOCK(i01,      I1 I2 N(14))
BLOCK(i12,      N(1) I1 I2 N(13))
BLOCK(i23,      N(2) I1 I2 N(12))
BLOCK(i02,      I1 N(1) I2 N(13))
BLOCK(i03,      I1 N(2) I2 N(12))
BLOCK(i34,      N(3) I1 I2 N(11))
/* I_STAT beside a RAM load or a store */
BLOCK(i0l1,     I1 L2 N(14))
BLOCK(l0i1,     L1 I2 N(14))
BLOCK(s0i1,     S(16) I1 N(14))
BLOCK(i0s1,     I1 S(16) N(14))

struct Span { uint32_t lo, hi; };
static Span time4(Block f, uint32_t p, uint32_t q) {
    Span s = { 0xFFFFFFFFu, 0 };
    for (int k = 0; k < 4; k++) {
        const uint32_t t = f(p, q);
        if (t < s.lo) s.lo = t;
        if (t > s.hi) s.hi = t;
    }
    return s;
}

static Span base;
static void row(const char* name, Block f) {
    const Span s = time4(f, (uint32_t)&ramWords[0], 0xBF801070u);
    const int d = (int)s.lo - (int)base.lo;
    const int a = d < 0 ? -d : d, per = (a * 100 + 64) / 128;
    printf("  %-14s %6u..%-6u  above the nops %6d  ->  %s%d.%02d a unit\n", name, (unsigned)s.lo, (unsigned)s.hi, d,
           d < 0 ? "-" : "", per / 100, per % 100);
}

int main() {
    printf("\ncpu/load-fill4\n");
    printf("Stores and loads at each word of a cold line (8 KB, every line a fill). Digits: the word of the unit (0-3 the\n");
    printf("first line); S store, L RAM load, I I_STAT load, U a reader. Timer 2, fastest..slowest of four.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    base = time4(nops, (uint32_t)&ramWords[0], 0xBF801070u);
    row("NOPS", nops);
    row("S0", s0);   row("S1", s1);   row("S2", s2);   row("S3", s3);
    row("S0 S1", s01); row("S2 S3", s23); row("S0 n S2", s02); row("S3 S4", s34); row("S0 S1 S2", s012);
    row("S EVERY LINE", sall); row("SS EVERY LINE", ssall);
    row("L0 S1", l0s1); row("L0 n n S3", l0s3); row("S0 L1", s0l1); row("S0 n n L3", s0l3); row("S0 L4", s0l4);
    row("S3 L4", s3l4);
    row("L0 n L2", l0l2); row("L0 n n L3", l0l3); row("L3 L4", l3l4);
    row("L1 n U3", l1u3); row("L2 n U4", l2u4); row("L3 n U5", l3u5); row("L0 n n U3", l0u3);
    row("I1", i1); row("I0 I1", i01); row("I1 I2", i12); row("I2 I3", i23); row("I0 n I2", i02); row("I0 n n I3", i03);
    row("I3 I4", i34);
    row("I0 L1", i0l1); row("L0 I1", l0i1); row("S0 I1", s0i1); row("I0 S1", i0s1);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
