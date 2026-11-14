/*
 * cpu/icache-place23 - pmsplit-sp's RAM data loads (its opcode-byte fetches and dispatch-table loads), as placement rows.
 *
 * WHY (2026-11-14; STATUS "pmsplit-sp's RAM data loads"). With pmsplit-sp's stack in the scratchpad (pmsplit-replaystk-47344c51) the
 * console's steady period is 91,580 cycles and this core's 91,236 (+0.38 %); the only RAM traffic left is 1,226 data loads a period,
 * 7.0 cycles each here, from the five instructions these rows hold. experiments/mk_place23.js writes this file from the snapshot.
 *   FETCH   8001AC40h: scratchpad load, lbu from the guest's memory (a0 = 80120570h), two scratchpad stores
 *   FETCHns the same with the stores turned into nops
 *   FD      FETCH and the dispatch behind it (lw from the table at 8001EC30h, nop, jr -> addu)
 *   DISP    the dispatch alone
 *   FETCH2  80015740h's fetch, TBL2 / TBL3 two more dispatch loads (tables at 8001E3C8h / 8001E3ACh)
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
static const uint32_t W_FETCH[14] = { 0x3C048012u, 0x34840570u, 0x3C051F80u, 0x3C031F80u, 0x8CA20080u, 0x24860001u, 0x24420001u, 0x908C0000u, 0xAC66008Cu, 0xACA20080u, 0x00000000u, 0x2D8200ECu, 0x00000000u, 0x3C038002u };
static const uint32_t W_FETCHns[14] = { 0x3C048012u, 0x34840570u, 0x3C051F80u, 0x3C031F80u, 0x8CA20080u, 0x24860001u, 0x24420001u, 0x908C0000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x2D8200ECu, 0x00000000u, 0x3C038002u };
static const uint32_t W_FD[21] = { 0x3C048012u, 0x34840570u, 0x3C051F80u, 0x3C031F80u, 0x8CA20080u, 0x24860001u, 0x24420001u, 0x908C0000u, 0xAC66008Cu, 0xACA20080u, 0x00000000u, 0x2D8200ECu, 0x00000000u, 0x3C038002u, 0x000C1080u, 0x2463EC2Cu, 0x00621021u, 0x8C420000u, 0x00000000u, 0x00404821u, 0x00000000u };
static const uint32_t W_DISP[9] = { 0x340C0001u, 0x3C038002u, 0x000C1080u, 0x2463EC2Cu, 0x00621021u, 0x8C420000u, 0x00000000u, 0x00404821u, 0x00000000u };
static const uint32_t W_FETCH2[16] = { 0x3C048012u, 0x34840570u, 0x3C051F80u, 0x3C031F80u, 0x8C650080u, 0x24660108u, 0x24870001u, 0x24A50001u, 0x90820000u, 0xACC7008Cu, 0xAC650080u, 0x3C061F80u, 0x000228C2u, 0x00022182u, 0x30A50007u, 0x30420007u };
static const uint32_t W_TBL2[9] = { 0x34030000u, 0x3C088002u, 0x00031880u, 0x2508E3C8u, 0x01031821u, 0x8C630000u, 0x00000000u, 0x00604821u, 0x00E02825u };
static const uint32_t W_TBL3[9] = { 0x34020002u, 0x3C038002u, 0x00021080u, 0x2463E3ACu, 0x00621021u, 0x8C420000u, 0x00000000u, 0x00404821u, 0x00000000u };
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
    printf("\ncpu/icache-place23\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    for (uint32_t a = 0x80120568u; a < 0x80120578u; a++) *(volatile uint8_t *)a = 1;   /* the guest bytes: table index 1 */
    run("nops", gen(W_FETCH, 0, 0), DATA);
    run("FETCH", gen(W_FETCH, 14, 0), DATA);
    run("FETCHns", gen(W_FETCHns, 14, 0), DATA);
    run("FD", gen(W_FD, 21, 0), DATA);
    run("DISP", gen(W_DISP, 9, 0), DATA);
    run("FETCH2", gen(W_FETCH2, 16, 0), DATA);
    run("TBL2", gen(W_TBL2, 9, 0), DATA);
    run("TBL3", gen(W_TBL3, 9, 12), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
