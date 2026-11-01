/*
 * cpu/loop-cold - a small loop entered cold, at every alignment: the taken branch out of a line still streaming, with a read or a store in the loop (2026-11-01)
 *
 * WHY. hwtests/pair-cold has no loop in it and hwtests/cold-branch no data access. ps1-tests' access-time is both, and it moved when v0.377.240 did: its first pass through the loop is a
 * cold line with a read in it, a taken branch out of that line, and the next read waiting on the first. Every counted loop a game runs starts like that.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc, generated code, every line cold: T2; a nops; L: nop; addiu t8, -1; X; bnez t8, L; nop; c nops; T2. X one of nop, lw A,
 * lbu A, lhu I_STAT, lhu SPUSTAT, sw A, sw scratchpad. a 0..7, n (the times round) 1 2 3 4 5 8 32 100, c 0..3: 2,240 cells, the fastest of 6 trials a few cycles apart.
 *
 * READ IT LIKE THIS. L X n opens a block; a line: A a, then the cells at c = 0..3. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js loop-cold --frames=30000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define KINDS 7
#define AS 8
#define CS 4
#define TRIALS 6

extern "C" {
uint32_t paSandbox[0x2200] __attribute__((aligned(1024)));
uint32_t paBuf[2048] __attribute__((aligned(4096)));
uint32_t paTrial(uint32_t* code, uint32_t n);
void paEvict(void);
}
#define paCode (paBuf + 0x100)                       /* cache index 400h */

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".balign 4096\n"
    ".global paEvict\n"
    "paEvict:\n"
    ".rept 1024\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 4096\n"
    ".global paTrial\n"
    "paTrial:\n"
    "addiu $sp, $sp, -24\n"
    "sw $ra, 0($sp)\n"
    "sw $s0, 4($sp)\n"
    "sw $s1, 8($sp)\n"
    "sw $s2, 12($sp)\n"
    "lui $s0, %hi(paSandbox)\n"
    "addiu $s0, $s0, %lo(paSandbox)\n"
    "lui $s1, 0x1F80\n"
    "lui $s2, 0x1F80\n"
    "ori $s2, $s2, 0x1000\n"
    "li $t0, 1\n"
    "li $t7, 32\n"
    "2:\n"
    "bnez $t7, 2b\n"
    "addiu $t7, $t7, -1\n"
    "move $t8, $a1\n"
    ".rept 16\n"
    "nop\n"
    ".endr\n"
    "jalr $a0\n"
    "nop\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "lw $ra, 0($sp)\n"
    "lw $s0, 4($sp)\n"
    "lw $s1, 8($sp)\n"
    "lw $s2, 12($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 24\n"
    ".set pop\n"
);

static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
#define S0 16u
#define S1 17u
#define S2 18u
#define T0 8u
#define T8 24u
static uint32_t kind(int k) {
    switch (k) {
        case 1: return I(0x23, S0, 12, 0x080);      /* lw A */
        case 2: return I(0x24, S0, 12, 0x081);      /* lbu A */
        case 3: return I(0x25, S2, 12, 0x070);      /* lhu I_STAT */
        case 4: return I(0x25, S2, 12, 0xDAE);      /* lhu SPUSTAT */
        case 5: return I(0x2B, S0, T0, 0x040);      /* sw A */
        case 6: return I(0x2B, S1, T0, 0x040);      /* sw scratchpad */
    }
    return 0;
}
static void build(int x, int a, int c) {
    int n = 0;
    uint32_t* p = paCode;
    p[n++] = I(0x25, S2, 2, 0x120);                 /* lhu v0, T2 */
    for (int i = 0; i < a; i++) p[n++] = 0;
    const int L = n;
    p[n++] = 0;
    p[n++] = I(0x09, T8, T8, 0xFFFF);               /* addiu t8, t8, -1 */
    p[n++] = kind(x);
    p[n] = 0x1700FFFFu & 0xFFFF0000u; p[n] |= (uint32_t)(L - (n + 1)) & 0xFFFF; n++;   /* bnez t8, L */
    p[n++] = 0;
    for (int i = 0; i < c; i++) p[n++] = 0;
    p[n++] = I(0x25, S2, 3, 0x120);                 /* lhu v1, T2 */
    p[n++] = 0;
    p[n++] = 0x03E00008u;                           /* jr ra */
    p[n++] = 0;
}
static uint32_t cell(uint32_t iters) {
    uint32_t m = 0xFFFF;
    for (int t = 0; t < TRIALS; t++) { paEvict(); const uint32_t v = paTrial(paCode, iters); if (v < m) m = v; for (volatile int i = 0; i < t * 3; i++) {} }
    return m;
}

int main() {
    static const uint16_t NS[8] = { 1, 2, 3, 4, 5, 8, 32, 100 };
    printf("\ncpu/loop-cold\n");
    printf("T2; a nops; L: nop; addiu; X; bnez L; nop; c nops; T2, cold, fastest of %d. L X n, then A a and c = 0..%d. No assertions.\n\n", TRIALS, CS - 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    static uint16_t row[CS];
    for (int x = 0; x < KINDS; x++)
        for (int k = 0; k < 8; k++) {
            printf("L %d %u\n", x, NS[k]);
            for (int a = 0; a < AS; a++) {
                for (int c = 0; c < CS; c++) { build(x, a, c); row[c] = (uint16_t)cell(NS[k]); }
                printf("A %d", a);
                for (int c = 0; c < CS; c++) printf(" %u", row[c]);
                printf("\n");
            }
        }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/loop-cold: Done.\n");
    for (;;) VSync(0);
}
