/*
 * cpu/icache-place - what an instruction-cache line fill costs by where the code sits: the entry word, the DRAM row,
 * a load or store beside the miss. Each case is code generated here at a chosen layout, timed cold (FlushCache, then
 * the call) and warm (the call again at once).
 *
 * WHY (2026-11-13; STATUS "pm386's gap is code placement, not protected mode"). PS1-DOS's 386 core runs +0.63% on the
 * second SCPH-5501 and the PSone against this emulator when built -O2 and -0.12% built -Os - the same C, the same 386
 * work, in all sixteen of pmsplit's phases; both consoles identical to four places. Its hot code (cpu386_step alone
 * 15 KB) thrashes the 4 KB I-cache, so the emulator's charge for a line fill depends on something about WHERE the code
 * is that it gets wrong. This splits the fill by the things a layout changes: the word a miss enters at (a partial
 * fill), the line after the last one missed or a line a DRAM row away, and a RAM data access at the miss.
 *
 * WHAT IT DOES. Interrupts off. Each case is 128 cache lines of generated code in RAM at 80030000h (its own 264 KB)
 * ending in jr ra, its data at 800C0000h; called from a stub with Timer 2 (system clock) read around the jalr. Cold:
 * A(44h) FlushCache, then the timed call; warm: the timed call again. 16 times each, min and max.
 *   seq       128 lines of nops, straight through (every fill at word 0, each the line after the last)
 *   jK s=S    a chain: each line entered at word K by the j in the line before (j at word 2, its slot word 3; K = 3:
 *             j at word 3, so its slot is the NEXT line's word 0, which fills that line, and the j goes to the line
 *             after it at word 3), lines S bytes apart: 16 (adjacent), 1040 (each a 1 KB row on), 2064 (2 KB on)
 *   ldW d=D   straight lines, a lw at word W (0 or 3) of each, its address D bytes on from the last (4: the same
 *             row; 1028: a 1 KB row on), and st the same with a sw
 *   lduse     the lw at word 0 and its register used at word 1
 *
 * READ IT LIKE THIS. "<case>: cold min M max X  warm min M max X" in cycles; cold - warm over 128 is a fill's cost
 * in that layout (the stub's own overhead cancels).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE (*(volatile uint32_t *)0x1F801124)
#define I_MASK  (*(volatile uint32_t *)0x1F801074)
#define CODE ((volatile uint32_t *)0x80030000)
#define DATA 0x800C0000u
#define LINES 128

extern "C" {
uint32_t ip_call(uint32_t fn, uint32_t a0, uint32_t a1);
void ip_flush(void);
}
__asm__(
    ".text\n.set push\n.set noreorder\n"
    ".balign 16\n.global ip_call\n"
    "ip_call:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n"
    "  move  $t2, $a0\n  move $a0, $a1\n  move $a1, $a2\n"
    "  lui   $t3, 0x1F80\n"
    "  lhu   $s0, 0x1120($t3)\n"
    "  jalr  $t2\n"
    "  nop\n"
    "  lui   $t3, 0x1F80\n"
    "  lhu   $v0, 0x1120($t3)\n"
    "  nop\n"
    "  subu  $v0, $v0, $s0\n  andi $v0, $v0, 0xFFFF\n"
    "  lw $ra, 20($sp)\n  lw $s0, 16($sp)\n  jr $ra\n  addiu $sp, $sp, 24\n"
    ".global ip_flush\n"
    "ip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n"
    ".set pop\n");

#define NOP 0u
#define JR_RA 0x03E00008u
#define LW_T0 0x8C880000u      /* lw t0, 0(a0) */
#define SW_T0 0xAC880000u      /* sw t0, 0(a0) */
#define ADDU_A0 0x00852021u    /* addu a0, a0, a1 */
#define USE_T0 0x01084821u     /* addu t1, t0, t0 */
static uint32_t J(uint32_t a) { return 0x08000000u | ((a >> 2) & 0x03FFFFFFu); }

static uint32_t end_at;   /* the word after the last one written */
static void wr(uint32_t a, uint32_t w) { CODE[(a - 0x80030000u) >> 2] = w; if (a + 4 > end_at) end_at = a + 4; }

/* straight lines: line i = w[0..3]; the last line ends jr ra; nop */
static uint32_t gen_straight(const uint32_t w[4]) {
    for (uint32_t i = 0; i < LINES; i++) for (int k = 0; k < 4; k++) wr(0x80030000u + i * 16 + k * 4, w[k]);
    wr(0x80030000u + LINES * 16, JR_RA); wr(0x80030000u + LINES * 16 + 4, NOP);
    return 0x80030000u;
}
/* a chain of lines S apart, each entered at word K */
static uint32_t gen_chain(uint32_t K, uint32_t S) {
    uint32_t base = 0x80030000u;
    if (K < 3) {
        for (uint32_t i = 0; i < LINES; i++) {
            uint32_t L = base + i * S, N = base + (i + 1) * S + K * 4;
            for (uint32_t k = K; k < 2; k++) wr(L + k * 4, NOP);
            wr(L + 8, i + 1 < LINES ? J(N) : JR_RA); wr(L + 12, NOP);
        }
        return base + K * 4;
    }
    /* K = 3: line 2i entered at word 3 (its j), the slot at line 2i+1 word 0, the j to line 2i+2 word 3 */
    for (uint32_t i = 0; i < LINES; i += 2) {
        uint32_t L = base + i * S, M = base + (i + 1) * S, N = base + (i + 2) * S + 12;
        wr(L + 12, i + 2 < LINES ? J(N) : JR_RA);
        wr(M, NOP);
    }
    return base + 12;
}

static void run(const char *name, uint32_t fn, uint32_t a0, uint32_t a1) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, a0, a1);
        uint32_t w = ip_call(fn, a0, a1);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (w < wmin) wmin = w; if (w > wmax) wmax = w;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}
static void clear(void) { for (uint32_t a = 0x80030000u; a < end_at; a += 4) wr(a, NOP); end_at = 0x80030000u; }

int main() {
    printf("\ncpu/icache-place\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    end_at = 0x80030000u + 0x42000u;
    clear();
    { const uint32_t w[4] = { NOP, NOP, NOP, NOP }; run("seq", gen_straight(w), DATA, 4); clear(); }
    static const uint32_t S[3] = { 16, 1040, 2064 };
    static const char *const SN[3] = { "s=16", "s=1040", "s=2064" };
    for (uint32_t K = 0; K < 4; K++) for (int s = 0; s < 3; s++) {
        char nm[16]; nm[0] = 'j'; nm[1] = (char)('0' + K); nm[2] = ' '; int i = 0; while (SN[s][i]) { nm[3 + i] = SN[s][i]; i++; } nm[3 + i] = 0;
        run(nm, gen_chain(K, S[s]), DATA, 4); clear();
    }
    { const uint32_t w[4] = { LW_T0, ADDU_A0, NOP, NOP }; uint32_t f = gen_straight(w); run("ld0 d=4", f, DATA, 4); run("ld0 d=1028", f, DATA, 1028); clear(); }
    { const uint32_t w[4] = { ADDU_A0, NOP, NOP, LW_T0 }; uint32_t f = gen_straight(w); run("ld3 d=4", f, DATA, 4); run("ld3 d=1028", f, DATA, 1028); clear(); }
    { const uint32_t w[4] = { SW_T0, ADDU_A0, NOP, NOP }; uint32_t f = gen_straight(w); run("st0 d=4", f, DATA, 4); run("st0 d=1028", f, DATA, 1028); clear(); }
    { const uint32_t w[4] = { ADDU_A0, NOP, NOP, SW_T0 }; uint32_t f = gen_straight(w); run("st3 d=4", f, DATA, 4); run("st3 d=1028", f, DATA, 1028); clear(); }
    { const uint32_t w[4] = { LW_T0, USE_T0, ADDU_A0, NOP }; uint32_t f = gen_straight(w); run("lduse d=4", f, DATA, 4); clear(); }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
