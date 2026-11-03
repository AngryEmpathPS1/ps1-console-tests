/*
 * mdec/mdec-reset-align - nugget's stock StrPlayer::mdecReset() prologue, cold and warm, at each place in a cache line
 * (2026-11-03 evening, for the SCPH-5501).
 *
 * WHY. psyqo-paths' StrPlayer::mdecReset() writes the reset (80000000h) and the enables (60000000h) to MDEC1 with three
 * instructions between them (lui, lui, addiu). On this core a GameDay build of THE VAULT (C:/psxsplash-work, stock player)
 * plays GDAY2K.STR 146 of 146 when the function starts at the first word of a cache line, and shows nothing when it starts
 * two words in: the enable lands 3-4 cycles behind the reset, inside the 7 cycles mdec-reset2 found the MDEC deaf (v0.377.227),
 * where at the first word a line fill puts it 9 behind. The SCPH-5501 plays both builds 146 of 146, so on the console the
 * enable is kept at both. Which time this core gets wrong - the cold fetch across the line, the stores' way to the bus, or
 * the window - is what this reads.
 *
 * WHAT IT DOES. The function's first nine instructions as gcc compiled them (stack, two lui, the reset store, lui, lui,
 * addiu, the enables store), its entry at word 0, 1, 2 and 3 of a 16-byte line. Each runs COLD (4 KB of nops run just
 * before, so no line of it is cached) and WARM (run twice, the second kept), from a settled MDEC, interrupts off. 400 cycles later a
 * table command, then the status: bit 28 (data in wanted) shows the enables were kept. Also the cycles from the call to
 * its return, by Timer 2 (sysclock), so the gap the fetch made can be read beside it. Eight tries each.
 *
 * READ IT LIKE THIS. "kept" counts the tries whose status has bit 28; a console keeping all at every place says the core's
 * cold or warm time for this run of code is too short where it loses them.
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

/* mdecReset's prologue as the GameDay builds have it (0x80011a40 in vault-gd0): a2 = 1F80xxxx, the reset, three
   instructions, the enables. K nops ahead of it inside the aligned block put its entry at word K of the line. */
#define BODY \
    ".set push\n.set noreorder\n .align 4\n .rept %[n]\n nop\n .endr\n" \
    " addiu $sp, $sp, -24\n lui $a2, 0x1f80\n lui $v0, 0x8000\n sw $ra, 20($sp)\n sw $v0, 0x1824($a2)\n" \
    " lui $v0, 0x4000\n lui $t2, 0x6000\n addiu $v0, $v0, 1\n sw $t2, 0x1824($a2)\n" \
    " lw $ra, 20($sp)\n addiu $sp, $sp, 24\n.set pop\n"
#define CLOBBER "a2", "v0", "t2", "ra", "memory"
template <int K> __attribute__((noinline, aligned(16))) static void prologue() { __asm__ volatile(BODY : : [n] "i"(K) : CLOBBER); }

/* 4 KB of nops, run through: every line of the 4 KB instruction cache holds them after, so nothing else is cached
   (pair-warm's paEvict). */
extern "C" void mraEvict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mraEvict\nmraEvict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

typedef void (*Case)();
static const Case CASES[4] = { prologue<0>, prologue<1>, prologue<2>, prologue<3> };

static void settle() { write32(MDEC1, 0x80000000u); waitCycles(400); }
static uint32_t kept() { waitCycles(400); write32(MDEC0, 0x40000001u); return read32(MDEC1); }

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset-align\n");
    printf("StrPlayer::mdecReset's prologue at each word of a cache line, cold and warm. No assertions.\n\n");
    static uint32_t st[4][2][8], cyc[4][2][8];
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < 2; w++)
            for (int t = 0; t < 8; t++) {
                EnterCriticalSection();
                const uint16_t m2 = read16(T2_MODE);
                write16(T2_MODE, 0);
                lastT = read16(T2_COUNT); clk = 0;
                settle();
                if (w) { CASES[k](); settle(); }   /* warm: once to fill the cache, then the run kept */
                else mraEvict();                   /* cold: nothing of it in the cache */
                const uint32_t t0 = now();
                CASES[k]();
                cyc[k][w][t] = now() - t0;
                st[k][w][t] = kept();
                settle();
                write16(T2_MODE, m2);
                ExitCriticalSection();
            }
    printf(" word  run   kept  status (8 tries)                                                      cycles in the call\n");
    for (int k = 0; k < 4; k++)
        for (int w = 0; w < 2; w++) {
            int n = 0;
            for (int t = 0; t < 8; t++) n += (st[k][w][t] >> 28) & 1;
            printf("  %d    %s  %d/8 ", k, w ? "warm" : "cold", n);
            for (int t = 0; t < 8; t++) printf(" %08x", (unsigned)st[k][w][t]);
            printf("  ");
            for (int t = 0; t < 8; t++) printf(" %u", (unsigned)cyc[k][w][t]);
            printf("\n");
        }
    printf("\nmdec/mdec-reset-align: Done.\n");
    for (;;) {}   /* park after Done. */
}
