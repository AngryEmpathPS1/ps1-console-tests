/*
 * cpu/icache-place21 - a scratchpad store behind K stack loads, at each word of its cold line (icache-place20: EPI2's +43 / -38 / +29 by the store's word).
 *
 * WHY (2026-11-13; STATUS "pmsplit-sp's stack"). Moving pmsplit-sp's stack from RAM to the scratchpad in its replay
 * (pmsplit-replaystk-47344c51) takes the console's steady period from 111,104 to 91,580 cycles and this core's from 109,824 to 91,236:
 * the stack's ~6,250 RAM accesses a period (3,289 loads, 2,969 stores from 34 instructions - runs of five or six register saves and
 * restores) cost the console 936 cycles more than this core, 73 % of the replay's +1,280. Each row here is one of those sequences, its
 * own words (experiments/mk_place16.js from the snapshot; registers renamed, control turned into nops), at its own offset in the cache
 * line, 32 copies falling through one into the next; the stack is the harness's (RAM, top of memory), a1 / v1 the scratchpad as there.
 *   PRO   8001ABE8h-8001AC2Ch: lui a1,1F80h ; sp -30h ; sw s0 ; three scratchpad loads ; ALU ; five stack stores ; two scratchpad stores
 *   PRO2  80015710h-8001572Ch: sp -28h ; subu ; four stack stores ; (a branch) ; a fifth
 *   EPI   8001AD00h-8001AD1Ch: six stack loads, the return (a nop here) and sp +30h
 *   EPI2  800158B0h-800158D0h: five stack loads with an addiu among them, a scratchpad store (v1 set to 1F800108h first, as there)
 * READ IT LIKE THIS. Console - this core per row, cold and warm: a row off by ~0.15+ a stack access is where the replay's gap is.
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
static const uint32_t W_L1_w0[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L1_w1[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L1_w2[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L1_w3[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L2_w0[5] = { 0x3C031F80u, 0x24630108u, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L2_w1[5] = { 0x3C031F80u, 0x24630108u, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L2_w2[5] = { 0x3C031F80u, 0x24630108u, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L2_w3[5] = { 0x3C031F80u, 0x24630108u, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L3_w0[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L3_w1[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L3_w2[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L3_w3[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L4_w0[7] = { 0x3C031F80u, 0x24630108u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L4_w1[7] = { 0x3C031F80u, 0x24630108u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L4_w2[7] = { 0x3C031F80u, 0x24630108u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L4_w3[7] = { 0x3C031F80u, 0x24630108u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L5_w0[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L5_w1[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L5_w2[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_L5_w3[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC620080u };
static const uint32_t W_R1_w0[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R1_w1[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R1_w2[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R1_w3[4] = { 0x3C031F80u, 0x24630108u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R3_w0[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R3_w1[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R3_w2[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R3_w3[6] = { 0x3C031F80u, 0x24630108u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R5_w0[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R5_w1[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R5_w2[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
static const uint32_t W_R5_w3[8] = { 0x3C031F80u, 0x24630108u, 0x8FAB0024u, 0x8FAF0020u, 0x8FAE001Cu, 0x8FAD0018u, 0x8FAC0014u, 0xAC820080u };
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
    printf("\ncpu/icache-place21\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("nops", gen(W_L1_w0, 0, 0), DATA);
    run("L1-w0", gen(W_L1_w0, 4, 4), DATA);
    run("L1-w1", gen(W_L1_w1, 4, 8), DATA);
    run("L1-w2", gen(W_L1_w2, 4, 12), DATA);
    run("L1-w3", gen(W_L1_w3, 4, 0), DATA);
    run("L2-w0", gen(W_L2_w0, 5, 0), DATA);
    run("L2-w1", gen(W_L2_w1, 5, 4), DATA);
    run("L2-w2", gen(W_L2_w2, 5, 8), DATA);
    run("L2-w3", gen(W_L2_w3, 5, 12), DATA);
    run("L3-w0", gen(W_L3_w0, 6, 12), DATA);
    run("L3-w1", gen(W_L3_w1, 6, 0), DATA);
    run("L3-w2", gen(W_L3_w2, 6, 4), DATA);
    run("L3-w3", gen(W_L3_w3, 6, 8), DATA);
    run("L4-w0", gen(W_L4_w0, 7, 8), DATA);
    run("L4-w1", gen(W_L4_w1, 7, 12), DATA);
    run("L4-w2", gen(W_L4_w2, 7, 0), DATA);
    run("L4-w3", gen(W_L4_w3, 7, 4), DATA);
    run("L5-w0", gen(W_L5_w0, 8, 4), DATA);
    run("L5-w1", gen(W_L5_w1, 8, 8), DATA);
    run("L5-w2", gen(W_L5_w2, 8, 12), DATA);
    run("L5-w3", gen(W_L5_w3, 8, 0), DATA);
    run("R1-w0", gen(W_R1_w0, 4, 4), DATA);
    run("R1-w1", gen(W_R1_w1, 4, 8), DATA);
    run("R1-w2", gen(W_R1_w2, 4, 12), DATA);
    run("R1-w3", gen(W_R1_w3, 4, 0), DATA);
    run("R3-w0", gen(W_R3_w0, 6, 12), DATA);
    run("R3-w1", gen(W_R3_w1, 6, 0), DATA);
    run("R3-w2", gen(W_R3_w2, 6, 4), DATA);
    run("R3-w3", gen(W_R3_w3, 6, 8), DATA);
    run("R5-w0", gen(W_R5_w0, 8, 4), DATA);
    run("R5-w1", gen(W_R5_w1, 8, 8), DATA);
    run("R5-w2", gen(W_R5_w2, 8, 12), DATA);
    run("R5-w3", gen(W_R5_w3, 8, 0), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
