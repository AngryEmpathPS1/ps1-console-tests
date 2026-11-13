/*
 * cpu/icache-place7 - a store next to a load in cached and cold code: is it the ORDER (a load right behind a store) or the store's
 * DATA (a register loaded three instructions before)?
 *
 * WHY (2026-11-13). hwtests/icache-place6 (21156c14) on the second SCPH-5501 and the PSone, warm (no miss): the pair sw t0 / lw t0
 * adjacent in every line costs +1 a line on silicon over this core at every address (+0, +4, +64, +1028), one nop between costs
 * nothing - but in those same rows the NEXT line's sw t0 stores the register loaded 3 instructions before it (2 in the matching
 * rows). Here the two are taken apart, 8-word units (two lines) repeated, a0 a fixed RAM word, the registers named:
 *   A  sw t2 ; lw t1  (adjacent, independent)          B  sw t2 ; nop ; lw t1
 *   C  lw t0 ; nop ; nop ; sw t0   (the store's data loaded 3 before, nothing after for 4)
 *   D  lw t0 ; nop ; sw t0         (2 before)          E  lw t0 ; nop ; nop ; nop ; sw t0   (4 before)
 *   F  lw t0 ; nop ; nop ; sw t2   (3 before, the store's data independent)
 *   G  sw t2 ; lw t1 at the unit's words 3 and 4 (the pair across a line boundary)
 * READ IT LIKE THIS. warm rows are the cached-code cost; cold rows add the fills (two a unit).
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
static uint32_t gen(const uint32_t u[8]) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < LINES * 4; i++) CODE[i] = u[i & 7];
    CODE[LINES * 4] = JR_RA;
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
    printf("\ncpu/icache-place7\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    static const uint32_t U[8][8] = {
        { NOP, NOP, NOP, NOP, NOP, NOP, NOP, NOP },
        { SW_T2, LW_T1, NOP, NOP, NOP, NOP, NOP, NOP },
        { SW_T2, NOP, LW_T1, NOP, NOP, NOP, NOP, NOP },
        { LW_T0, NOP, NOP, SW_T0B, NOP, NOP, NOP, NOP },
        { LW_T0, NOP, SW_T0B, NOP, NOP, NOP, NOP, NOP },
        { LW_T0, NOP, NOP, NOP, SW_T0B, NOP, NOP, NOP },
        { LW_T0, NOP, NOP, SW_T2, NOP, NOP, NOP, NOP },
        { NOP, NOP, NOP, SW_T2, LW_T1, NOP, NOP, NOP } };
    static const char *const N[8] = { "nops", "A sw;lw", "B sw;nop;lw", "C lw..3 sw", "D lw..2 sw", "E lw..4 sw", "F lw..3 sw2", "G sw|lw" };
    for (int u = 0; u < 8; u++) run(N[u], gen(U[u]), 0);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
