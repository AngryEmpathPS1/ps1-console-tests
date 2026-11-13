/*
 * cpu/icache-place4 - runs of n consecutive cold lines that each end in a RAM load at word 3: where do the 3 cycles begin?
 *
 * WHY (2026-11-13). hwtests/icache-place3 (4f606ba7) on the second SCPH-5501 and the PSone: the load every line costs 3 a line more
 * than here, every other line (or 3rd, 4th) not at all. This runs n load lines then g nop lines, n = 1, 2, 3, 4, 8, 128, g = 1, 2.
 * The rest of icache-place3's note, for the record:
 * hwtests/icache-place (6bd79e93) on the second SCPH-5501 and the PSone: 128 cold lines each with a
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

static uint32_t gen(uint32_t n, uint32_t g) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0, p = 0; i < LINES; i++, p = (p + 1) % (n + g)) if (p < n) { CODE[i * 4] = ADDU_A0; CODE[i * 4 + 3] = LW_T0; }
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
    printf("\ncpu/icache-place4\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("nops", gen(0, 1), 0);
    static const uint32_t N[6] = { 1, 2, 3, 4, 8, 128 };
    for (uint32_t g = 1; g <= 2; g++) for (int j = 0; j < 6; j++) {
        if (N[j] == 128 && g == 2) continue;
        char nm[16]; int o = 0; nm[o++] = 'n';
        if (N[j] >= 100) nm[o++] = (char)('0' + N[j] / 100);
        if (N[j] >= 10) nm[o++] = (char)('0' + (N[j] / 10) % 10);
        nm[o++] = (char)('0' + N[j] % 10); nm[o++] = ' '; nm[o++] = 'g'; nm[o++] = (char)('0' + g); nm[o] = 0;
        run(nm, gen(N[j], g), 4);
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
