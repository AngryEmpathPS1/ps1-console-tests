/*
 * cpu/icache-place15 - cold code that jumps between DRAM pages.
 *
 * WHY (2026-11-13; STATUS "The PSone back"). PS1-DOS's faster 386 core (pmsplit-sp) runs +3.5 % on the consoles against this core in
 * its register phase, ~1.24 cycles a miss over its 3,101 misses a period (experiments/pm386_pathtrace.js); every pattern in its hot
 * lines matches alone (icache-place11 .. -place13), but every placement test fills its code inside one DRAM page, and pmsplit-sp's hot
 * code spans ~36 KB with ~900 jumps a period between lines. Here 128 cold lines [X ; nop ; j next ; nop], the odd ones moved by K (a
 * multiple of 4 KB, so every line keeps its own cache index):
 *   J0 / J4K / J64K / J1M   X = nop, K = 0 / 1000h / 10000h / 100000h
 *   L0N / L0F               X = lw t0,0(a1), K = 0, a1 near the code (80030F00h) / far (800C0000h)
 *   L4N / L4F               the same, K = 1000h
 * READ IT LIKE THIS. Each row against J0 / L0x is what a page change between fills costs, if anything.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE (*(volatile uint32_t *)0x1F801124)
#define I_MASK  (*(volatile uint32_t *)0x1F801074)
#define CODE ((volatile uint32_t *)0x80030000)
#define DATA 0x800C0000u

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
#define LINES 128
static uint32_t at(uint32_t i, uint32_t K) { return BASE + i * 16 + (i & 1) * K; }
static uint32_t gen(uint32_t X, uint32_t K) {
    for (uint32_t i = 0; i <= LINES + 1; i++) { volatile uint32_t *L = (volatile uint32_t *)at(i, K); L[0] = NOP; L[1] = NOP; L[2] = NOP; L[3] = NOP; }
    for (uint32_t i = 0; i <= LINES; i++) {
        volatile uint32_t *L = (volatile uint32_t *)at(i, K);
        if (i == 0) L[0] = 0x03E0C821u;                     /* addu t9, ra, zero */
        else L[0] = X;
        L[2] = 0x08000000u | ((at(i + 1, K) >> 2) & 0x03FFFFFFu);   /* j next */
    }
    volatile uint32_t *E = (volatile uint32_t *)at(LINES + 1, K);
    E[0] = 0x03200008u; E[1] = NOP;                         /* jr t9 */
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
    printf("\ncpu/icache-place15\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("J0", gen(NOP, 0), DATA);
    run("J4K", gen(NOP, 0x1000), DATA);
    run("J64K", gen(NOP, 0x10000), DATA);
    run("J1M", gen(NOP, 0x100000), DATA);
    run("L0N", gen(0x8CA80000u, 0), 0x80030F00u);
    run("L0F", gen(0x8CA80000u, 0), DATA);
    run("L4N", gen(0x8CA80000u, 0x1000), 0x80030F00u);
    run("L4F", gen(0x8CA80000u, 0x1000), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
