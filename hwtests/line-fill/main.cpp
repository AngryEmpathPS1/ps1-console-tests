/*
 * cpu/line-fill - how far apart are two instruction-cache line fills from RAM
 * on a RETAIL console?
 *
 * WHY (ROAD A10, M236; STATUS M160/M167). Sony's bus analyzer capture (TUTO3.PAD,
 * a DTL-H2700 dev board) shows straight-line uncached-in-the-cache code
 * fetching a 4-word line every 9 cycles; this core fills one every 8. psx-spx
 * 6010b13 ties the ninth cycle to DRAM_CTRL bit 7, and the dev board runs a
 * cycle off retail on exactly these accesses (M207/M209: back-to-back RAM
 * loads 6 apart there, 7 on retail). So the 9 may be the dev board's and the
 * 8 right for retail - or not. Nothing retail has measured it.
 *
 * WHAT IT DOES. One 4 KB function - the whole I-cache - of 1,024 nops in 256
 * lines, entered at a line boundary N lines before its end, so N lines run.
 * For N = 16, 32, 64, 128 and 255: FlushCache (A0h 44h), time a call (COLD),
 * time it again (WARM, all hits). Timer2 counts the system clock. Each N five
 * times. COLD - WARM over N lines is the fill's cost; its slope between N
 * values drops everything fixed (the call, the return path's own misses).
 *
 * READ IT LIKE THIS. The warm rows are N x 4 + a constant - a check that the
 * clock and the entry points are right. The cold slope per line is the fill
 * interval: 8 if a fill streams straight into the next, 9 with the dev board's
 * extra cycle.
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js line-fill`): warm
 * 77 / 147 / 283 / 539 / 1,047 for N = 16..255 (N x 4 + 13: the clock and the
 * entry points are right); cold 141-146 / 274 / 537 / 1,059 / 2,095; the cold
 * slope 8.31 / 8.21 / 8.15 / 8.15 a line - this core's 8, plus DRAM refresh
 * (5 bus cycles every 256, M168). The dev board's 9 would read ~9.2.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

extern "C" void lines256(void);
__asm__(
    ".text\n"
    ".balign 16\n"
    ".global lines256\n"
    "lines256:\n"
    ".set push\n.set noreorder\n"
    ".rept 1020\n\tnop\n\t.endr\n"
    "jr $ra\n"
    "nop\n"
    "nop\n"
    "nop\n"
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

int main() {
    printf("\ncpu/line-fill\n");
    printf("Instruction-cache line fills back to back: 8 or 9 cycles apart on retail? (dev board: 9)\n");
    printf("A call of N lines of nops, cold (after FlushCache) and warm, Timer2 at the system clock.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    const uint16_t prev = initTimer(2, 0);        /* Timer2: system clock */
    EnterCriticalSection();
    static const int ns[] = { 16, 32, 64, 128, 255 };
    uint32_t coldMin[5], warmMin[5];
    for (int k = 0; k < 5; k++) {
        const int n = ns[k];
        const fn f = (fn)((uint32_t)lines256 + (256 - n) * 16);
        coldMin[k] = warmMin[k] = 0xFFFFFFFF;
        printf("N=%3d lines  cold", n);
        uint32_t w[5];
        for (int run = 0; run < 5; run++) {
            flushCache();
            const uint32_t c = timeCall(f);
            w[run] = timeCall(f);
            printf(" %5d", c);
            if (c < coldMin[k]) coldMin[k] = c;
            if (w[run] < warmMin[k]) warmMin[k] = w[run];
        }
        printf("   warm");
        for (int run = 0; run < 5; run++) printf(" %5d", w[run]);
        printf("   cold-warm %d = %d.%02d a line\n", coldMin[k] - warmMin[k],
               (coldMin[k] - warmMin[k]) / n, (coldMin[k] - warmMin[k]) * 100 / n % 100);
    }
    printf("\nslopes (min cold between N values, per line):");
    for (int k = 1; k < 5; k++) {
        const uint32_t d = coldMin[k] - coldMin[k - 1], dn = ns[k] - ns[k - 1];
        printf("  %d-%d: %d.%02d", ns[k - 1], ns[k], d / dn, d * 100 / dn % 100);
    }
    printf("\n");
    ExitCriticalSection();
    restoreTimer(2, prev);
    printf("\nDone.\n");
    for (;;) { }
}
