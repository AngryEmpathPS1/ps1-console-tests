/*
 * cpu/wb-page2 - what lets the write buffer's burst go on: the next word, or any word of a small block? (2026-10-11)
 *
 * WHY. hwtests/wb-page (the SCPH-5501 x3, 2026-10-11): stores 1 KB, 2 KB and 4 KB apart never pair, and a run of
 * consecutive words breaks its burst crossing a 2 KB boundary as at a 4 KB one. This core's queue (src/psx.js queueWrite,
 * M207) goes on with a burst anywhere in a 2 KB page - so stores 1 KB apart pair here and do not on the console. Either
 * the burst needs the next address (a CAS run up the row: one word after the last) or it needs a block smaller than
 * 1 KB. This separates them.
 *
 * WHAT IT DOES. As wb-page (cycles per iteration above the same with nops, N = 1..12 stores back to back then 48 nops,
 * 256 times, Timer 2 at the system clock, interrupts off), in a 4 KB-aligned buffer:
 *   same word         N stores to one word (STEP 0)
 *   halfwords         N consecutive halfwords (sh, STEP 2)
 *   8 apart ... 512 apart   N words STEP bytes apart (8, 16, 32, 64, 128, 256, 512)
 *   across 256 / 512 / 1 KB   N consecutive words from 16 bytes before that boundary: the fifth crosses it
 *
 * READ IT LIKE THIS. wb-page's "at 0, words" (consecutive words, one burst) read 0 0 0 -0.01 0.89 2.06 2.28 4.90 4.04
 * 6.42 6.70 8.68 on the 5501; a new burst each store, 0.84 6.85 11.55 16.32 at N = 5..8. A row reading like the first
 * keeps its burst; like the second, it does not. Same word and the strides say whether "next address" or "small block";
 * the crossings say where a block ends if it is one.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js wb-page2`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u

uint32_t wbBuf[2 * 1024 + 64] __attribute__((aligned(4096)));   /* 8 KB: the widest row writes 12 words 512 bytes apart */

typedef uint32_t (*fn)(uint32_t);

#define PRE  ".set push\n\t.set noreorder\n\t.set noat\n\t" \
             "lui $t9, 0x1f80\n\tli $t8, 256\n\tlhu %[t0], 0x1120($t9)\n\tnop\n1:\n\t"
#define POST ".rept 48\n\tnop\n\t.endr\n\taddiu $t8, $t8, -1\n\tbnez $t8, 1b\n\tnop\n\tlhu %[t1], 0x1120($t9)\n\tnop\n\t.set pop\n\t"

/* N stores STEP bytes apart from OFF bytes into the buffer (H: halfword stores). */
template <int N, int OFF, int STEP, int H> __attribute__((noinline)) uint32_t tS(uint32_t p) {
    uint32_t t0, t1;
    if (H) __asm__ volatile(PRE ".set wbo, %[o]\n\t.rept %[n]\n\tsh $zero, wbo(%[p])\n\t.set wbo, wbo + %[s]\n\t.endr\n\t" POST
                            : [t0] "=&r"(t0), [t1] "=&r"(t1) : [p] "r"(p), [n] "i"(N), [o] "i"(OFF), [s] "i"(STEP) : "t8", "t9", "memory");
    else   __asm__ volatile(PRE ".set wbo, %[o]\n\t.rept %[n]\n\tsw $zero, wbo(%[p])\n\t.set wbo, wbo + %[s]\n\t.endr\n\t" POST
                            : [t0] "=&r"(t0), [t1] "=&r"(t1) : [p] "r"(p), [n] "i"(N), [o] "i"(OFF), [s] "i"(STEP) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}
template <int N> __attribute__((noinline)) uint32_t tB(uint32_t) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".rept %[n]\n\tnop\n\t.endr\n\t" POST : [t0] "=&r"(t0), [t1] "=&r"(t1) : [n] "i"(N) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}

#define ROW12(OFF, STEP, H) { tS<1,OFF,STEP,H>, tS<2,OFF,STEP,H>, tS<3,OFF,STEP,H>, tS<4,OFF,STEP,H>, tS<5,OFF,STEP,H>, tS<6,OFF,STEP,H>, \
                              tS<7,OFF,STEP,H>, tS<8,OFF,STEP,H>, tS<9,OFF,STEP,H>, tS<10,OFF,STEP,H>, tS<11,OFF,STEP,H>, tS<12,OFF,STEP,H> }
static const fn B[12] = { tB<1>, tB<2>, tB<3>, tB<4>, tB<5>, tB<6>, tB<7>, tB<8>, tB<9>, tB<10>, tB<11>, tB<12> };
static const fn SAME[12] = ROW12(0, 0, 0), HALF[12] = ROW12(0, 2, 1);
static const fn S8[12] = ROW12(0, 8, 0), S16[12] = ROW12(0, 16, 0), S32[12] = ROW12(0, 32, 0), S64[12] = ROW12(0, 64, 0);
static const fn S128[12] = ROW12(0, 128, 0), S256[12] = ROW12(0, 256, 0), S512[12] = ROW12(0, 512, 0);
static const fn X256[12] = ROW12(256 - 16, 4, 0), X512[12] = ROW12(512 - 16, 4, 0), X1K[12] = ROW12(1024 - 16, 4, 0);

static uint32_t twice(fn f, uint32_t p) { f(p); return f(p); }
static void hund(int32_t v) { if (v < 0) { printf("-"); v = -v; } printf("%d.%02d", v / 100, v % 100); }
static void row(const char* name, const fn* s, uint32_t p) {
    printf("  %-18s", name);
    for (int i = 0; i < 12; i++) {
        const int32_t d = ((int32_t)twice(s[i], p) - (int32_t)twice(B[i], p)) * 100 / 256;
        printf(" "); hund(d);
    }
    printf("\n");
}

int main() {
    printf("\ncpu/wb-page2\n");
    printf("N RAM stores back to back, then 48 nops: cycles per iteration above the same with nops, N = 1..12.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t p = (uint32_t)wbBuf;
    row("same word", SAME, p);
    row("halfwords", HALF, p);
    row("8 apart", S8, p);
    row("16 apart", S16, p);
    row("32 apart", S32, p);
    row("64 apart", S64, p);
    row("128 apart", S128, p);
    row("256 apart", S256, p);
    row("512 apart", S512, p);
    row("across 256", X256, p);
    row("across 512", X512, p);
    row("across 1 KB", X1K, p);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
