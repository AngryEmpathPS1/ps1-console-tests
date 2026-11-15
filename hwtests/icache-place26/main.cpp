/*
 * cpu/icache-place26 - two loads back to back in a cold line: by the first load's kind, the second's, their word and the use behind.
 *
 * WHY (2026-11-14; STATUS "pmsplit-sp's t2_poll"). icache-place25 (three consoles, identical): pmsplit-sp's t2_poll +32 cold over
 * 32 copies over this core, gone with the Timer 2 read at its cold line's word 0 or the RAM load behind it at word 1 made a nop;
 * place24's T2Pnt (the timer read a RAM read) +32 as well. experiments/mk_place26.js writes this file.
 * ROWS "<A><B>_w<W>[_u]": load A at line word W, load B right behind it, then andi v1 (A's register) if _u, nops to ten words.
 *   T Timer 2 (1F801120h)   R RAM 80020204h   r RAM 80020200h (the word before)   F RAM 80040200h (another page)   S scratchpad
 * READ IT LIKE THIS. Console - this core per row, cold and warm, over 32 copies.
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
#define NCOPY 32
static const uint32_t W_TR_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TR_w1_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TR_w2_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TR_w3_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TR_w0[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8D248044u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RR_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RR_w1_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RR_w2_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RR_w3_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_rR_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238040u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RF_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8CE40000u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TF_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8CE40000u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TS_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8C480080u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_SR_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C430080u, 0x8D248044u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RS_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8C480080u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_TT_w0_u[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8C431120u, 0x8C481120u, 0x3063FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W_RR_w0[13] = { 0x3C098002u, 0x352981C0u, 0x3C078004u, 0x34E70200u, 0x3C021F80u, 0x8D238044u, 0x8D248044u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static uint32_t gen(const uint32_t *ws, uint32_t n, uint32_t off) {
    const uint32_t stride = (off + n * 4 + 15) & ~15u;
    for (uint32_t i = 0; i < (NCOPY * stride + 64) / 4; i++) CODE[i] = NOP;
    CODE[0] = 0x03E0C821u;                                  /* addu t9, ra, zero */
    uint32_t at = 16;
    for (uint32_t k = 0; k < NCOPY; k++) { for (uint32_t q = 0; q < n; q++) CODE[(at + off) / 4 + q] = ws[q]; at += stride; }
    CODE[at / 4] = 0x03200008u; CODE[at / 4 + 1] = NOP;    /* jr t9 */
    return BASE;
}
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
    printf("\ncpu/icache-place26\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("nops", gen(W_TR_w0_u, 0, 0), DATA);
    run("TR_w0_u", gen(W_TR_w0_u, 13, 12), DATA);
    run("TR_w1_u", gen(W_TR_w1_u, 13, 0), DATA);
    run("TR_w2_u", gen(W_TR_w2_u, 13, 4), DATA);
    run("TR_w3_u", gen(W_TR_w3_u, 13, 8), DATA);
    run("TR_w0", gen(W_TR_w0, 13, 12), DATA);
    run("RR_w0_u", gen(W_RR_w0_u, 13, 12), DATA);
    run("RR_w1_u", gen(W_RR_w1_u, 13, 0), DATA);
    run("RR_w2_u", gen(W_RR_w2_u, 13, 4), DATA);
    run("RR_w3_u", gen(W_RR_w3_u, 13, 8), DATA);
    run("rR_w0_u", gen(W_rR_w0_u, 13, 12), DATA);
    run("RF_w0_u", gen(W_RF_w0_u, 13, 12), DATA);
    run("TF_w0_u", gen(W_TF_w0_u, 13, 12), DATA);
    run("TS_w0_u", gen(W_TS_w0_u, 13, 12), DATA);
    run("SR_w0_u", gen(W_SR_w0_u, 13, 12), DATA);
    run("RS_w0_u", gen(W_RS_w0_u, 13, 12), DATA);
    run("TT_w0_u", gen(W_TT_w0_u, 13, 12), DATA);
    run("RR_w0", gen(W_RR_w0, 13, 12), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
