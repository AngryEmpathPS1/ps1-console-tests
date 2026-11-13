/*
 * cpu/icache-place9 - the sw;lw pair's extra cycle in warm code: the store's data, the load's register, or both?
 *
 * WHY (2026-11-13; STATUS "PLAN: the stream model"). icache-place6 / -place8 warm on the second SCPH-5501 and the PSone: every
 * line [.. sw t0 ; lw t0 ..] runs +0.83 (another block) .. +1.09 (the store's own) a line over this core; place7's sw t2 ; lw t1
 * (independent registers) matches. In those lines the sw stores the t0 the line before loaded three instructions earlier (the
 * store-data wait, v0.377.344, whose cycle this core then absorbs). Repeated every 4-word line, 128 lines, cold and warm:
 *   A  sw t0 ; lw t0,X ; nop ; nop     (place6's)            B  sw t2 ; lw t0,X ; nop ; nop   (the store's data independent)
 *   C  sw t0 ; lw t1,X ; nop ; nop     (t0 never loaded)     D  lw t0,X ; nop ; nop ; sw t0   (the wait, a load right after it)
 *   E  sw t0 ; nop ; lw t0,X ; nop     (the store 2 after the line before's load, a nop between)
 * each with X = 4 (the store's 64-byte block) and X = 1028 (a row away).
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
    printf("\ncpu/icache-place9\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    { static const uint32_t z[4] = { NOP, NOP, NOP, NOP }; run("nops", gen(z), 0); }
    static const uint32_t X[2] = { 4, 1028 };
    for (int x = 0; x < 2; x++) {
        const uint32_t o = X[x];
        const uint32_t U[5][4] = {
            { SW_T0Z, LW_T0X(o), NOP, NOP }, { SW_T2Z, LW_T0X(o), NOP, NOP }, { SW_T0Z, LW_T1X(o), NOP, NOP },
            { LW_T0X(o), NOP, NOP, SW_T0Z }, { SW_T0Z, NOP, LW_T0X(o), NOP } };
        static const char *const N[2][5] = { { "A x4", "B x4", "C x4", "D x4", "E x4" }, { "A x1028", "B x1028", "C x1028", "D x1028", "E x1028" } };
        for (int u = 0; u < 5; u++) run(N[x][u], gen(U[u]), 0);
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
