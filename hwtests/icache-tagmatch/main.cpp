/*
 * cpu/icache-tagmatch - an I-cache miss on a line whose TAG ALREADY MATCHES: does the refill take the whole line,
 * or only the missed word to the end of the line?
 *
 * WHY (ROAD A8's I-cache line; M245; an AI's audit of v0.265, 2026-09-29). On a miss whose line tag matches but
 * whose word is invalid:
 *   psx-spx (live 934ccc1, Nicolas Noble's BIU investigation): "a full line refill from RAM, replacing all 4 code
 *     words and setting valid to 0Fh" - and then, contradicting it: "Consecutive fills to the same line (with
 *     matching tags) OR into the valid bits, progressively filling the line".
 *   mednafen (cpu.cpp): fills from the missed word to the end, as on any miss. So does this core (M245).
 * Filling from the missed word is settled for a tag MISMATCH (Sony's Hardware manual, SN's CACHE.S, and Sony's
 * analyzer capture TUTO3.PAD: fills start at words 0/1/2/3 3,645 / 268 / 182 / 153, the last as single reads -
 * STATUS, "an AI's audit"). The matching tag is what nobody has measured, and every game reaches it: branchaudit's
 * icache-tagmatch-midline-fill fires on every disc of the US library, millions of times each. The two rules
 * differ by the words BEFORE the missed one - read or not - so they move timing everywhere a little.
 *
 * WHAT IT DOES. A four-instruction function on one 16-byte line (word 0, word 1, jr ra, delay slot). Timer 2 at
 * the system clock, interrupts off, every row the minimum of 16 runs:
 *   COLD      FlushCache, then time a call from word 0: a miss that fills the line.
 *   WARM      call it again: all hits.
 *   CASE      FlushCache; call from word 2 (a tag MISMATCH miss: fills words 2-3 under both rules); call from
 *             word 1 (the tag now MATCHES, word 1 invalid: the question); then time a call from word 0.
 *               whole-line refill (psx-spx): word 0 became valid - CASE reads like WARM;
 *               from the missed word (mednafen, this core): word 0 is still invalid - CASE reads like COLD
 *               (a miss at word 0 that fills the line).
 *   CONTROL   FlushCache; call from word 2; then time a call from word 0 (a matching-tag miss at word 0, where
 *             both rules fill words 0-3): must read like COLD on any machine. If it does not, this test is
 *             measuring something else.
 *   W1 TIME   the call from word 1 itself, timed: the whole-line rule reads one word more (0-3 against 1-3).
 * The verdict compares CASE with CONTROL: after FlushCache the timed call's own code is cold as well, so CASE on a
 * whole-line machine is WARM plus that, not WARM.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.266 (`node hwtests/run.js icache-tagmatch --frames=600`): COLD 23, WARM 15, CONTROL 23,
 * CASE 23, W1 TIME 21, W1 COLD 21 - "the refill ran from the MISSED word". TESTED ON A SCRATCH CORE WITH
 * PSX-SPX'S RULE (a matching-tag miss refills the line from word 0): CASE 19, W1 TIME 22, the rest the same -
 * "the WHOLE LINE was refilled". So the test tells the two apart by 4 cycles, and a console's CASE says which.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

extern "C" void tagline(void);
extern "C" void tagline_w1(void);
extern "C" void tagline_w2(void);
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global tagline\n"
    ".global tagline_w1\n"
    ".global tagline_w2\n"
    ".set push\n.set noreorder\n"
    "tagline:\n"
    "    addiu $v0, $v0, 1\n"      /* word 0 */
    "tagline_w1:\n"
    "    addiu $v0, $v0, 2\n"      /* word 1 */
    "tagline_w2:\n"
    "    jr $ra\n"                 /* word 2 */
    "    addiu $v0, $v0, 4\n"      /* word 3, the delay slot */
    ".set pop\n");

typedef void (*fn)(void);

/* The kernel's FlushCache, A0h function 44h: every I-cache line invalid. */
static void flushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}

static inline uint16_t t2() { return read16(0x1F801120); }

static uint32_t timeCall(fn f) {
    const uint16_t a = t2();
    f();
    const uint16_t b = t2();
    return (uint16_t)(b - a);
}

static uint32_t minOf(uint32_t (*one)(void)) {
    uint32_t m = 0xFFFFFFFF;
    for (int i = 0; i < 16; i++) { const uint32_t v = one(); if (v < m) m = v; }
    return m;
}

static uint32_t cold()    { flushCache(); return timeCall(tagline); }
static uint32_t warm()    { tagline(); return timeCall(tagline); }
static uint32_t caseRun() { flushCache(); tagline_w2(); tagline_w1(); return timeCall(tagline); }
static uint32_t control() { flushCache(); tagline_w2(); return timeCall(tagline); }
static uint32_t w1Time()  { flushCache(); tagline_w2(); return timeCall(tagline_w1); }
static uint32_t w1Cold()  { flushCache(); return timeCall(tagline_w1); }

int main() {
    printf("\ncpu/icache-tagmatch\n");
    printf("An I-cache miss on a line whose tag already matches: the whole line refilled, or from the missed word?\n");
    printf("Times in system clocks (Timer 2), each the minimum of 16 runs. No assertions.\n\n");
    const uint16_t prev = initTimer(2, 0);
    EnterCriticalSection();
    const uint32_t c = minOf(cold), w = minOf(warm), k = minOf(caseRun), ctl = minOf(control);
    const uint32_t w1 = minOf(w1Time), w1c = minOf(w1Cold);
    ExitCriticalSection();
    restoreTimer(2, prev);
    printf("  COLD    call from word 0 after FlushCache                 %4d\n", c);
    printf("  WARM    the same call again                                %4d\n", w);
    printf("  CONTROL word 2, then word 0 (a matching-tag miss at word 0) %4d   <- must read like COLD\n", ctl);
    printf("  CASE    word 2, word 1, then word 0                        %4d\n", k);
    printf("  W1 TIME word 2, then the call from word 1 (timed)          %4d\n", w1);
    printf("  W1 COLD the call from word 1 after FlushCache              %4d\n", w1c);
    /* CASE is judged against CONTROL, not the COLD/WARM midpoint: after FlushCache the timed call's own code is
     * cold too, so CASE on a whole-line machine is WARM plus that - it read 19 against COLD 23 / WARM 15 on a
     * scratch core with psx-spx's rule, exactly the midpoint. CONTROL has the same cold harness and a line fill. */
    printf("\n");
    if (c <= w + 2 || ctl + 1 < c) printf("  *** A CONTROL FAILED (COLD not slower than WARM, or CONTROL faster than COLD) - no verdict ***\n");
    else if (ctl >= k + 3) printf("  VERDICT: word 0 was VALID after the word-1 miss (CASE %d cycles under CONTROL) - the WHOLE LINE was refilled (psx-spx)\n", ctl - k);
    else if (k + 1 >= ctl) printf("  VERDICT: word 0 was still INVALID (CASE = CONTROL) - the refill ran from the MISSED word (mednafen; this core)\n");
    else printf("  VERDICT: unclear - CASE %d cycles under CONTROL, less than a line fill\n", ctl - k);
    printf("\nDone.\n");
    for (;;) { }
}
