/*
 * cpu/icache-place13 - store runs, a store before a call, and calls / returns between cold lines.
 *
 * WHY (2026-11-13; STATUS "Stream refit step 4"). PS1-DOS's faster 386 core (pmsplit-sp) still runs +2.16 % on the second SCPH-5501
 * against this core. The lines that miss most in its register phase (experiments/pm386_hotlines.js) hold runs of stores (sw zero ;
 * sw s2 ; sw zero), a store right before a jal, small helpers called and returned from (jal .. jr ra) between cold lines, a store a
 * little before a load of its block - none of which icache-place .. -place12 time. A prologue line saves ra in t9 (the calls clobber
 * ra); then 128 cold lines of one pattern; a last line returns through t9. a1 a RAM word (800C0000h). Cold and warm:
 *   W2 / W3 / W3e   two stores at words 0-1 / three at 0-2 / three at 1-3
 *   WL              sw zero,0(a1) ; nop ; lw t0,8(a1) ; nop      (a load two behind a store, its own block)
 *   CALL            jal F_i ; nop ; nop ; nop, F_i a cold line elsewhere: jr ra ; nop
 *   SCALL           sw zero,0(a1) ; jal F_i ; nop ; nop
 *   CALLS           jal F_i ; nop ; nop ; nop, F_i: jr ra ; sw zero,0(a1)     (a store in the return's slot)
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
#define ADDU_A0 0x00852021u    /* addu a0, a0, a1 */
#define BASE 0x80030000u

#define SW_T0 0xAC880000u      /* sw t0, 0(a0) */
#define ADDIU_A0 0x24840404u   /* addiu a0, a0, 1028 */
#define LW_T1 0x8C890008u      /* lw t1, 8(a0) */
#define SW_T2 0xAC8A0000u      /* sw t2, 0(a0) */
#define SW_T0B 0xAC880010u     /* sw t0, 16(a0) */
#define FBASE 0x80034800u
static uint32_t gen(const uint32_t u[4], int call, uint32_t f0, uint32_t f1) {
    for (uint32_t i = 0; i < (LINES + 4) * 4; i++) CODE[i] = NOP;
    CODE[0] = 0x03E0C821u;                                  /* addu t9, ra, zero */
    for (uint32_t i = 0; i < LINES; i++) {
        volatile uint32_t *L = CODE + (i + 1) * 4;
        for (int k = 0; k < 4; k++) L[k] = u[k];
        if (call) {
            uint32_t F = FBASE + i * 16;
            L[call - 1] = 0x0C000000u | ((F >> 2) & 0x03FFFFFFu);   /* jal F */
            volatile uint32_t *FL = (volatile uint32_t *)F;
            FL[0] = f0; FL[1] = f1; FL[2] = NOP; FL[3] = NOP;
        }
    }
    CODE[(LINES + 1) * 4] = 0x03200008u;                     /* jr t9 */
    CODE[(LINES + 1) * 4 + 1] = NOP;
    return BASE;
}
#define SWZ(o) (0xACA00000u | (o))
#define LW_T0X(x) (0x8C880000u | (x))
#define LW_T1X(x) (0x8C890000u | (x))
#define SW_T0Z 0xAC880000u
#define SW_T2Z 0xAC8A0000u

static void run(const char *name, uint32_t fn, uint32_t a1) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, DATA, a1);
        uint32_t wv = ip_call(fn, DATA, a1);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (wv < wmin) wmin = wv; if (wv > wmax) wmax = wv;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}

int main() {
    printf("\ncpu/icache-place13\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    static const uint32_t U[8][4] = {
        { NOP, NOP, NOP, NOP },
        { SWZ(0), SWZ(4), NOP, NOP }, { SWZ(0), SWZ(4), SWZ(8), NOP }, { NOP, SWZ(0), SWZ(4), SWZ(8) },
        { SWZ(0), NOP, 0x8CA80008u, NOP },
        { NOP, NOP, NOP, NOP }, { SWZ(0), NOP, NOP, NOP }, { NOP, NOP, NOP, NOP } };
    static const char *const N[8] = { "nops", "W2", "W3", "W3e", "WL", "CALL", "SCALL", "CALLS" };
    static const int C[8] = { 0, 0, 0, 0, 0, 1, 2, 1 };
    for (int u = 0; u < 8; u++) run(N[u], gen(U[u], C[u], JR_RA, u == 7 ? SWZ(0) : NOP), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
