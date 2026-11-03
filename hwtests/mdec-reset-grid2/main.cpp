/*
 * mdec/mdec-reset-grid2 - hwtests/mdec-reset-grid with a RAM store just ahead of the reset, as StrPlayer::mdecReset has it
 * (sw $ra to the stack), by the reset's word in a cache line and the distance to the enables, cold and warm
 * (2026-11-03 morning, for the SCPH-5501).
 *
 * WHY THIS ONE. mdec-reset-grid (nops only between the set-up, the reset and the enables) reads the same on the SCPH-5501 and
 * this core in 87 of 88 cells; mdec-reset-align (mdecReset's own prologue) does not: the console keeps or loses its enables
 * by the code's word in a line, this core by cold or warm alone. What the prologue has and the grid lacks is the stack
 * store right before the reset: the reset waits in the write queue behind a RAM write.
 *
 * WHAT IT DOES. As mdec-reset-grid: the set-up in the line before, K nops, then a store to RAM (the stack, below sp), the
 * reset at word K+1, N nops, the enables. Cold and warm, 4 tries a cell, bit 28 after a table command says kept.
 *
 * READ IT LIKE THIS. Against mdec-reset-grid: the N where each row turns to 4 moves by what the RAM store costs the reset
 * on its way out, by word and by cold or warm. No assertions.
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

extern "C" void mrg2Evict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mrg2Evict\nmrg2Evict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

/* The set-up fills the line before (three instructions and a nop), then K nops, the stack store at word K, the reset at
   word K+1 (the next line's first word when K is 3), N nops, the enables. */
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
    printf("\nmdec/mdec-reset-grid2\n");
    printf("A RAM store, the reset at word K+1 of a line, N nops, the enables; cold and warm; kept of 4. No assertions.\n\n");
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
                    if (w) { CASES[k][n](); settle(); } else mrg2Evict();
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
    printf("\nmdec/mdec-reset-grid2: Done.\n");
    for (;;) {}   /* park after Done. */
}
