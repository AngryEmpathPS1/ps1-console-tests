/*
 * mdec/mdec-reset-tailgrid - hwtests/mdec-reset-tail's cold rows with the reset and the enables N nops further apart, so each
 * tail's effect reads in cycles, not kept or lost (2026-11-03 morning, for the SCPH-5501).
 *
 * WHY. Cold, mdec-reset-tail keeps the stock prologue's enables at every word with a nop behind them (B) and loses them at word 3
 * when any access follows within two instructions (A, C, D, E) and at word 0 when that access re-reads the word the stack
 * store wrote (A). Whether such an access moves the reset, the enables or both, and by how much, a kept / lost cell cannot say;
 * the distance at which the enables come back can (mdec-reset-grid's way: the reset's deaf window is 7 cycles, v0.377.227).
 *
 * WHAT IT DOES. The prologue (the stack store, the reset, its three instructions, N nops, the enables, the tail, the stack
 * restored) entered at word 0-3 of a 16-byte line, cold only (4 KB of nops just before), N 0-7, 4 tries a cell, from a settled
 * MDEC with interrupts off; 400 cycles later a table command and the status's bit 28. Six rows a word:
 *   A  lw ra, 20(sp) behind the enables (stock)      B  a nop there
 *   C  lw t3, 512(sp) (another RAM block)              D  lhu t3, Timer 2 (on-die)
 *   E  a nop, then lw ra                               G  A without the stack store
 * Each cell also has the fastest call of its 4 (Timer 2, sysclock).
 *
 * READ IT LIKE THIS. kept of 4 at N 0-7 for each tail and word, the fastest call below each. Where B keeps at N 0 and A needs
 * N k, A moves the stores k cycles closer; a row lost at every N says the tail holds the enables ahead of the reset's end.
 * No assertions.
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

#define BODY(ST, TL) \
    ".set push\n.set noreorder\n .align 4\n .rept %[k]\n nop\n .endr\n" \
    " addiu $sp, $sp, -24\n lui $a2, 0x1f80\n lui $v0, 0x8000\n " ST "\n sw $v0, 0x1824($a2)\n" \
    " lui $v0, 0x4000\n lui $t2, 0x6000\n addiu $v0, $v0, 1\n .rept %[n]\n nop\n .endr\n sw $t2, 0x1824($a2)\n" \
    " " TL "\n addiu $sp, $sp, 24\n.set pop\n"
#define CLOBBER "a2", "v0", "t2", "t3", "ra", "memory"
#define VARIANT(NAME, ST, TL) \
    template <int K, int N> __attribute__((noinline, aligned(16))) static void NAME() { __asm__ volatile(BODY(ST, TL) : : [k] "i"(K), [n] "i"(N) : CLOBBER); }
VARIANT(vA, "sw $ra, 20($sp)", "lw $ra, 20($sp)")
VARIANT(vB, "sw $ra, 20($sp)", "nop")
VARIANT(vC, "sw $ra, 20($sp)", "lw $t3, 512($sp)")
VARIANT(vD, "sw $ra, 20($sp)", "lhu $t3, 0x1120($a2)")
VARIANT(vE, "sw $ra, 20($sp)", "nop\n lw $ra, 20($sp)")
VARIANT(vG, "nop", "lw $ra, 20($sp)")

extern "C" void mtgEvict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mtgEvict\nmtgEvict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

typedef void (*Case)();
#define NROW(V, K) { V<K, 0>, V<K, 1>, V<K, 2>, V<K, 3>, V<K, 4>, V<K, 5>, V<K, 6>, V<K, 7> }
#define VROWS(V) { NROW(V, 0), NROW(V, 1), NROW(V, 2), NROW(V, 3) }
static const Case CASES[6][4][8] = { VROWS(vA), VROWS(vB), VROWS(vC), VROWS(vD), VROWS(vE), VROWS(vG) };
static const char NAMES[6] = { 'A', 'B', 'C', 'D', 'E', 'G' };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static bool kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return (read32(MDEC1) >> 28) & 1; }

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset-tailgrid\n");
    printf("mdec-reset-tail's cold rows with N nops more between the reset and the enables. Kept of 4, fastest call. No assertions.\n\n");
    static uint8_t keptN[6][4][8];
    static uint16_t cyc[6][4][8];
    for (int v = 0; v < 6; v++)
        for (int k = 0; k < 4; k++)
            for (int n = 0; n < 8; n++) {
                int c = 0; uint32_t best = 0xFFFF;
                for (int t = 0; t < 4; t++) {
                    EnterCriticalSection();
                    const uint16_t m2 = read16(T2_MODE);
                    write16(T2_MODE, 0);
                    lastT = read16(T2_COUNT); clk = 0;
                    settle();
                    mtgEvict();
                    const uint32_t t0 = now();
                    CASES[v][k][n]();
                    const uint32_t t1 = now();
                    if (t1 - t0 < best) best = t1 - t0;
                    c += kept();
                    settle();
                    write16(T2_MODE, m2);
                    ExitCriticalSection();
                }
                keptN[v][k][n] = (uint8_t)c; cyc[v][k][n] = (uint16_t)best;
            }
    printf(" var word      N:   0   1   2   3   4   5   6   7\n");
    for (int v = 0; v < 6; v++)
        for (int k = 0; k < 4; k++) {
            printf("  %c   %d   kept", NAMES[v], k);
            for (int n = 0; n < 8; n++) printf(" %3d", keptN[v][k][n]);
            printf("\n  %c   %d   cyc ", NAMES[v], k);
            for (int n = 0; n < 8; n++) printf(" %3u", (unsigned)cyc[v][k][n]);
            printf("\n");
        }
    printf("\nmdec/mdec-reset-tailgrid: Done.\n");
    for (;;) {}   /* park after Done. */
}
