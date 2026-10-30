/*
 * cpu/counter-bracket - how far apart are two timer reads, and what moves
 * the second one?
 *
 * WHY (ROAD A8 "the counter-read bracket"; STATUS M207, left open; ROAD B).
 * Every hardware timing test here brackets its code between two reads of a
 * counter, so the bracket itself is inside every number. PCSX-Redux's
 * load-timings (silicon) reads every row of its loadShadow and loadInterlocked
 * tables exactly 3 cycles below this core, and its empty bracket - two
 * counter reads with nothing between - is 4 on silicon and 5 here. M205 found
 * nocash's DIAG wants the counter sampled early in a read and ps1-tests'
 * timers wants it late, and one sample point cannot satisfy both. Nothing
 * separates the pieces: the read's own cost, where in it the counter is
 * sampled, and what a store or the write queue ahead of it does.
 *
 * WHAT IT DOES. Timer 0 and Timer 2 free-running at the system clock,
 * interrupts off. Each sequence is a fixed run of instructions (`.set
 * noreorder`), from CACHED code (its second call: the lines already in the
 * I-cache) and UNCACHED code (its KSEG1 alias):
 *   read, N nops, read     two lhu of Timer 2, N = 0..8 between: the empty
 *                          bracket (N = 0) and the cost of a nop
 *   read, sw RAM, N, read  a store to RAM between, N = 0..8: the store and
 *                          the write queue
 *   read, 3 sw, read       three stores queued
 *   read, sw I/O, read     a store to an I/O register (Timer 1's target)
 *   T0 then T2, T2 then T0 two DIFFERENT counters: their mean is the interval
 *                          between two reads of different registers, their
 *                          half-difference the two timers' offset
 *
 * READ IT LIKE THIS. Each number is the second read minus the first, in
 * cycles. N = 0 is the bracket Redux's test pays on every row (4 on silicon,
 * 5 here); the slope over N is what a nop costs in each kind of code; the
 * store rows say whether a store ahead of a read delays the read's sample.
 *
 * ANSWERED IN PART by the SCPH-5501, 7501 and 9001 (v0.377.25-.26): a timer read waits for the write queue less its
 * last 2 cycles (IO_WB_OVERLAP), lhu b in lhu a's shadow issues there (shadowAhead), and from uncached code the fetch
 * right behind an on-die load runs in its stall. AT v0.377.26 every cached row is the consoles' (4 6 6 6 7 8 9 10 11;
 * 8 8 8 9 10 11 12 13 14); uncached 8 13 18 24 30 36 42 48 54 and 20 26 32 38 44 50 60 62 68 (the consoles' rows, a
 * DRAM refresh landing on other cells); 3 sw 12 / 43, sw I/O 6 / 15. OPEN: the T0/T2 offset, 5.0 on the consoles from
 * cached and uncached code alike and 16.0 here - two I-cache fills sit between the MODE writes here, which no 5 cycles
 * can hold, so the question is when an I/O store takes effect, not the counter.
 * ON THIS CORE AT v0.258 (`node hwtests/run.js counter-bracket`):
 *                        N=0  1  2  3  4  5  6  7  8
 *   cached  read,N,read    4  6  7  7  7  8  9 10 11
 *   cached  read,sw,N,read 6  7  8  9 10 11 12 13 14
 *   uncached read,N,read   8 15 21 27 33 39 45 51 57
 *   uncached read,sw,N,read 20 26 32 38 44 54 56 62 72
 *   cached: 3 sw 8, sw I/O 6, two counters 4.0 apart; uncached: 42, 15, 8.0.
 * The empty bracket reads 4 here - Redux's "5" (STATUS M207) is its own
 * sequence, which ends in a nop and an instruction using the loaded value.
 * The flat N = 2..4 is the LOAD SHADOW (M207's retail pipeline: independent
 * instructions run during a load's bus access), so the nops there cost
 * nothing. A console's row says whether that shadow exists and how long it
 * is; uncached, a fetch costs ~6 a nop here.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T1_TARGET 0x1F801118u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramWord;

typedef uint32_t (*Seq)(uint32_t, uint32_t, uint32_t);

#define RR(K) \
static uint32_t __attribute__((noinline, aligned(64))) rr##K(uint32_t t, uint32_t, uint32_t) { \
    uint32_t a, b; \
    __asm__ volatile(".set noreorder\n lhu %0, 0(%2)\n .rept " #K "\n nop\n .endr\n lhu %1, 0(%2)\n nop\n .set reorder\n" \
                     : "=&r"(a), "=&r"(b) : "r"(t) : "memory"); \
    return (b - a) & 0xFFFF; }
#define SR(K) \
static uint32_t __attribute__((noinline, aligned(64))) sr##K(uint32_t t, uint32_t m, uint32_t) { \
    uint32_t a, b; \
    __asm__ volatile(".set noreorder\n lhu %0, 0(%2)\n sw %0, 0(%3)\n .rept " #K "\n nop\n .endr\n lhu %1, 0(%2)\n nop\n .set reorder\n" \
                     : "=&r"(a), "=&r"(b) : "r"(t), "r"(m) : "memory"); \
    return (b - a) & 0xFFFF; }
RR(0) RR(1) RR(2) RR(3) RR(4) RR(5) RR(6) RR(7) RR(8)
SR(0) SR(1) SR(2) SR(3) SR(4) SR(5) SR(6) SR(7) SR(8)

static uint32_t __attribute__((noinline, aligned(64))) s3r(uint32_t t, uint32_t m, uint32_t) {
    uint32_t a, b;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%2)\n sw %0, 0(%3)\n sw %0, 4(%3)\n sw %0, 8(%3)\n lhu %1, 0(%2)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b) : "r"(t), "r"(m) : "memory");
    return (b - a) & 0xFFFF;
}
static uint32_t __attribute__((noinline, aligned(64))) sior(uint32_t t, uint32_t io, uint32_t v) {
    uint32_t a, b;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%2)\n sw %4, 0(%3)\n lhu %1, 0(%2)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b) : "r"(t), "r"(io), "r"(v) : "memory");
    return (b - a) & 0xFFFF;
}
/* First counter at `t`, second at `u`: returns second - first. */
static uint32_t __attribute__((noinline, aligned(64))) two(uint32_t t, uint32_t u, uint32_t) {
    uint32_t a, b;
    __asm__ volatile(".set noreorder\n lhu %0, 0(%2)\n lhu %1, 0(%3)\n nop\n .set reorder\n"
                     : "=&r"(a), "=&r"(b) : "r"(t), "r"(u) : "memory");
    return (b - a) & 0xFFFF;
}

static Seq uncached(Seq f) { return (Seq)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

/* The second of two calls: cached code then runs from lines the first call filled. */
static uint32_t run(Seq f, bool unc, uint32_t a, uint32_t b, uint32_t c) {
    Seq g = unc ? uncached(f) : f;
    g(a, b, c);
    return g(a, b, c);
}

static int16_t s16(uint32_t v) { return (int16_t)(v & 0xFFFF); }

int main() {
    printf("\ncpu/counter-bracket\n");
    printf("Cycles between two counter reads (Timer 0 and 2 at the system clock), and what moves them.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint32_t m0 = read16(T0_MODE), m2 = read16(T2_MODE), t1t = read16(T1_TARGET);
    write16(T0_MODE, 0);
    write16(T2_MODE, 0);
    static const Seq rrs[9] = { rr0, rr1, rr2, rr3, rr4, rr5, rr6, rr7, rr8 };
    static const Seq srs[9] = { sr0, sr1, sr2, sr3, sr4, sr5, sr6, sr7, sr8 };
    uint32_t vrr[2][9], vsr[2][9], v3[2], vio[2], v02[2], v20[2];
    for (int u = 0; u < 2; u++) {
        for (int k = 0; k < 9; k++) {
            vrr[u][k] = run(rrs[k], u, T2_COUNT, 0, 0);
            vsr[u][k] = run(srs[k], u, T2_COUNT, (uint32_t)&ramWord, 0);
        }
        v3[u] = run(s3r, u, T2_COUNT, (uint32_t)&ramWord, 0);
        vio[u] = run(sior, u, T2_COUNT, T1_TARGET, t1t);
        v02[u] = run(two, u, T0_COUNT, T2_COUNT, 0);
        v20[u] = run(two, u, T2_COUNT, T0_COUNT, 0);
    }
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("                       N=0  N=1  N=2  N=3  N=4  N=5  N=6  N=7  N=8\n");
    for (int u = 0; u < 2; u++) {
        printf("%s read,N,read", u ? "uncached" : "cached  ");
        for (int k = 0; k < 9; k++) printf(" %4d", (int)vrr[u][k]);
        printf("\n%s read,sw,N,read", u ? "uncached" : "cached  ");
        for (int k = 0; k < 9; k++) printf("%s%4d", k ? " " : "", (int)vsr[u][k]);
        printf("\n");
    }
    printf("\n");
    for (int u = 0; u < 2; u++) {
        const int d02 = s16(v02[u]), d20 = s16(v20[u]);
        printf("%s read, 3 sw, read %4d   read, sw I/O, read %4d   T0->T2 %5d  T2->T0 %5d  (interval %d.%d, offset %d.%d)\n",
               u ? "uncached" : "cached  ", (int)v3[u], (int)vio[u], d02, d20,
               (d02 + d20) / 2, ((d02 + d20) & 1) ? 5 : 0, (d02 - d20) / 2, ((d02 - d20) & 1) ? 5 : 0);
    }
    printf("\ncpu/counter-bracket: Done.\n");
    for (;;) VSync(0);
}
