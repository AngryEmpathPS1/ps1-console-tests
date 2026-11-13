/*
 * cpu/icache-place3 - a RAM load at a cold line's last word, every k-th line: when does the console charge it 7 and when 4?
 *
 * WHY (2026-11-13). hwtests/icache-place (6bd79e93) on the second SCPH-5501 and the PSone: 128 cold lines each with a
 * `lw` at word 3 (the next line's miss right behind it) run 16.4 cycles a line, a load 7.1 above the nops, where this
 * core reads 13.4 (4.2). But hwtests/load-fill's "RAM w=3" - the same load at word 3 with the next line cold, every
 * FOURTH line - reads 4.15 on the consoles and here alike. So what costs the 3 is the lines' sequence. This counts it:
 * the load every k = 1, 2, 3, 4 lines, at a fixed word (load-fill's) and at a moving one (icache-place's), and the same
 * at word 2.
 *
 * WHAT IT DOES. As icache-place: 128 cache lines of generated code at 80030000h, cold (FlushCache, then the call) and
 * warm, 16 times, min and max; Timer 2 (system clock). A line with a load is [addu a0,a0,a1; nop; nop; lw t0,0(a0)]
 * (w3) or [addu; nop; lw; nop] (w2); the others are nops. a1 = 0 (fixed word) or 4 (moving).
 * READ IT LIKE THIS. (cold - warm - (nops' cold - warm)) / loads is a load's cost over a nop line's fill.
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

static uint32_t gen(uint32_t k, uint32_t w) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    if (k) for (uint32_t i = 0; i < LINES; i += k) { CODE[i * 4] = ADDU_A0; CODE[i * 4 + w] = LW_T0; }
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
    printf("\ncpu/icache-place3\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("nops", gen(0, 3), 0);
    for (uint32_t w = 2; w <= 3; w++) for (uint32_t k = 1; k <= 4; k++) for (uint32_t m = 0; m < 2; m++) {
        char nm[16] = "w? k? fixed";
        nm[1] = (char)('0' + w); nm[4] = (char)('0' + k);
        if (m) { nm[6] = 'm'; nm[7] = 'o'; nm[8] = 'v'; nm[9] = 'e'; nm[10] = 0; }
        run(nm, gen(k, w), m ? 4 : 0);
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
