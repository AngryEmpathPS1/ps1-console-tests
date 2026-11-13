/*
 * cpu/icache-place11 - dispatch into cold code: a jr whose target a RAM load brought one or two instructions before.
 *
 * WHY (2026-11-13; STATUS "PLAN, hand-over"). PS1-DOS's faster 386 core (pmsplit-sp) runs +2.3 % on the second SCPH-5501 against
 * this core, ~+1.3 cycles a cache fill in its register phases - where straight-line fills, jump chains, stores and loads at a line's
 * end all match (icache-place .. -place10). Its hot path dispatches: a table load, then jr to a cold handler. Chains of 128 cold
 * lines, each ending in a jump to the next line's word 0, cold and warm:
 *   D2   lw t1,0(a0) ; addiu a0,a0,4 ; jr t1 ; nop      (the target loaded two before the jr; a0 walks a table of line addresses)
 *   (a load ONE before the jr is the load delay slot: the jr reads the old register on an R3000 - no such row)
 *   JRC  addiu a1,a1,16 ; nop ; jr a1 ; nop              (jr to a computed register, no load)
 *   J    nop ; addiu a0,a0,4 ; j next ; nop              (a direct jump)
 * READ IT LIKE THIS. cold - warm against J's: what dispatching through a loaded or computed register costs a cold line.
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

#define TABLE ((volatile uint32_t *)0x800C0000)
static uint32_t gen(const uint32_t u[4], int isJ) {
    clear();
    for (uint32_t i = 0; i < LINES; i++) {
        uint32_t L = BASE + i * 16;
        for (int k = 0; k < 4; k++) wr(L + k * 4, u[k]);
        if (isJ) wr(L + 8, J(L + 16));
        TABLE[i] = L + 16;
    }
    wr(BASE + (LINES - 1) * 16 + 8, JR_RA);   /* the last line returns */
    return BASE;
}

static void run(const char *name, uint32_t fn, uint32_t a1v) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, DATA, a1v);
        uint32_t w = ip_call(fn, DATA, a1v);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (w < wmin) wmin = w; if (w > wmax) wmax = w;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}

int main() {
    printf("\ncpu/icache-place11\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    static const uint32_t D2[4] = { 0x8C890000u, 0x24840004u, 0x01200008u, NOP };
    static const uint32_t JRC[4] = { 0x24A50010u, NOP, 0x00A00008u, NOP };
    static const uint32_t JJ[4] = { NOP, 0x24840004u, NOP, NOP };
    run("D2", gen(D2, 0), BASE);
    run("JRC", gen(JRC, 0), BASE);
    run("J", gen(JJ, 1), BASE);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
