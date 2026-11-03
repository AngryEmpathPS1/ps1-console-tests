/*
 * mdec/mdec-reset-half - the stock mdecReset prologue warm but for chosen cache lines, the way a game meets it (2026-11-03 morning,
 * for the SCPH-5501).
 *
 * WHY. A GameDay build of THE VAULT on nugget's stock player shows nothing on this core at PAD 2 and 6 and plays 146 of 146 on
 * the SCPH-5501 at PAD 0, 2 and 6. In the game this core runs mdecReset with the reset's line cached and the next line cold (the
 * stream path), defers the reset behind that line's fill and writes the enables 3 cycles after it, inside the reset's 7 deaf
 * cycles. mdec-reset-tail ran the prologue all warm or all cold, neither the game's state. This runs it warm with one line or
 * two of it taken out of the instruction cache just before the call.
 *
 * WHAT IT DOES. The prologue (mdec-reset-tail's A: the stack store, the reset, three instructions, the enables, lw ra; and B,
 * a nop in place of the lw ra) entered at word 0-3 of a 16-byte line. GCC wraps each function in its own prologue (addiu sp;
 * sw ra, 4(sp); the asm's .align nops), so the function's lines are: L1 GCC's, L2 the entry's (the stack store at word K+3, in
 * L3 for K >= 1), L3 the reset's (at word K), L4 the enables' (at word K; the lw ra behind them, in L5 for K = 3). Each call runs
 * once to cache it all, the MDEC is settled, then lines are evicted by running one line of a 4 KB block of `jr ra` lines whose
 * index matches (nothing else of the prologue moves), then the timed call: evicted none, L1, L2, L3, L4, L2+L3, L3+L4. 8 tries
 * a cell, interrupts off; 400 cycles later a table command and bit 28.
 *
 * READ IT LIKE THIS. kept of 8 and the fastest call (Timer 2, sysclock) per tail, word and evicted set. The game enters mdecReset
 * fully cold (experiments/mdec-gdlines.js), mdec-reset-tail's cold rows. No assertions.
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

#define BODY(TL) \
    ".set push\n.set noreorder\n .align 4\n .rept %[k]\n nop\n .endr\n" \
    " addiu $sp, $sp, -24\n lui $a2, 0x1f80\n lui $v0, 0x8000\n sw $ra, 20($sp)\n sw $v0, 0x1824($a2)\n" \
    " lui $v0, 0x4000\n lui $t2, 0x6000\n addiu $v0, $v0, 1\n sw $t2, 0x1824($a2)\n" \
    " " TL "\n addiu $sp, $sp, 24\n.set pop\n"
#define CLOBBER "a2", "v0", "t2", "ra", "memory"
template <int K> __attribute__((noinline, aligned(16))) static void vA() { __asm__ volatile(BODY("lw $ra, 20($sp)") : : [k] "i"(K) : CLOBBER); }
template <int K> __attribute__((noinline, aligned(16))) static void vB() { __asm__ volatile(BODY("nop") : : [k] "i"(K) : CLOBBER); }

/* 256 lines of `jr ra; nop; nop; nop` on a 4 KB boundary: calling line i fills cache line i with it and nothing else. */
extern "C" void mhLines(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mhLines\nmhLines:\n.rept 256\njr $ra\nnop\nnop\nnop\n.endr\n.set pop\n");
static void evictLine(uint32_t addr) { ((void (*)())((uint32_t)mhLines + (addr & 0xFF0)))(); }

typedef void (*Case)();
static const Case CASES[2][4] = { { vA<0>, vA<1>, vA<2>, vA<3> }, { vB<0>, vB<1>, vB<2>, vB<3> } };
static const char NAMES[2] = { 'A', 'B' };
/* the evicted sets, as a bit per line L1..L4 */
static const uint8_t SETS[7] = { 0, 1, 2, 4, 8, 6, 12 };
static const char *SETNAME[7] = { "none", "L1", "L2", "L3", "L4", "L2+L3", "L3+L4" };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static bool kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return (read32(MDEC1) >> 28) & 1; }

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset-half\n");
    printf("mdecReset's prologue warm with chosen lines evicted. Kept of 8, fastest call. No assertions.\n\n");
    static uint8_t keptN[2][4][7];
    static uint16_t cyc[2][4][7];
    for (int v = 0; v < 2; v++)
        for (int k = 0; k < 4; k++)
            for (int s = 0; s < 7; s++) {
                int c = 0; uint32_t best = 0xFFFF;
                const uint32_t base = (uint32_t)CASES[v][k];
                for (int t = 0; t < 8; t++) {
                    EnterCriticalSection();
                    const uint16_t m2 = read16(T2_MODE);
                    write16(T2_MODE, 0);
                    lastT = read16(T2_COUNT); clk = 0;
                    settle();
                    CASES[v][k]();
                    settle();
                    for (int l = 0; l < 4; l++) if (SETS[s] & (1 << l)) evictLine(base + 16 * l);
                    const uint32_t t0 = now();
                    CASES[v][k]();
                    const uint32_t t1 = now();
                    if (t1 - t0 < best) best = t1 - t0;
                    c += kept();
                    settle();
                    write16(T2_MODE, m2);
                    ExitCriticalSection();
                }
                keptN[v][k][s] = (uint8_t)c; cyc[v][k][s] = (uint16_t)best;
            }
    printf(" var word   evicted:  none     L1     L2     L3     L4  L2+L3  L3+L4\n");
    for (int v = 0; v < 2; v++)
        for (int k = 0; k < 4; k++) {
            printf("  %c   %d   ", NAMES[v], k);
            for (int s = 0; s < 7; s++) printf("  %d %3u", keptN[v][k][s], (unsigned)cyc[v][k][s]);
            printf("\n");
        }
    printf("\nmdec/mdec-reset-half: Done.\n");
    for (;;) {}   /* park after Done. */
}
