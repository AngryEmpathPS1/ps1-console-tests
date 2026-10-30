/*
 * cpu/wb-page - does the write buffer's burst go on across a 2 KB boundary inside one 4 KB DRAM page? (2026-10-11)
 *
 * WHY. The write queue (src/psx.js queueWrite, M207) goes on with a burst (2 cycles a word, not 6) only within a 2 KB
 * page: Sony's bus capture and pcsx-redux PR #2091 had it so, and hwtests/wb-drain's stores 4 KB apart never pair - which
 * says nothing between 2 and 4 KB. The reads' page mode (v0.377.78's candidate, CB_RPGSH) is 4 KB: load-fill2's TWO,
 * words at 8002C7FCh and 8002C800h - across a 2 KB line - read 10.24 on the 7501 as every same-page pair does. If the
 * DRAM's row is 4 KB the stores' burst should go on across 2 KB as well, and this core's 2 KB is a cycle-count error on
 * every store run that crosses one.
 *
 * WHAT IT DOES. As wb-drain: each row N stores (sw), back to back, then 48 nops, 256 times, against the same with nops;
 * cycles per iteration above that (Timer 2 at the system clock, interrupts off). The rows differ only in where the
 * stores go, all in a 4 KB-aligned buffer:
 *   at 0, words           N consecutive words from the start of a 4 KB page (wb-drain's own row)
 *   across 2 KB, words    N consecutive words from 16 bytes before the page's 2 KB mark: the fifth crosses it
 *   across 4 KB, words    N consecutive words from 16 bytes before the next page: the fifth crosses into it
 *   2 KB apart            N words 2048 bytes apart: two to a 4 KB page
 *   4 KB apart            N words 4096 bytes apart: a page each (wb-drain's far row; N = 1..8)
 *   1 KB apart            N words 1024 bytes apart: two to a 2 KB page, four to a 4 KB page
 *
 * READ IT LIKE THIS. A row matches "at 0" while its stores stay in one burst; a new burst costs a word about 4 cycles
 * more once the queue is full. With a 2 KB page, "across 2 KB" reads as "across 4 KB" and "2 KB apart" as "4 KB apart";
 * with a 4 KB page, "across 2 KB" reads as "at 0" and "2 KB apart" pairs.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js wb-page`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u

uint32_t wbBuf[8 * 1024 + 64] __attribute__((aligned(4096)));   /* 32 KB: the 4 KB-apart row writes 8 words a page apart */

typedef uint32_t (*fn)(uint32_t);

#define PRE  ".set push\n\t.set noreorder\n\t.set noat\n\t" \
             "lui $t9, 0x1f80\n\tli $t8, 256\n\tlhu %[t0], 0x1120($t9)\n\tnop\n1:\n\t"
#define POST ".rept 48\n\tnop\n\t.endr\n\taddiu $t8, $t8, -1\n\tbnez $t8, 1b\n\tnop\n\tlhu %[t1], 0x1120($t9)\n\tnop\n\t.set pop\n\t"

/* N stores STEP bytes apart from OFF bytes into the buffer. */
template <int N, int OFF, int STEP> __attribute__((noinline)) uint32_t tS(uint32_t p) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".set wbo, %[o]\n\t.rept %[n]\n\tsw $zero, wbo(%[p])\n\t.set wbo, wbo + %[s]\n\t.endr\n\t" POST
                     : [t0] "=&r"(t0), [t1] "=&r"(t1) : [p] "r"(p), [n] "i"(N), [o] "i"(OFF), [s] "i"(STEP) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}
template <int N> __attribute__((noinline)) uint32_t tB(uint32_t) {
    uint32_t t0, t1;
    __asm__ volatile(PRE ".rept %[n]\n\tnop\n\t.endr\n\t" POST : [t0] "=&r"(t0), [t1] "=&r"(t1) : [n] "i"(N) : "t8", "t9", "memory");
    return (uint16_t)(t1 - t0);
}

#define ROW12(OFF, STEP) { tS<1,OFF,STEP>, tS<2,OFF,STEP>, tS<3,OFF,STEP>, tS<4,OFF,STEP>, tS<5,OFF,STEP>, tS<6,OFF,STEP>, \
                           tS<7,OFF,STEP>, tS<8,OFF,STEP>, tS<9,OFF,STEP>, tS<10,OFF,STEP>, tS<11,OFF,STEP>, tS<12,OFF,STEP> }
static const fn B[12] = { tB<1>, tB<2>, tB<3>, tB<4>, tB<5>, tB<6>, tB<7>, tB<8>, tB<9>, tB<10>, tB<11>, tB<12> };
static const fn AT0[12] = ROW12(0, 4), X2K[12] = ROW12(2048 - 16, 4), X4K[12] = ROW12(4096 - 16, 4);
#define ROW8(OFF, STEP) { tS<1,OFF,STEP>, tS<2,OFF,STEP>, tS<3,OFF,STEP>, tS<4,OFF,STEP>, tS<5,OFF,STEP>, tS<6,OFF,STEP>, tS<7,OFF,STEP>, tS<8,OFF,STEP> }
static const fn A2K[12] = ROW12(0, 2048), A1K[12] = ROW12(0, 1024), A4K[8] = ROW8(0, 4096);   /* offsets stay under 32 KB */

static uint32_t twice(fn f, uint32_t p) { f(p); return f(p); }
static void hund(int32_t v) { if (v < 0) { printf("-"); v = -v; } printf("%d.%02d", v / 100, v % 100); }
static void row(const char* name, const fn* s, uint32_t p, int n = 12) {
    printf("  %-20s", name);
    for (int i = 0; i < n; i++) {
        const int32_t d = ((int32_t)twice(s[i], p) - (int32_t)twice(B[i], p)) * 100 / 256;
        printf(" "); hund(d);
    }
    printf("\n");
}

int main() {
    printf("\ncpu/wb-page\n");
    printf("N RAM stores back to back, then 48 nops: cycles per iteration above the same with nops, N = 1..12.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t p = (uint32_t)wbBuf;
    row("at 0, words", AT0, p);
    row("across 2 KB, words", X2K, p);
    row("across 4 KB, words", X4K, p);
    row("2 KB apart", A2K, p);
    row("4 KB apart", A4K, p, 8);
    row("1 KB apart", A1K, p);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
