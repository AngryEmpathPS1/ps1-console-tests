/*
 * mdec/mdec-reset-tail - hwtests/mdec-reset-align with what follows the enables changed, one thing at a time (2026-11-03 morning,
 * for the SCPH-5501).
 *
 * WHY. mdec-reset-align keeps the stock StrPlayer::mdecReset() prologue's enables warm at words 2 and 3 of a line (6/8, 8/8) and
 * loses them at 0 and 1 (0/8, 2/8). mdec-reset-grid3's warm cells put the stack store, the reset and the enables at the same
 * places and the same cycles and never keep them at three instructions apart. What differs is behind the enables: the
 * prologue's `lw ra, 20(sp)` (a RAM read of the word the stack store wrote) where grid3 returns and reads Timer 2. If a RAM read
 * there goes ahead of the queued enables on the bus (read priority), the enables arrive behind it, past the reset's deaf
 * cycles. This changes only that.
 *
 * WHAT IT DOES. The prologue at word 0-3 of a 16-byte line (the stack store, the reset, three instructions, the enables), cold
 * (4 KB of nops just before) and warm (run twice, the second kept), 8 tries each, from a settled MDEC with interrupts off, in
 * seven variants:
 *   A  stock: lw ra, 20(sp) right behind the enables
 *   B  a nop there instead (ra is unchanged, so nothing reads it)
 *   C  lw t3, 512(sp) there: a RAM read of another 64-byte block
 *   D  lhu t3, Timer 2 there: an on-die read
 *   E  a nop, then lw ra: the RAM read one instruction later
 *   F  two nops, then lw ra: two later
 *   G  stock, but a nop in place of the stack store: no RAM store ahead of the reset
 * 400 cycles later a table command, then the status: bit 28 (data in wanted) says the enables were kept. Each call is timed by
 * Timer 2 (sysclock) from just before to just after it; the fastest of the 8 is printed beside the count.
 *
 * READ IT LIKE THIS. Per variant, kept of 8 and the fastest call, cold and warm at each word. A against B says whether the read
 * behind the enables keeps them; C, D whether it must be a RAM read and of which word; E, F how near it must be; G whether it
 * needs the store ahead. No assertions.
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

/* mdecReset's prologue as the GameDay builds have it (mdec-reset-align's), with the stack store (ST) and what follows the
   enables (TL) given per variant; K nops ahead put its entry at word K of the line. */
#define BODY(ST, TL) \
    ".set push\n.set noreorder\n .align 4\n .rept %[n]\n nop\n .endr\n" \
    " addiu $sp, $sp, -24\n lui $a2, 0x1f80\n lui $v0, 0x8000\n " ST "\n sw $v0, 0x1824($a2)\n" \
    " lui $v0, 0x4000\n lui $t2, 0x6000\n addiu $v0, $v0, 1\n sw $t2, 0x1824($a2)\n" \
    " " TL "\n addiu $sp, $sp, 24\n.set pop\n"
#define CLOBBER "a2", "v0", "t2", "t3", "ra", "memory"
#define VARIANT(NAME, ST, TL) \
    template <int K> __attribute__((noinline, aligned(16))) static void NAME() { __asm__ volatile(BODY(ST, TL) : : [n] "i"(K) : CLOBBER); }
VARIANT(vA, "sw $ra, 20($sp)", "lw $ra, 20($sp)")
VARIANT(vB, "sw $ra, 20($sp)", "nop")
VARIANT(vC, "sw $ra, 20($sp)", "lw $t3, 512($sp)")
VARIANT(vD, "sw $ra, 20($sp)", "lhu $t3, 0x1120($a2)")
VARIANT(vE, "sw $ra, 20($sp)", "nop\n lw $ra, 20($sp)")
VARIANT(vF, "sw $ra, 20($sp)", "nop\n nop\n lw $ra, 20($sp)")
VARIANT(vG, "nop", "lw $ra, 20($sp)")

/* 4 KB of nops, run through: every line of the instruction cache holds them after (pair-warm's paEvict). */
extern "C" void mrtEvict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mrtEvict\nmrtEvict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

typedef void (*Case)();
#define ROW(V) { V<0>, V<1>, V<2>, V<3> }
static const Case CASES[7][4] = { ROW(vA), ROW(vB), ROW(vC), ROW(vD), ROW(vE), ROW(vF), ROW(vG) };
static const char NAMES[7] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G' };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static bool kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return (read32(MDEC1) >> 28) & 1; }

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset-tail\n");
    printf("mdecReset's prologue at each word, cold and warm, with what follows the enables changed. Kept of 8, fastest call. No assertions.\n\n");
    static uint8_t keptN[7][4][2];
    static uint16_t cyc[7][4][2];
    for (int v = 0; v < 7; v++)
        for (int k = 0; k < 4; k++)
            for (int w = 0; w < 2; w++) {
                int c = 0; uint32_t best = 0xFFFF;
                for (int t = 0; t < 8; t++) {
                    EnterCriticalSection();
                    const uint16_t m2 = read16(T2_MODE);
                    write16(T2_MODE, 0);
                    lastT = read16(T2_COUNT); clk = 0;
                    settle();
                    if (w) { CASES[v][k](); settle(); } else mrtEvict();
                    const uint32_t t0 = now();
                    CASES[v][k]();
                    const uint32_t t1 = now();
                    if (t1 - t0 < best) best = t1 - t0;
                    c += kept();
                    settle();
                    write16(T2_MODE, m2);
                    ExitCriticalSection();
                }
                keptN[v][k][w] = (uint8_t)c; cyc[v][k][w] = (uint16_t)best;
            }
    printf(" var   word 0 cold  warm   word 1 cold  warm   word 2 cold  warm   word 3 cold  warm\n");
    for (int v = 0; v < 7; v++) {
        printf("  %c  ", NAMES[v]);
        for (int k = 0; k < 4; k++) printf("     %d/8 %3u %d/8 %3u", keptN[v][k][0], (unsigned)cyc[v][k][0], keptN[v][k][1], (unsigned)cyc[v][k][1]);
        printf("\n");
    }
    printf("\nmdec/mdec-reset-tail: Done.\n");
    for (;;) {}   /* park after Done. */
}
