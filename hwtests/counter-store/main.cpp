/*
 * cpu/counter-store - when does a store to a counter take effect, as the
 * next read of it sees it?
 *
 * WHY (ROAD A8 "where an I/O read samples inside its stall", dug 2026-09-27;
 * ROAD B "I/O read sample point"). hwtests/counter-bracket times two READS;
 * the other half of the question is a WRITE then a read. Two console logs
 * time exactly that and disagree with this core in opposite directions:
 * ps1-tests' timers reads its write-to-read first sample 3 SHORT here (245
 * against 248) and nocash's DIAG MUL/DIV TIME bracket 3 LONG (789 against
 * 787, STATUS M207). Opposite signs point at WHEN an I/O store takes effect
 * - Sony's write buffer (tr41 _DEVREFS_HARDWARE) against psx-spx's "seems to
 * be disabled" for the I/O area - and not only at where a read samples.
 *
 * WHAT IT DOES. Timer 2 free-running at the system clock, interrupts off.
 * Each row stores to Timer 2 and reads it back as a fixed run of
 * instructions (`.set noreorder`), from CACHED code (its second call) and
 * UNCACHED code (its KSEG1 alias):
 *   sh 0, N nops, read    the counter set to 0 by a halfword store, N = 0..8
 *   sw 0, N nops, read    the same by a word store
 *   mode, N nops, read    a store to Timer 2's MODE (which resets the counter,
 *                         psx-spx) then N nops, N = 0..8
 *   Q sw RAM, sh 0, read  Q = 1..4 RAM stores queued ahead of the counter
 *                         store: does the write queue hold the I/O store back?
 *   sh 0, sw RAM, read    a RAM store between the counter store and the read
 *
 * READ IT LIKE THIS. Each number is the value read: the cycles from the
 * moment the store set the counter to the moment the read sampled it. The
 * slope over N is a nop's cost (the load shadow does not apply: nothing is
 * loaded before the read); N = 0 is the store's effect and the read's sample
 * together. The queued rows grow if the I/O store waits behind the RAM
 * stores (a write queue it shares) and stay flat if it does not.
 *
 * ANSWERED (v0.377.27, src/psx.js TIMER_WRITE_HOLD) by the SCPH-5501, 7501 and 9001: the counter keeps the value
 * written until the write queue is empty and 3 cycles have passed, a RAM store queued inside those 3 cycles holding it
 * too. AT v0.377.27 every row is the consoles': cached 0 0 1 2 3 4 5 6 7 (all three); uncached 4 10 16 22 28 34 40 46
 * 52 (a DRAM refresh lands on a few cells, as on the consoles); Q = 1..4 0 0 0 0 cached, 4 4 4 4 uncached; a RAM
 * store behind it 0 cached, 15 uncached.
 * ON THIS CORE AT v0.258 (`node hwtests/run.js counter-store`):
 *                         N=0  1  2  3  4  5  6  7  8
 *   cached   sh/sw/mode     2  3  4  5  6  7  8  9 10   (all three alike)
 *   uncached sh/sw/mode     7 13 19 25 31 37 43 49 55   (two rows meet a DRAM
 *                                                        refresh: +10 once)
 *   Q = 1..4 RAM stores queued ahead: 2 2 2 2 cached, 7 7 7 7 uncached - the
 *   counter store does not wait behind them here; a RAM store between the
 *   counter store and the read: 3 cached, 18 uncached.
 * With counter-bracket (two reads 4 apart cached, 8 uncached), a store to a
 * counter reaches the next read of it 2 cycles sooner than a read does. A
 * console whose Q rows fall with Q is making the I/O store wait for the RAM
 * stores ahead of it - the write buffer psx-spx says the I/O area bypasses.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramWords[8];

typedef uint32_t (*Seq)(uint32_t, uint32_t);

#define WS(K) \
static uint32_t __attribute__((noinline, aligned(64))) ws##K(uint32_t t, uint32_t) { \
    uint32_t v; \
    __asm__ volatile(".set noreorder\n sh $zero, 0(%1)\n .rept " #K "\n nop\n .endr\n lhu %0, 0(%1)\n nop\n .set reorder\n" \
                     : "=&r"(v) : "r"(t) : "memory"); \
    return v & 0xFFFF; }
#define WW(K) \
static uint32_t __attribute__((noinline, aligned(64))) ww##K(uint32_t t, uint32_t) { \
    uint32_t v; \
    __asm__ volatile(".set noreorder\n sw $zero, 0(%1)\n .rept " #K "\n nop\n .endr\n lhu %0, 0(%1)\n nop\n .set reorder\n" \
                     : "=&r"(v) : "r"(t) : "memory"); \
    return v & 0xFFFF; }
#define WM(K) \
static uint32_t __attribute__((noinline, aligned(64))) wm##K(uint32_t t, uint32_t) { \
    uint32_t v; \
    __asm__ volatile(".set noreorder\n sh $zero, 4(%1)\n .rept " #K "\n nop\n .endr\n lhu %0, 0(%1)\n nop\n .set reorder\n" \
                     : "=&r"(v) : "r"(t) : "memory"); \
    return v & 0xFFFF; }
WS(0) WS(1) WS(2) WS(3) WS(4) WS(5) WS(6) WS(7) WS(8)
WW(0) WW(1) WW(2) WW(3) WW(4) WW(5) WW(6) WW(7) WW(8)
WM(0) WM(1) WM(2) WM(3) WM(4) WM(5) WM(6) WM(7) WM(8)

/* Q RAM stores queued ahead of the counter store (Q = 1..4), then the read. */
#define QS(Q, STORES) \
static uint32_t __attribute__((noinline, aligned(64))) qs##Q(uint32_t t, uint32_t m) { \
    uint32_t v; \
    __asm__ volatile(".set noreorder\n" STORES " sh $zero, 0(%1)\n lhu %0, 0(%1)\n nop\n .set reorder\n" \
                     : "=&r"(v) : "r"(t), "r"(m) : "memory"); \
    return v & 0xFFFF; }
QS(1, " sw $zero, 0(%2)\n")
QS(2, " sw $zero, 0(%2)\n sw $zero, 4(%2)\n")
QS(3, " sw $zero, 0(%2)\n sw $zero, 4(%2)\n sw $zero, 8(%2)\n")
QS(4, " sw $zero, 0(%2)\n sw $zero, 4(%2)\n sw $zero, 8(%2)\n sw $zero, 12(%2)\n")

/* A RAM store between the counter store and the read. */
static uint32_t __attribute__((noinline, aligned(64))) sbetween(uint32_t t, uint32_t m) {
    uint32_t v;
    __asm__ volatile(".set noreorder\n sh $zero, 0(%1)\n sw $zero, 0(%2)\n lhu %0, 0(%1)\n nop\n .set reorder\n"
                     : "=&r"(v) : "r"(t), "r"(m) : "memory");
    return v & 0xFFFF;
}

static Seq uncached(Seq f) { return (Seq)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }

/* The second of two calls: cached code then runs from lines the first call filled. */
static uint32_t run(Seq f, int unc) {
    Seq g = unc ? uncached(f) : f;
    g(T2_COUNT, (uint32_t)ramWords);
    return g(T2_COUNT, (uint32_t)ramWords);
}

int main() {
    printf("\ncpu/counter-store\n");
    printf("A store to Timer 2 (system clock), then a read of it: the value read is the cycles between them.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint32_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    static const Seq wss[9] = { ws0, ws1, ws2, ws3, ws4, ws5, ws6, ws7, ws8 };
    static const Seq wws[9] = { ww0, ww1, ww2, ww3, ww4, ww5, ww6, ww7, ww8 };
    static const Seq wms[9] = { wm0, wm1, wm2, wm3, wm4, wm5, wm6, wm7, wm8 };
    static const Seq qss[4] = { qs1, qs2, qs3, qs4 };
    uint32_t vs[2][9], vw[2][9], vm[2][9], vq[2][4], vb[2];
    for (int u = 0; u < 2; u++) {
        for (int k = 0; k < 9; k++) {
            vs[u][k] = run(wss[k], u);
            vw[u][k] = run(wws[k], u);
            vm[u][k] = run(wms[k], u);          /* the mode written is 0: free-running at the system clock, as set */
        }
        for (int q = 0; q < 4; q++) vq[u][q] = run(qss[q], u);
        vb[u] = run(sbetween, u);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("                        N=0  N=1  N=2  N=3  N=4  N=5  N=6  N=7  N=8\n");
    for (int u = 0; u < 2; u++) {
        const char *c = u ? "uncached" : "cached  ";
        printf("%s sh 0, N, read ", c);
        for (int k = 0; k < 9; k++) printf(" %4d", (int)vs[u][k]);
        printf("\n%s sw 0, N, read ", c);
        for (int k = 0; k < 9; k++) printf(" %4d", (int)vw[u][k]);
        printf("\n%s mode, N, read ", c);
        for (int k = 0; k < 9; k++) printf(" %4d", (int)vm[u][k]);
        printf("\n");
    }
    printf("\n                        Q=1  Q=2  Q=3  Q=4   sh 0, sw RAM, read\n");
    for (int u = 0; u < 2; u++) {
        printf("%s Q sw, sh 0, read", u ? "uncached" : "cached  ");
        for (int q = 0; q < 4; q++) printf("%s%4d", q ? " " : "", (int)vq[u][q]);
        printf("   %4d\n", (int)vb[u]);
    }
    printf("\ncpu/counter-store: Done.\n");
    for (;;) VSync(0);
}
