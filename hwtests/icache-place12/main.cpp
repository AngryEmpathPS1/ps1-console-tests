/*
 * cpu/icache-place12 - the scratchpad in cold code, and two RAM loads back to back at a line's start.
 *
 * WHY (2026-11-13; STATUS "PLAN, hand-over"). PS1-DOS's faster 386 core (pmsplit-sp) runs +2.3 % on the second SCPH-5501 against
 * this core; the lines that miss most in its register phase (experiments/pm386_hotlines.js) are full of scratchpad accesses
 * (lui 0x1f80 ; lw / lbu / sw through it) and of RAM load pairs at a line's start (lw a1,0xe8(s0) ; lw a0,0x12c(s0)), which no
 * placement test has covered (icache-place11: dispatch through a loaded jr matches). Each 4-word pattern every line, 128 lines,
 * cold and warm; a0 the scratchpad (1F800000h), a1 a RAM word (800C0000h):
 *   S0 / S3   lw t0,0x84(a0) at word 0 / word 3          SB   lbu t0,0x1e0(a0) at word 0
 *   W0 / W3   sw t0,0x84(a0) at word 0 / word 3
 *   RR        lw t0,0(a1) ; lw t1,4(a1)                  RS   lw t0,0(a1) ; lw t1,0x84(a0)      SR   lw t1,0x84(a0) ; lw t0,0(a1)
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
static uint32_t gen(const uint32_t u[4]) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < LINES * 4; i++) CODE[i] = u[i & 3];
    CODE[LINES * 4] = JR_RA;
    return BASE;
}
#define LW_T0X(x) (0x8C880000u | (x))
#define LW_T1X(x) (0x8C890000u | (x))
#define SW_T0Z 0xAC880000u
#define SW_T2Z 0xAC8A0000u

static void run(const char *name, uint32_t fn) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, 0x1F800000u, DATA);
        uint32_t wv = ip_call(fn, 0x1F800000u, DATA);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (wv < wmin) wmin = wv; if (wv > wmax) wmax = wv;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}

int main() {
    printf("\ncpu/icache-place12\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    static const uint32_t U[10][4] = {
        { NOP, NOP, NOP, NOP },
        { 0x8C880084u, NOP, NOP, NOP }, { NOP, NOP, NOP, 0x8C880084u }, { 0x908801E0u, NOP, NOP, NOP },
        { 0xAC880084u, NOP, NOP, NOP }, { NOP, NOP, NOP, 0xAC880084u },
        { 0x8CA80000u, 0x8CA90004u, NOP, NOP }, { 0x8CA80000u, 0x8C890084u, NOP, NOP }, { 0x8C890084u, 0x8CA80000u, NOP, NOP },
        { 0x8CA80000u, 0x8CA90004u, NOP, NOP } };
    static const char *const N[9] = { "nops", "S0", "S3", "SB", "W0", "W3", "RR", "RS", "SR" };
    for (int u = 0; u < 9; u++) run(N[u], gen(U[u]));
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
