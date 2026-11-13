/*
 * cpu/icache-place14 - a cache line whose tag is there but whose first words are not: entered at word 2, later at word 0.
 *
 * WHY (2026-11-13; STATUS "The PSone back"). PS1-DOS's faster 386 core (pmsplit-sp) runs +2.16 % on the consoles against this core,
 * and every pattern in its hot lines matches alone (icache-place11 .. -place13). Its register phase is periodic (64,639 instructions,
 * 158 distinct lines - it fits the 4 KB cache: its misses are conflicts, experiments/pm386_pathtrace.js), and conflicts make lines that
 * are entered mid-line - only their tail filled - and later entered at an earlier word with their tag still there, a refill no placement
 * test has made. Here 32 cold lines L_i = [nop ; nop ; jr t2 ; nop] are each visited twice through helper lines (two cache lines each,
 * on other cache indices: lui/ori t1 = the entry, lui/ori t2 = the next helper, jr t1):
 *   TM     first at L_i + 8 (words 2-3 filled), then at L_i + 0 (the tag matches, words 0-1 invalid)
 *   FULL   at L_i + 0, then at L_i + 0 again (the second a hit)          PART  at L_i + 8 twice
 * READ IT LIKE THIS. cold TM - cold PART is the refill at word 0 of a line whose tail is in; against FULL - PART, a fresh fill.
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
#define NL 32
#define HBASE (BASE + 0x200)          /* helpers: cache indices 32 .. 159, the L lines 0 .. 31 */
static void lui_ori(volatile uint32_t *w, uint32_t r, uint32_t v) { w[0] = 0x3C000000u | (r << 16) | (v >> 16); w[1] = 0x34000000u | (r << 21) | (r << 16) | (v & 0xFFFF); }
static void helper(uint32_t at, uint32_t to, uint32_t next) {
    volatile uint32_t *w = CODE + ((at - BASE) >> 2);
    lui_ori(w, 9, to); lui_ori(w + 2, 10, next); w[4] = 0x01200008u; w[5] = NOP; w[6] = NOP; w[7] = NOP;   /* t1 = to, t2 = next, jr t1 */
}
static uint32_t gen(uint32_t e1, uint32_t e2) {
    for (uint32_t i = 0; i < 0xC00 / 4; i++) CODE[i] = NOP;
    for (uint32_t i = 0; i < NL; i++) { volatile uint32_t *L = CODE + i * 4; L[0] = NOP; L[1] = NOP; L[2] = 0x01400008u; L[3] = NOP; }   /* jr t2 */
    const uint32_t EXIT = HBASE + NL * 64;
    CODE[(EXIT - BASE) >> 2] = 0x03200008u; CODE[((EXIT - BASE) >> 2) + 1] = NOP;   /* jr t9 */
    for (uint32_t i = 0; i < NL; i++) {
        const uint32_t A = HBASE + i * 64, B = A + 32, nextA = i + 1 < NL ? A + 64 : EXIT;
        helper(A, BASE + i * 16 + e1, B);
        helper(B, BASE + i * 16 + e2, nextA);
    }
    /* the entry, after EXIT: t9 = ra (EXIT returns through it), then the first helper */
    volatile uint32_t *E = CODE + ((EXIT + 16 - BASE) >> 2);
    E[0] = 0x03E0C821u; lui_ori(E + 1, 9, HBASE); E[3] = 0x01200008u; E[4] = NOP;   /* addu t9,ra,zero ; t1 = HBASE ; jr t1 */
    return EXIT + 16;
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
    printf("\ncpu/icache-place14\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    run("TM", gen(8, 0), DATA);
    run("FULL", gen(0, 0), DATA);
    run("PART", gen(8, 8), DATA);
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
