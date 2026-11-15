/*
 * cpu/icache-place25 - icache-place24's T2P row (pmsplit-sp's t2_poll, +33 cold on three consoles) taken apart: one change a row.
 *
 * WHY (2026-11-14; STATUS "pmsplit-sp's t2_poll"). icache-place24's T2P read +33 cold over this core on three consoles; before it, pmsplit-replaystk3-5783c5b2 (the replay stopped at each 386
 * instruction of a period, 32 runs a point averaged) reads the five segments a period that run this code +20..+37 cycles each over
 * this core, the other 315 ~+1.5 each. experiments/mk_place25.js writes this file from the snapshot.
 *   T2P    the row as in place24: [lui t1 ; ori t1 ; lui v0] [lw v1,Timer 2 ; lw v0,gp ; andi ; lw a0,gp] [subu ; andi ; addu v0,a0,v0 ;
 *          sw v0,gp] [jr -> addu ; sw v1,gp]   (t1 = gp = 800281C0h; the loads read 80020204h / 80020200h, the stores write them back)
 *   NOST / NOST1 / NOST2   both stores / the word-3 store / the next line's store -> nops;  NOLDA / NOLDV  lw a0 / lw v0 -> nops
 *   NOTMR  the timer read -> nop;  NOUSE  addu v1,a0,v0 (a0 still read);  NOUSE2  addu v1,v0,zero (a0 not read)
 *   STFAR  the stores to 80020300h / 304h instead of the words just loaded
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
static const uint32_t W_T2P[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0xAD228040u, 0x01604021u, 0xAD238044u };
static const uint32_t W_NOST[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0x00000000u, 0x01604021u, 0x00000000u };
static const uint32_t W_NOST1[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0x00000000u, 0x01604021u, 0xAD238044u };
static const uint32_t W_NOST2[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0xAD228040u, 0x01604021u, 0x00000000u };
static const uint32_t W_NOLDA[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x00000000u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0xAD228040u, 0x01604021u, 0xAD238044u };
static const uint32_t W_NOLDV[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x00000000u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0xAD228040u, 0x01604021u, 0xAD238044u };
static const uint32_t W_NOTMR[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x00000000u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0xAD228040u, 0x01604021u, 0xAD238044u };
static const uint32_t W_NOUSE[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821821u, 0xAD228040u, 0x01604021u, 0xAD238044u };
static const uint32_t W_NOUSE2[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00401821u, 0xAD228040u, 0x01604021u, 0xAD238044u };
static const uint32_t W_STFAR[13] = { 0x3C098002u, 0x352981C0u, 0x3C021F80u, 0x8C431120u, 0x8D228044u, 0x3063FFFFu, 0x8D248040u, 0x00621023u, 0x3042FFFFu, 0x00821021u, 0xAD228140u, 0x01604021u, 0xAD238144u };
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
    printf("\ncpu/icache-place25\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("nops", gen(W_T2P, 0, 0), DATA);
    run("T2P", gen(W_T2P, 13, 4), DATA);
    run("NOST", gen(W_NOST, 13, 4), DATA);
    run("NOST1", gen(W_NOST1, 13, 4), DATA);
    run("NOST2", gen(W_NOST2, 13, 4), DATA);
    run("NOLDA", gen(W_NOLDA, 13, 4), DATA);
    run("NOLDV", gen(W_NOLDV, 13, 4), DATA);
    run("NOTMR", gen(W_NOTMR, 13, 4), DATA);
    run("NOUSE", gen(W_NOUSE, 13, 4), DATA);
    run("NOUSE2", gen(W_NOUSE2, 13, 4), DATA);
    run("STFAR", gen(W_STFAR, 13, 4), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
