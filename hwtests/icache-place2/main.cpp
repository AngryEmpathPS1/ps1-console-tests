/*
 * cpu/icache-place2 - an instruction-cache miss at a JUMP's target by what sits in the jump's delay slot: nothing, a RAM
 * store, a RAM load; the target entered at word 0, 1, 2 or 3 of its line.
 *
 * WHY (2026-11-13). hwtests/icache-place (6bd79e93) found one layout off on the second SCPH-5501 and the PSone: a RAM load
 * at a line's last word with the next line cold, +3.0 cycles a line. Fixing it here does not move PS1-DOS's pmsplit gap
 * (its -O2 386 core +0.63% on silicon in every phase, -Os -0.12%). Fitting that gap phase by phase against this
 * emulator's miss counts, the term that carries it is a miss at word 3 - a jump's target - whose previous instruction
 * was a store: the -O2 build has 230,371 of them, the -Os build 3,502, and that count alone fits the 28 phase rows to
 * an RMS of 110k cycles of 214k. icache-place's stores sat before a fall-through, never in a jump's delay slot.
 *
 * WHAT IT DOES. Interrupts off; 128 cache lines of generated code at 80030000h, cold (FlushCache, then the call) and
 * warm (again at once), 16 times, min and max, Timer 2 (system clock) around the jalr.
 *   K=0..2 / slot X   line i entered at word K: nops to word 1, `j` at word 2 to line i+1 word K, X at word 3 (its slot)
 *   K=3 / slot X      line 2i entered at word 3: `j` there to line 2i+2 word 3; its slot X is line 2i+1's word 0
 *   X: nop, sw t0,0(a0), lw t0,0(a0) - a0 a fixed RAM word at 800C0000h
 * READ IT LIKE THIS. cold - warm over 128 lines (64 pairs for K=3) is a miss's cost there; the nop rows are icache-place's
 * jK s=16 rows again (the same layout), the sw / lw rows what the slot adds.
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

static uint32_t gen(uint32_t K, uint32_t X) {
    clear();
    if (K < 3) {
        for (uint32_t i = 0; i < LINES; i++) {
            uint32_t L = BASE + i * 16;
            wr(L + 8, i + 1 < LINES ? J(BASE + (i + 1) * 16 + K * 4) : JR_RA);
            wr(L + 12, X);
        }
        return BASE + K * 4;
    }
    for (uint32_t i = 0; i < LINES; i += 2) {
        uint32_t L = BASE + i * 16;
        wr(L + 12, i + 2 < LINES ? J(BASE + (i + 2) * 16 + 12) : JR_RA);
        wr(L + 16, X);
    }
    return BASE + 12;
}

static void run(const char *name, uint32_t fn) {
    uint32_t cmin = ~0u, cmax = 0, wmin = ~0u, wmax = 0;
    for (int r = 0; r < 17; r++) {
        ip_flush();
        uint32_t c = ip_call(fn, DATA, 4);
        uint32_t w = ip_call(fn, DATA, 4);
        I_MASK = 0;
        if (r == 0) continue;
        if (c < cmin) cmin = c; if (c > cmax) cmax = c;
        if (w < wmin) wmin = w; if (w > wmax) wmax = w;
    }
    printf("%-14s cold min %u max %u  warm min %u max %u\n", name, cmin, cmax, wmin, wmax);
}

int main() {
    printf("\ncpu/icache-place2\n");
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    T2_MODE = 0x0000;
    static const uint32_t XS[3] = { NOP, SW_T0, LW_T0 };
    static const char *const XN[3] = { "nop", "sw", "lw" };
    for (uint32_t K = 0; K < 4; K++) for (int x = 0; x < 3; x++) {
        char nm[16]; nm[0] = 'K'; nm[1] = (char)('0' + K); nm[2] = ' '; int i = 0; while (XN[x][i]) { nm[3 + i] = XN[x][i]; i++; } nm[3 + i] = 0;
        run(nm, gen(K, XS[x]));
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
