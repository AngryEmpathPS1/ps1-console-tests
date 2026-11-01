/*
 * cpu/mul-grid - what a multiply takes, by both operands (2026-11-01)
 *
 * WHY. hwtests/fuzz-cpu's misses regress onto multu then mflo: this core 2.3 cycles too slow at distance 1, 1.4 at 2, 1.3 at 3 (STATUS "fuzz-cpu"). This core
 * takes 7 / 10 / 14 by rs alone, as mednafen does, at thresholds 800h and 100000h, with only tiny operands ever timed here (cpu-extras' 3 x 3, load-pairs' 5A5A5A5Ah).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc, warm: `lhu T2; mult; mflo; lhu T2`, the fastest of 8, for every pair of 13 values as rs and rt, signed and
 * unsigned. Then the same at K = 1, 2, 3 nops between them with rt = 1. Printed less the block with two nops in their place, so a multiply that is done reads 0.
 *
 * READ IT LIKE THIS. A row whose cells do not change along rt says rs decides; a step along rt says rt does too. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js mul-grid --frames=1500 --quiet=800`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define N 13

#define BLOCK(name, ins)                                    \
    "." "balign 64\n"                                       \
    ".global " name "\n" name ":\n"                         \
    "lui $t0, 0x1F80\n"                                     \
    "ori $t0, $t0, 0x1120\n"                                \
    "nop\n"                                                 \
    "lhu $v0, 0($t0)\n"                                     \
    "nop\n"                                                 \
    ins                                                     \
    "lhu $v1, 0($t0)\n"                                     \
    "nop\n"                                                 \
    "subu $v0, $v1, $v0\n"                                  \
    "jr $ra\n"                                              \
    "andi $v0, $v0, 0xFFFF\n"

extern "C" {
uint32_t mgBase(uint32_t, uint32_t);
uint32_t mgM0(uint32_t, uint32_t); uint32_t mgM1(uint32_t, uint32_t); uint32_t mgM2(uint32_t, uint32_t); uint32_t mgM3(uint32_t, uint32_t);
uint32_t mgU0(uint32_t, uint32_t); uint32_t mgU1(uint32_t, uint32_t); uint32_t mgU2(uint32_t, uint32_t); uint32_t mgU3(uint32_t, uint32_t);
}
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    BLOCK("mgBase", "nop\nnop\n")
    BLOCK("mgM0", "mult $a0, $a1\nmflo $v1\n")
    BLOCK("mgM1", "mult $a0, $a1\nnop\nmflo $v1\n")
    BLOCK("mgM2", "mult $a0, $a1\nnop\nnop\nmflo $v1\n")
    BLOCK("mgM3", "mult $a0, $a1\nnop\nnop\nnop\nmflo $v1\n")
    BLOCK("mgU0", "multu $a0, $a1\nmflo $v1\n")
    BLOCK("mgU1", "multu $a0, $a1\nnop\nmflo $v1\n")
    BLOCK("mgU2", "multu $a0, $a1\nnop\nnop\nmflo $v1\n")
    BLOCK("mgU3", "multu $a0, $a1\nnop\nnop\nnop\nmflo $v1\n")
    ".set pop\n"
);

static const uint32_t V[N] = { 0, 1, 0x7FF, 0x800, 0xFFFFF, 0x100000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0xFFFFF800u, 0xFFFFF7FFu, 0xFFF00000u, 0xFFEFFFFFu };
typedef uint32_t (*Fn)(uint32_t, uint32_t);
static uint32_t best(Fn f, uint32_t a, uint32_t b) { uint32_t m = 0xFFFF; for (int i = 0; i < 8; i++) { const uint32_t t = f(a, b); if (i && t < m) m = t; } return m; }

int main() {
    printf("\ncpu/mul-grid\n");
    printf("mult and multu then mflo, warm, fastest of 8, less the block with nops. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t base = best(mgBase, 0, 0);
    printf("base %u\n", base);
    for (int s = 0; s < 2; s++) {
        printf("\n%s, K = 0: rows rs, columns rt:\n         ", s ? "multu" : "mult");
        for (int c = 0; c < N; c++) printf(" %08x", V[c]);
        printf("\n");
        for (int r = 0; r < N; r++) {
            printf("%08x:", V[r]);
            for (int c = 0; c < N; c++) printf(" %8d", (int)best(s ? mgU0 : mgM0, V[r], V[c]) - (int)base);
            printf("\n");
        }
    }
    printf("\nK = 0 1 2 3 nops, rt = 1: rs, mult, multu:\n");
    static const Fn FM[4] = { mgM0, mgM1, mgM2, mgM3 }, FU[4] = { mgU0, mgU1, mgU2, mgU3 };
    for (int r = 0; r < N; r++) {
        printf("%08x:", V[r]);
        for (int k = 0; k < 4; k++) printf(" %3d", (int)best(FM[k], V[r], 1) - (int)base);
        printf("  ");
        for (int k = 0; k < 4; k++) printf(" %3d", (int)best(FU[k], V[r], 1) - (int)base);
        printf("\n");
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/mul-grid: Done.\n");
    for (;;) VSync(0);
}
