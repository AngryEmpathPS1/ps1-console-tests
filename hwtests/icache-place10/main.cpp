/*
 * cpu/icache-place10 - a RAM load in a taken jump's delay slot at a cold line's last word: does it wait for the line's own burst?
 *
 * WHY (2026-11-13; STATUS "PLAN, hand-over"). hwtests/icache-place2 on the second SCPH-5501: lines [nop .. ; j to the next line's
 * word K at word 2 ; lw t0 in its slot at word 3], the target cold: +129 (K 0) / +126 (K 1) over 128 lines against this core, 0 (K 2);
 * the K that sets the target's entry also sets the line's own (each line is entered at K), so the fill before the load and the fill
 * after it change together. Here they are apart: even lines entered at word Ka, odd lines at Kb (each jump goes to the other's), Ka,
 * Kb = 0, 1, 2, the slot a lw (X lw) or a nop (X nop, the control). 128 lines, cold and warm.
 * READ IT LIKE THIS. cold lw - cold nop, per line pair, against this core: where it is +1 the load waits for something the core
 * does not have it wait for; which of Ka (the burst it runs in) or Kb (the fill after it) moves it says what.
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
static uint32_t J(uint32_t a) { return 0x08000000u | ((a >> 2) & 0x03FFFFFFu); }
#define BASE 0x80030000u
static void wr(uint32_t a, uint32_t w) { CODE[(a - BASE) >> 2] = w; }
static void clear(void) { for (uint32_t i = 0; i < (LINES + 4) * 4; i++) CODE[i] = NOP; }

static uint32_t gen(uint32_t Ka, uint32_t Kb, uint32_t X) {
    clear();
    for (uint32_t i = 0; i < LINES; i++) {
        uint32_t L = BASE + i * 16, Kn = (i & 1) ? Ka : Kb;
        wr(L + 8, i + 1 < LINES ? J(BASE + (i + 1) * 16 + Kn * 4) : JR_RA);
        wr(L + 12, X);
    }
    return BASE + Ka * 4;
}

static void run(const char *name, uint32_t fn) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, DATA, 4);
        uint32_t w = ip_call(fn, DATA, 4);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (w < wmin) wmin = w; if (w > wmax) wmax = w;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}

int main() {
    printf("\ncpu/icache-place10\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    static const uint32_t XS[2] = { NOP, LW_T0 };
    static const char *const XN[2] = { "nop", "lw" };
    for (uint32_t Ka = 0; Ka < 3; Ka++) for (uint32_t Kb = 0; Kb < 3; Kb++) for (int x = 0; x < 2; x++) {
        char nm[16]; int k = 0; nm[k++] = 'K'; nm[k++] = (char)('0' + Ka); nm[k++] = (char)('0' + Kb); nm[k++] = ' ';
        for (int i = 0; XN[x][i]; i++) nm[k++] = XN[x][i]; nm[k] = 0;
        run(nm, gen(Ka, Kb, XS[x]));
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
