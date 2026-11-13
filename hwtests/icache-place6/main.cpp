/*
 * cpu/icache-place6 - a RAM load right behind a RAM store: the same word, the next, 64 bytes on, a 1 KB row on; warm and cold.
 *
 * WHY (2026-11-13). hwtests/icache-place5 (e02d1289) on the second SCPH-5501 and the PSone: every store pattern at a cold line's end
 * matched this core but one, s2l3 - sw at word 2, lw of the SAME word at word 3 - +379 cycles over 128 lines cold (+3 a line) and +133
 * warm (+1 a line, no miss at all), while lw-then-sw (l2s3) matched. This splits it: the pair at words (0,1), (1,2), (2,3), one nop
 * between at (0,2), (1,3); the load's address the store's (+0), +4, +64, +1028. Every line has the pair; 128 lines, cold and warm.
 * READ IT LIKE THIS. warm rows: the pair's cost in cached code; cold rows: with the next line's fill behind it.
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
static uint32_t gen(uint32_t ws, uint32_t wl, uint32_t off) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < LINES; i++) { CODE[i * 4 + ws] = SW_T0; CODE[i * 4 + wl] = LW_T0 | (off & 0xFFFF); }
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
    printf("\ncpu/icache-place6\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    { uint32_t f = gen(0, 0, 0); for (uint32_t i = 0; i < LINES * 4; i++) CODE[i] = NOP; run("nops", f, 0); }
    static const uint32_t P[5][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 0, 2 }, { 1, 3 } };
    static const uint32_t O[4] = { 0, 4, 64, 1028 };
    for (int p = 0; p < 5; p++) for (int o = 0; o < 4; o++) {
        char nm[20]; int k = 0; nm[k++] = 's'; nm[k++] = (char)('0' + P[p][0]); nm[k++] = 'l'; nm[k++] = (char)('0' + P[p][1]); nm[k++] = ' '; nm[k++] = '+';
        uint32_t v = O[o]; char t[6]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) nm[k++] = t[--n]; nm[k] = 0;
        run(nm, gen(P[p][0], P[p][1], O[o]), 0);
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
