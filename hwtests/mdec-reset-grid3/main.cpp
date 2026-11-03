/*
 * mdec/mdec-reset-grid3 - hwtests/mdec-reset-grid2 with each cell's time: a RAM store, the MDEC reset at word K+1 of a line,
 * N nops, the enables, cold and warm, and the call's cycles beside kept (2026-11-03 morning, for the SCPH-5501).
 *
 * WHY. mdec-reset-grid2's cold rows read nearly the opposite here and on the SCPH-5501 (word 2: the console keeps the enables
 * at N 0-1, loses them at 2-5 and keeps them from 6; this core loses them at 0-1 and keeps them from 2), and neither a longer
 * bus hold for an MDEC store nor keeping it out of a fill's deferral moves them (experiments/_mkmdst.js). A FIFO with a fixed
 * store length cannot make the console's row. The call's time says which half differs: where the times agree and kept
 * does not, the stores' order or place on the bus; where the times differ, the fetch.
 *
 * WHAT IT DOES. mdec-reset-grid2's cells (the set-up in the line before, K nops, a store to the stack, the reset, N nops, the
 * enables), each timed by Timer 2 from just before the call to just after it, the fastest of 4 tries kept beside the
 * tries that kept the enables; and MDEC1 read once at once after the call returns (the last try's), which shows a reset
 * still under way or the status.
 *
 * READ IT LIKE THIS. Three rows a word and run: kept of 4, the fastest call's cycles, and the status read at once. No
 * assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

extern "C" void mrg3Evict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mrg3Evict\nmrg3Evict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

template <int K, int N> __attribute__((noinline, aligned(16))) static void body() {
    __asm__ volatile(".set push\n.set noreorder\n .align 4\n lui $a2, 0x1f80\n lui $v0, 0x8000\n lui $t2, 0x6000\n nop\n"
                     " .rept %[k]\n nop\n .endr\n sw $v0, -4($sp)\n sw $v0, 0x1824($a2)\n .rept %[n]\n nop\n .endr\n sw $t2, 0x1824($a2)\n.set pop\n"
                     : : [k] "i"(K), [n] "i"(N) : "a2", "v0", "t2", "memory");
}
typedef void (*Case)();
#define ROW(K) { body<K, 0>, body<K, 1>, body<K, 2>, body<K, 3>, body<K, 4>, body<K, 5>, body<K, 6>, body<K, 7>, body<K, 8>, body<K, 9>, body<K, 10> }
static const Case CASES[4][11] = { ROW(0), ROW(1), ROW(2), ROW(3) };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static bool kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return (read32(MDEC1) >> 28) & 1; }

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset-grid3\n");
    printf("A RAM store, the reset at word K+1, N nops, the enables; cold and warm; kept of 4, fastest call, status at once.\n\n");
    static uint8_t keptN[4][2][11];
    static uint16_t cyc[4][2][11];
    static uint32_t st[4][2][11];
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < 2; w++)
            for (int n = 0; n < 11; n++) {
                int c = 0; uint32_t best = 0xFFFF, s = 0;
                for (int t = 0; t < 4; t++) {
                    EnterCriticalSection();
                    const uint16_t m2 = read16(T2_MODE);
                    write16(T2_MODE, 0);
                    lastT = read16(T2_COUNT); clk = 0;
                    settle();
                    if (w) { CASES[k][n](); settle(); } else mrg3Evict();
                    const uint32_t t0 = now();
                    CASES[k][n]();
                    const uint32_t t1 = now();
                    s = read32(MDEC1);
                    if (t1 - t0 < best) best = t1 - t0;
                    c += kept();
                    settle();
                    write16(T2_MODE, m2);
                    ExitCriticalSection();
                }
                keptN[k][w][n] = (uint8_t)c; cyc[k][w][n] = (uint16_t)best; st[k][w][n] = s;
            }
    printf(" K  run    N:    0    1    2    3    4    5    6    7    8    9   10\n");
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < 2; w++) {
            printf(" %d  %s kept", k, w ? "warm" : "cold");
            for (int n = 0; n < 11; n++) printf(" %4d", keptN[k][w][n]);
            printf("\n %d  %s cyc ", k, w ? "warm" : "cold");
            for (int n = 0; n < 11; n++) printf(" %4u", (unsigned)cyc[k][w][n]);
            printf("\n %d  %s st  ", k, w ? "warm" : "cold");
            for (int n = 0; n < 11; n++) printf(" %04x", (unsigned)(st[k][w][n] >> 16));
            printf("\n");
        }
    printf("\nmdec/mdec-reset-grid3: Done.\n");
    for (;;) {}   /* park after Done. */
}
