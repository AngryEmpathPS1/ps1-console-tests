/*
 * cpu/wr-stream - a stream of RAM stores that never pauses: when a DRAM write burst goes on, and when it ends.
 *
 * WHY (v0.377.53). nocash's DIAG times 16 stores to one word of RAM, 256 passes back to back: 8546 cycles with the
 * stores back to back (RAM WR x16) and 9292 with a nop after each (RAM WR NOP x16), and he notes "1st nop adds 0 clks
 * on write (except 3 clks for last write)" - with a nop between the stores the pass's 3-cycle loop end does not
 * overlap the writes. hwtests/wb-drain, whose stores start on an empty queue, could not see it, and the queue that fits
 * wb-drain read 9170 here. This core reads 9286 since v0.377.53 because a burst goes on only while its next word was
 * queued more than 2 cycles before the last word's end (src/psx.js WRITE_QUEUE_CONT_LEAD): with a store every 2 cycles
 * only ~4 cycles of words are queued, the loop end lets them run out, and every pass starts a new burst. That rule
 * came from one console's two numbers. This maps it.
 *
 * WHAT IT DOES. Each row: 16 x (sw, K nops), then G nops, as one pass, 256 passes back to back (the queue never empties
 * on purpose), against the same pass with nops for the stores; cycles a pass above that, Timer 2 at the system clock,
 * interrupts off. G = 0..8 across a row (the loop's own addiu, bnez and nop come after the G nops), K = 0, 1, 2, the 16
 * stores to ONE word (DIAG's) and to 16 consecutive words. Then one store and G nops a pass (DIAG's RAM WR is G = 0).
 *
 * READ IT LIKE THIS. With K = 0 the stores outrun the bus and the pass is the bus's: 16 words of a burst and the
 * refreshes, flat across G until the CPU's own pass is longer. With K = 1 the stores come at the bus's own rate; where a
 * row steps up by ~4 as G grows is where the queued words run out at the loop end and the next pass starts a new burst
 * (its first word 6 cycles, not 2) - the size of that window is what WRITE_QUEUE_CONT_LEAD guesses. K = 2 leaves the
 * bus idle between words: does each word start a burst of its own? One word against 16 consecutive tells a column of
 * the same row from a walk along it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js wr-stream`.
 * ON THIS CORE AT v0.377.53 (the continuation window and the refresh cycle): one word K = 0 14.34 13.30 12.30 13.37
 *   14.21 13.23 13.13 13.39 13.16 (the CPU outruns the bus to G = 2, then each pass starts a burst); K = 1 1.20 0.64
 *   0.77 0.82 0.71 0.79 0.91 0.70 0.33; K = 2 0.19-0.78; 16 words as one word; one store a pass 0 throughout. DIAG's
 *   two, as printed: K = 0 G = 0 33.35 a pass (his console 33.31 with his call taken out), K = 1 G = 0 36.25 (36.22).
 *   v0.377.52's core (slots freed at their end): K = 0 steps at G = 5 (14.08), K = 1 0.85 at G = 0, 35.94 a pass.
 *   With the free-early slots alone: K = 0 steps at G = 6, K = 1 0.71, 35.75 a pass. A console's K = 0 row says
 *   where its step is; WRITE_QUEUE_CONT_LEAD moves it one G a cycle.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u

uint32_t wsBuf[64] __attribute__((aligned(64)));

typedef uint32_t (*fn)(uint32_t);

#define PRE  ".set push\n\t.set noreorder\n\t.set noat\n\t" \
             "lui $t9, 0x1f80\n\tli $t8, 256\n\tlhu %[t0], 0x1120($t9)\n\tnop\n1:\n\t"
#define POST "addiu $t8, $t8, -1\n\tbnez $t8, 1b\n\tnop\n\tlhu %[t1], 0x1120($t9)\n\tnop\n\t.set pop\n\t"

/* N stores, K nops after each, then G nops: one pass. STEP bytes between the stores' words (0: one word). */
template <int N, int K, int G, int STEP> __attribute__((noinline)) uint32_t tS(uint32_t p) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".set wso, 0\n\t.rept %[n]\n\tsw $zero, wso(%[p])\n\t.rept %[k]\n\tnop\n\t.endr\n\t.set wso, wso + %[s]\n\t.endr\n\t"
                     ".rept %[g]\n\tnop\n\t.endr\n\t" POST
                     : [t0] "=&r"(t0), [t1] "=&r"(t1) : [p] "r"(p), [n] "i"(N), [k] "i"(K), [g] "i"(G), [s] "i"(STEP) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}
template <int N, int K, int G> __attribute__((noinline)) uint32_t tB(uint32_t) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".rept %[n]\n\tnop\n\t.rept %[k]\n\tnop\n\t.endr\n\t.endr\n\t.rept %[g]\n\tnop\n\t.endr\n\t" POST
                     : [t0] "=&r"(t0), [t1] "=&r"(t1) : [n] "i"(N), [k] "i"(K), [g] "i"(G) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}

#define ROWG(N, K, STEP) { tS<N,K,0,STEP>, tS<N,K,1,STEP>, tS<N,K,2,STEP>, tS<N,K,3,STEP>, tS<N,K,4,STEP>, \
                           tS<N,K,5,STEP>, tS<N,K,6,STEP>, tS<N,K,7,STEP>, tS<N,K,8,STEP> }
#define BASEG(N, K) { tB<N,K,0>, tB<N,K,1>, tB<N,K,2>, tB<N,K,3>, tB<N,K,4>, tB<N,K,5>, tB<N,K,6>, tB<N,K,7>, tB<N,K,8> }
static const fn S0o[9] = ROWG(16, 0, 0), S1o[9] = ROWG(16, 1, 0), S2o[9] = ROWG(16, 2, 0);
static const fn S0c[9] = ROWG(16, 0, 4), S1c[9] = ROWG(16, 1, 4), S2c[9] = ROWG(16, 2, 4);
static const fn B0[9] = BASEG(16, 0), B1[9] = BASEG(16, 1), B2[9] = BASEG(16, 2);
static const fn S1x[9] = ROWG(1, 0, 0), B1x[9] = BASEG(1, 0);

static uint32_t twice(fn f, uint32_t p) { f(p); return f(p); }
static void hund(int32_t v) { if (v < 0) { printf("-"); v = -v; } printf("%d.%02d", v / 100, v % 100); }
static void row(const char* name, const fn* s, const fn* b, uint32_t p) {
    printf("  %-26s", name);
    for (int i = 0; i < 9; i++) {
        const int32_t d = ((int32_t)twice(s[i], p) - (int32_t)twice(b[i], p)) * 100 / 256;
        printf(" "); hund(d);
    }
    printf("\n");
}
static void raw(const char* name, const fn* s, uint32_t p) {
    printf("  %-26s", name);
    for (int i = 0; i < 9; i++) { printf(" "); hund((int32_t)twice(s[i], p) * 100 / 256); }
    printf("\n");
}

int main() {
    printf("\ncpu/wr-stream\n");
    printf("16 x (sw, K nops) then G nops a pass, 256 passes back to back: cycles a pass above the same with nops, G = 0..8.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t p = (uint32_t)wsBuf;
    row("one word, K = 0", S0o, B0, p);
    row("one word, K = 1", S1o, B1, p);
    row("one word, K = 2", S2o, B2, p);
    row("16 words, K = 0", S0c, B0, p);
    row("16 words, K = 1", S1c, B1, p);
    row("16 words, K = 2", S2c, B2, p);
    row("one store a pass", S1x, B1x, p);
    printf("\n  cycles a pass, as DIAG prints them (its loop is this one at G = 0):\n");
    raw("one word, K = 0", S0o, p);
    raw("one word, K = 1", S1o, p);
    raw("one store a pass", S1x, p);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
