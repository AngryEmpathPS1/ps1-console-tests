/*
 * mdec/mdec-reset-grid - the MDEC reset's deaf window as a store sees it, by where the reset sits in a cache line and how
 * far behind it the enables are, cold and warm (2026-11-03 evening, for the SCPH-5501).
 *
 * WHY. hwtests/mdec-reset-align: the console keeps or loses StrPlayer::mdecReset's enables (three instructions behind the
 * reset) by the word of the line the code starts at, cold and warm alike (word 0 lost both ways, word 3 lost cold and kept
 * warm), where this core keeps every cold run and loses every warm one with the right total cycles. mdec-reset2 found the
 * window, 7 cycles, with nops in warm code at one alignment. This grid gives, for each word and each of cold and warm, the
 * distance at which the enables start to be kept: the gap the console puts between the two stores' arrivals.
 *
 * WHAT IT DOES. An aligned block: K nops (the reset store at word K of its line, the set-up in the line before), the
 * reset, N nops (0..10), the enables. Cold (4 KB of nops run first, so no line of it is cached) and warm (run twice, the
 * second kept), 4 tries a cell, interrupts off, from a settled MDEC. 400 cycles later a table command; bit 28 of the
 * status says the enables were kept.
 *
 * READ IT LIKE THIS. Each row is a word and a run; each column N the tries of 4 that kept the enables. The N where a row
 * turns to 4 is the boundary; its word-to-word and cold-to-warm shifts are the rule. No assertions.
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

extern "C" void mrgEvict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mrgEvict\nmrgEvict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

/* The set-up fills the line before the reset's (three instructions and a nop: 16 bytes, aligned), then K nops, the reset
   at word K of the next line, N nops, the enables. */
template <int K, int N> __attribute__((noinline, aligned(16))) static void body() {
    __asm__ volatile(".set push\n.set noreorder\n .align 4\n lui $a2, 0x1f80\n lui $v0, 0x8000\n lui $t2, 0x6000\n nop\n"
                     " .rept %[k]\n nop\n .endr\n sw $v0, 0x1824($a2)\n .rept %[n]\n nop\n .endr\n sw $t2, 0x1824($a2)\n.set pop\n"
                     : : [k] "i"(K), [n] "i"(N) : "a2", "v0", "t2", "memory");
}
typedef void (*Case)();
#define ROW(K) { body<K, 0>, body<K, 1>, body<K, 2>, body<K, 3>, body<K, 4>, body<K, 5>, body<K, 6>, body<K, 7>, body<K, 8>, body<K, 9>, body<K, 10> }
static const Case CASES[4][11] = { ROW(0), ROW(1), ROW(2), ROW(3) };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static bool kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return (read32(MDEC1) >> 28) & 1; }

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset-grid\n");
    printf("The reset at word K of a line, N nops, the enables; cold and warm; kept of 4. No assertions.\n\n");
    static uint8_t keptN[4][2][11];
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < 2; w++)
            for (int n = 0; n < 11; n++) {
                int c = 0;
                for (int t = 0; t < 4; t++) {
                    EnterCriticalSection();
                    const uint16_t m2 = read16(T2_MODE);
                    write16(T2_MODE, 0);
                    lastT = read16(T2_COUNT); clk = 0;
                    settle();
                    if (w) { CASES[k][n](); settle(); } else mrgEvict();
                    CASES[k][n]();
                    c += kept();
                    settle();
                    write16(T2_MODE, m2);
                    ExitCriticalSection();
                }
                keptN[k][w][n] = (uint8_t)c;
            }
    printf(" K  run    N: 0 1 2 3 4 5 6 7 8 9 10\n");
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < 2; w++) {
            printf(" %d  %s      ", k, w ? "warm" : "cold");
            for (int n = 0; n < 11; n++) printf(" %d", keptN[k][w][n]);
            printf("\n");
        }
    printf("\nmdec/mdec-reset-grid: Done.\n");
    for (;;) {}   /* park after Done. */
}
