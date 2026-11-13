/*
 * cpu/icache-place5 - RAM stores the stream defers behind the next line's fill: how many, where, to which row.
 *
 * WHY (2026-11-13). PS1-DOS's pmsplit gap (its -O2 386 core +0.63% on the second SCPH-5501 and the PSone, -Os -0.12%), fitted
 * phase by phase against this core's stream-rule counts (experiments/pm386_phaseprof3.js, pm386_fitphase.js): the term that
 * separates the two builds is the RAM STORES deferred behind a fill (-Os 3,676,307 of them, -O2 789,149), at -1 to -3 cycles
 * each on silicon. icache-place's one store at word 3 per line matched (st3 d=4 +8, the cold rows' usual), its row-apart one
 * read +19. This times store patterns at a cold line's end, every line, 128 lines, cold and warm:
 *   s3 / s23 / s123   sw at word 3; at 2 and 3; at 1, 2 and 3 (same RAM word, a0 fixed)
 *   s23d s123d        the same, each store 1028 bytes (a 1 KB row) after the one before (a0 += 1028 at word 0)
 *   l2s3 / s2l3       a lw and a sw at words 2 and 3, each order
 *   s3s0              sw at word 3 and at the next line's word 0 (every other line: lines 2i, 2i+1)
 * READ IT LIKE THIS. cold - warm against the nops' (the first row) is the stores' cost over a fill; the same in the other
 * columns of icache-place's st rows.
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
static uint32_t gen(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3, uint32_t every) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < LINES; i += every) { CODE[i * 4] = w0; CODE[i * 4 + 1] = w1; CODE[i * 4 + 2] = w2; CODE[i * 4 + 3] = w3; }
    CODE[LINES * 4] = JR_RA;
    return BASE;
}
static uint32_t gen_s3s0(void) {
    for (uint32_t i = 0; i < (LINES + 2) * 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < LINES; i += 2) { CODE[i * 4 + 3] = SW_T0; CODE[(i + 1) * 4] = SW_T0; }
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
    printf("\ncpu/icache-place5\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("nops", gen(NOP, NOP, NOP, NOP, 1), 0);
    run("s3", gen(NOP, NOP, NOP, SW_T0, 1), 0);
    run("s23", gen(NOP, NOP, SW_T0, SW_T0, 1), 0);
    run("s123", gen(NOP, SW_T0, SW_T0, SW_T0, 1), 0);
    run("s3d", gen(ADDIU_A0, NOP, NOP, SW_T0, 1), 0);
    run("s23d", gen(ADDIU_A0, NOP, SW_T0, SW_T0, 1), 0);
    run("l2s3", gen(NOP, NOP, LW_T0, SW_T0, 1), 0);
    run("s2l3", gen(NOP, NOP, SW_T0, LW_T0, 1), 0);
    run("s3 every2", gen(NOP, NOP, NOP, SW_T0, 2), 0);
    run("s23 every2", gen(NOP, NOP, SW_T0, SW_T0, 2), 0);
    run("s3s0", gen_s3s0(), 0);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
