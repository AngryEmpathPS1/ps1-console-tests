/*
 * cpu/icache-place18 - a scratchpad store behind RAM stores still in the write queue.
 *
 * WHY (2026-11-13; STATUS "pmsplit-sp's stack"). icache-place17 on three consoles: pmsplit-sp's prologue (five stack stores, then two
 * scratchpad stores) warm 917 over 32 copies, 867 with the scratchpad stores taken out - this core 852 / 854 - and the scratchpad stores
 * with no RAM store before them cost nothing (828 / 825). So a scratchpad store waits on the write queue's RAM stores, ~0.8 cycles each in
 * that prologue, and this core lets it pass. Rows "K G S": K RAM stores (sw zero to one 64-byte block), G nops, S scratchpad stores, then
 * 24 nops; 16 copies falling through, a prologue saving ra in t9 (experiments/mk_place18.js).
 * READ IT LIKE THIS. Each row against K 0 (no RAM store): what the scratchpad store pays for K queued stores, and how the nops between
 * (the queue draining) take it back.
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
#define NCOPY 16
static const uint32_t W0[25] = { 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W1[26] = { 0xAC800000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W2[27] = { 0xAC800000u, 0xAC800004u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W3[28] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W4[29] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W5[30] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W6[31] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0xAC800014u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W7[30] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC600100u, 0xAC600104u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W8[31] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0xAC600100u, 0xAC600104u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W9[31] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W10[32] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W11[33] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0x00000000u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W12[34] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W13[36] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W14[38] = { 0xAC800000u, 0xAC800004u, 0xAC800008u, 0xAC80000Cu, 0xAC800010u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W15[28] = { 0xAC800000u, 0xAC800004u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W16[29] = { 0xAC800000u, 0xAC800004u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static const uint32_t W17[31] = { 0xAC800000u, 0xAC800004u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xAC600100u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u };
static uint32_t gen(const uint32_t *ws, uint32_t n) {
    const uint32_t stride = (n * 4 + 15) & ~15u;
    for (uint32_t i = 0; i < (NCOPY * stride + 64) / 4; i++) CODE[i] = NOP;
    CODE[0] = 0x03E0C821u;                                  /* addu t9, ra, zero */
    CODE[1] = 0x3C031F80u;                                  /* lui v1, 1F80h */
    uint32_t at = 16;
    for (uint32_t k = 0; k < NCOPY; k++) { for (uint32_t q = 0; q < n; q++) CODE[at / 4 + q] = ws[q]; at += stride; }
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
    printf("\ncpu/icache-place18\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("K0 G0 S1", gen(W0, 25), DATA);
    run("K1 G0 S1", gen(W1, 26), DATA);
    run("K2 G0 S1", gen(W2, 27), DATA);
    run("K3 G0 S1", gen(W3, 28), DATA);
    run("K4 G0 S1", gen(W4, 29), DATA);
    run("K5 G0 S1", gen(W5, 30), DATA);
    run("K6 G0 S1", gen(W6, 31), DATA);
    run("K4 G0 S2", gen(W7, 30), DATA);
    run("K5 G0 S2", gen(W8, 31), DATA);
    run("K5 G1 S1", gen(W9, 31), DATA);
    run("K5 G2 S1", gen(W10, 32), DATA);
    run("K5 G3 S1", gen(W11, 33), DATA);
    run("K5 G4 S1", gen(W12, 34), DATA);
    run("K5 G6 S1", gen(W13, 36), DATA);
    run("K5 G8 S1", gen(W14, 38), DATA);
    run("K2 G1 S1", gen(W15, 28), DATA);
    run("K2 G2 S1", gen(W16, 29), DATA);
    run("K2 G4 S1", gen(W17, 31), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
