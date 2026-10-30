/*
 * cpu/wb-drain - how the write buffer takes and drains a run of RAM stores.
 *
 * WHY (v0.377.28). hwtests/cpu-extras' WRITE BUFFER row on the SCPH-5501, 7501 and 9001: N stores to consecutive words
 * back to back, then 40 nops, cost 0 0 0 0 0 2.25 2.19 4.14 cycles above N nops for N = 1..8. This core's queue (four
 * words, the first word 6 bus cycles and each after it 2 on the same page, a slot freed as its word is written; M207,
 * from Sony's bus capture and pcsx-redux PR #2091) reads 0 0 0 0 2.08 3.22 4.31 5.88 - it stalls a store sooner (the
 * fifth) and a cycle a store after that, where the consoles stall the sixth and then two cycles every second store. Five
 * entries freed two at a time would read 0 0 0 0 0 2 2 4; one row does not decide a model. This maps it.
 *
 * WHAT IT DOES. Each row: N stores (sw to consecutive words of one 2 KB page) with K nops between each, then 48 nops
 * (the queue empty again), 256 times, against the same with nops for the stores; cycles per iteration above that,
 * Timer 2 at the system clock, interrupts off. N = 1..12, K = 0..3. Then N = 1..8 with K = 0 to two words 4 KB apart
 * (another DRAM page each time), and N = 1..8 halfword stores (sh) to consecutive halfwords.
 *
 * READ IT LIKE THIS. The first N that costs anything is the depth (plus whatever the first word takes); the step
 * between later N is the drain per word as the CPU sees it, and a staircase that rises every second N is a buffer that
 * frees two entries at once. With K nops between, the stores issue slower than the drain and the stall moves out.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js wb-drain`.
 * ON THIS CORE AT v0.377.28 (four words, freed one at a time): sw K = 0: 0 0 0 0.04 2.19 3.21 5.91 5.01 6.35 7.48
 *   8.58 10.19 (refresh draws make N = 7 read above N = 8); K = 1: 0 to N = 5, then 0.16-0.91; K = 2, 3: ~0; 4 KB apart
 *   0 0 0 0 2.17 7.00 12.19 17.44 (a new burst, 6, each); sh as sw.
 * ON THE SCPH-5501 AND 7501 (2026-10-08, under the runner, two runs each, all four alike): sw K = 0 0 0 0 0.05 0.84
 *   2.05 2.25 4.90 4.01 6.38 6.69 8.68; 4 KB apart 0.04 0 0 0 0.87 6.89 11.53 16.31; sh 0.89 2.05 2.24 4.90 at N = 5..8.
 * ON THIS CORE AT v0.377.53 (a slot frees a cycle before its word ends, a page burst's later words in pairs, a burst
 *   goes on only while its next word is queued 3 cycles ahead, a refresh that meets one costs a cycle more - src/psx.js
 *   WRITE_QUEUE_FREE_EARLY, WRITE_QUEUE_CONT_LEAD): sw K = 0 ... 1.03 2.07 2.38 4.91 4.00 6.50 6.76 8.85; K = 2 N = 11
 *   1.25 (consoles 1.23); 4 KB apart 1.03 6.91 11.41 16.41; RMS 0.076 cycles over the 64 numbers against the consoles'
 *   mean (0.888 at v0.377.52).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u

uint32_t wbBuf[8 * 1024 + 64] __attribute__((aligned(4096)));   /* 32 KB: the far row writes 8 words 4 KB apart */

typedef uint32_t (*fn)(uint32_t);

#define PRE  ".set push\n\t.set noreorder\n\t.set noat\n\t" \
             "lui $t9, 0x1f80\n\tli $t8, 256\n\tlhu %[t0], 0x1120($t9)\n\tnop\n1:\n\t"
#define POST ".rept 48\n\tnop\n\t.endr\n\taddiu $t8, $t8, -1\n\tbnez $t8, 1b\n\tnop\n\tlhu %[t1], 0x1120($t9)\n\tnop\n\t.set pop\n\t"

/* N stores, K nops after each; STEP bytes apart; OP the store. */
template <int N, int K, int STEP, int H> __attribute__((noinline)) uint32_t tS(uint32_t p) {
    uint32_t t0, t1;
    if (H) __asm__ volatile(PRE ".set wbo, 0\n\t.rept %[n]\n\tsh $zero, wbo(%[p])\n\t.rept %[k]\n\tnop\n\t.endr\n\t.set wbo, wbo + %[s]\n\t.endr\n\t" POST
                            : [t0] "=&r"(t0), [t1] "=&r"(t1) : [p] "r"(p), [n] "i"(N), [k] "i"(K), [s] "i"(STEP) : "t8", "t9", "memory");
    else   __asm__ volatile(PRE ".set wbo, 0\n\t.rept %[n]\n\tsw $zero, wbo(%[p])\n\t.rept %[k]\n\tnop\n\t.endr\n\t.set wbo, wbo + %[s]\n\t.endr\n\t" POST
                            : [t0] "=&r"(t0), [t1] "=&r"(t1) : [p] "r"(p), [n] "i"(N), [k] "i"(K), [s] "i"(STEP) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}
template <int N, int K> __attribute__((noinline)) uint32_t tB(uint32_t) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".rept %[n]\n\tnop\n\t.rept %[k]\n\tnop\n\t.endr\n\t.endr\n\t" POST
                     : [t0] "=&r"(t0), [t1] "=&r"(t1) : [n] "i"(N), [k] "i"(K) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}

#define ROW12(K, STEP, H) { tS<1,K,STEP,H>, tS<2,K,STEP,H>, tS<3,K,STEP,H>, tS<4,K,STEP,H>, tS<5,K,STEP,H>, tS<6,K,STEP,H>, \
                            tS<7,K,STEP,H>, tS<8,K,STEP,H>, tS<9,K,STEP,H>, tS<10,K,STEP,H>, tS<11,K,STEP,H>, tS<12,K,STEP,H> }
#define BASE12(K) { tB<1,K>, tB<2,K>, tB<3,K>, tB<4,K>, tB<5,K>, tB<6,K>, tB<7,K>, tB<8,K>, tB<9,K>, tB<10,K>, tB<11,K>, tB<12,K> }
static const fn S0[12] = ROW12(0, 4, 0), S1[12] = ROW12(1, 4, 0), S2[12] = ROW12(2, 4, 0), S3[12] = ROW12(3, 4, 0);
static const fn B0[12] = BASE12(0), B1[12] = BASE12(1), B2[12] = BASE12(2), B3[12] = BASE12(3);
#define ROW8(K, STEP, H) { tS<1,K,STEP,H>, tS<2,K,STEP,H>, tS<3,K,STEP,H>, tS<4,K,STEP,H>, tS<5,K,STEP,H>, tS<6,K,STEP,H>, tS<7,K,STEP,H>, tS<8,K,STEP,H> }
static const fn FAR[8] = ROW8(0, 4096, 0);
static const fn HALF[8] = ROW8(0, 2, 1);

static uint32_t twice(fn f, uint32_t p) { f(p); return f(p); }
static void hund(int32_t v) { if (v < 0) { printf("-"); v = -v; } printf("%d.%02d", v / 100, v % 100); }
static void row(const char* name, const fn* s, const fn* b, int n, uint32_t p) {
    printf("  %-22s", name);
    for (int i = 0; i < n; i++) {
        const int32_t d = ((int32_t)twice(s[i], p) - (int32_t)twice(b[i], p)) * 100 / 256;
        printf(" "); hund(d);
    }
    printf("\n");
}

int main() {
    printf("\ncpu/wb-drain\n");
    printf("N RAM stores (K nops after each), then 48 nops: cycles per iteration above the same with nops, N = 1..12.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t p = (uint32_t)wbBuf;
    row("sw, K = 0", S0, B0, 12, p);
    row("sw, K = 1", S1, B1, 12, p);
    row("sw, K = 2", S2, B2, 12, p);
    row("sw, K = 3", S3, B3, 12, p);
    printf("\n  N = 1..8:\n");
    row("sw 4 KB apart, K = 0", FAR, B0, 8, p);
    row("sh, K = 0", HALF, B0, 8, p);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
