/*
 * cpu/pair-warm - every two instructions at every spacing, warm: a table any emulator can be laid beside (2026-11-01)
 *
 * WHY. The question put: "If every emulator developer could have their ultimate dream tests.. what would they be". hwtests/fuzz-trail says what is left of the CPU's timing is how
 * neighbours touch. Random programs find such things and cannot tell them apart, so all of them, in a grid: T2 read; a nops; X; b nops; Y; c nops; T2 read. Each cell is one
 * question and none is left out by chance. hwtests/pair-atlas was the first of these, and C code sharing the block's cache index left its first line cold in every trial: its
 * log is that case. Here the trials run in the asm below, at index 0 of the cache, the block at index 400h, and no other code runs between them. pair-cold is the same grid
 * with every line cold.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc, generated code. X and Y each one of 16 kinds: nop, sw A, sw A+4, sw A+400h, sw B (32 KB on, another DRAM
 * page), sb A, sw scratchpad, sw Timer 0's count, lw A, lw B, lbu A, lw scratchpad, lhu I_STAT, lhu SPUSTAT, mult, mflo. a 0..6, b 0..4, c 0..6: 62,720 cells. A cell is the
 * bracket's fastest of 6 trials a few cycles apart. Nothing depends on anything: X loads into t4, Y into t5, the multiply is 1 x 1. nop / nop is the base.
 *
 * READ IT LIKE THIS. P X Y opens a pair; a line: A a b, then the cells at c = 0..6. What a pair costs beyond its two instructions is what they did to each other, or to the
 * bracket's own reads. experiments/scratch_atlas.js lays two logs side by side and ranks the pairs. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js pair-warm --frames=30000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define KINDS 16
#define AS 7
#define BS 5
#define CS 7
#define TRIALS 6
#ifndef PA_COLD
#define PA_COLD 0
#endif
#ifndef PA_DEP
#define PA_DEP 0
#endif
#ifndef PA_UNC
#define PA_UNC 0
#endif
#define PA_NAME (PA_UNC ? "pair-unc" : PA_DEP ? (PA_COLD ? "pair-cdep" : "pair-dep") : PA_COLD ? "pair-cold" : "pair-warm")

extern "C" {
uint32_t paSandbox[0x2200] __attribute__((aligned(1024)));
uint32_t paBuf[2048] __attribute__((aligned(4096)));
uint32_t paTrials(uint32_t* code, uint32_t trials);
void paEvict(void);
void paCop2(void);
}
#define paCode (paBuf + 0x100)                       /* cache index 400h */

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".balign 4096\n"
    ".global paCop2\n"
    "paCop2:\n"
    "mfc0 $t0, $12\n"
    "lui $t1, 0x4000\n"
    "or $t0, $t0, $t1\n"
    "mtc0 $t0, $12\n"
    "nop\n"
    "jr $ra\n"
    "nop\n"
    ".balign 4096\n"
    ".global paEvict\n"
    "paEvict:\n"
    ".rept 1024\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 4096\n"
    ".global paTrials\n"
    "paTrials:\n"
    "addiu $sp, $sp, -24\n"
    "sw $ra, 0($sp)\n"
    "sw $s0, 4($sp)\n"
    "sw $s1, 8($sp)\n"
    "sw $s2, 12($sp)\n"
    "sw $s3, 16($sp)\n"
    "lui $s0, %hi(paSandbox)\n"
    "addiu $s0, $s0, %lo(paSandbox)\n"
    "lui $s3, 1\n"
    "addiu $s3, $s3, -32768\n"
    "addu $s3, $s3, $s0\n"
    "lui $s1, 0x1F80\n"
    "lui $s2, 0x1F80\n"
    "ori $s2, $s2, 0x1000\n"
    "li $t0, 1\n"
    "li $t1, 1\n"
    "move $t6, $a1\n"
    "li $t7, 0xFFFF\n"
#if !PA_COLD
    ".rept 16\n"
    "nop\n"
    ".endr\n"
    "jalr $a0\n"                                     /* the warm-up, not counted */
    "nop\n"
#endif
    "1:\n"
    "sll $t8, $t6, 2\n"
    "2:\n"
    "bnez $t8, 2b\n"
    "addiu $t8, $t8, -1\n"
    ".rept 16\n"
    "nop\n"
    ".endr\n"
    "jalr $a0\n"
    "nop\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "sltu $at, $v0, $t7\n"
    "beq $at, $0, 3f\n"
    "nop\n"
    "move $t7, $v0\n"
    "3:\n"
    "addiu $t6, $t6, -1\n"
    "bnez $t6, 1b\n"
    "nop\n"
    "move $v0, $t7\n"
    "lw $ra, 0($sp)\n"
    "lw $s0, 4($sp)\n"
    "lw $s1, 8($sp)\n"
    "lw $s2, 12($sp)\n"
    "lw $s3, 16($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 24\n"
    ".set pop\n"
);

static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
#define S0 16u
#define S1 17u
#define S2 18u
#define S3 19u
#define T0 8u
static uint32_t kind(int k, uint32_t rt) {
#if PA_DEP
    switch (k) {
        case 1:  return 0x018C6821u;                /* addu t5, t4, t4 */
        case 2:  return I(0x23, S0, rt, 0x080);
        case 3:  return I(0x25, S2, rt, 0x070);
        case 4:  return I(0x25, S2, rt, 0xDAE);
        case 5:  return I(0x23, S1, rt, 0x080);
        case 6:  return I(0x2B, S0, rt, 0x040);     /* sw t4, A */
        case 7:  return 0x01090019u;                /* multu t0, t1 */
        case 8:  return 0x480CC800u;                /* mfc2 t4, $25 */
        case 9:  return 0x488C4800u;                /* mtc2 t4, $9 */
        case 10: return 0x4A000006u;                /* NCLIP */
        case 11: return 0x4A180001u;                /* RTPS */
        case 12: return I(0x32, S0, 9, 0x080);      /* lwc2 $9, A */
        case 13: return I(0x3A, S0, 25, 0x040);     /* swc2 $25, A */
        case 14: return 0x15000001u;                /* bne t0, zero: taken, to the word behind its delay slot */
        case 15: return 0x400C6000u;                /* mfc0 t4, $12 */
    }
    return 0;
#endif
    switch (k) {
        case 1:  return I(0x2B, S0, T0, 0x040);
        case 2:  return I(0x2B, S0, T0, 0x044);
        case 3:  return I(0x2B, S0, T0, 0x440);
        case 4:  return I(0x2B, S3, T0, 0x040);
        case 5:  return I(0x28, S0, T0, 0x040);
        case 6:  return I(0x2B, S1, T0, 0x040);
        case 7:  return I(0x2B, S2, 0, 0x100);      /* Timer 0's count, cleared */
        case 8:  return I(0x23, S0, rt, 0x080);
        case 9:  return I(0x23, S3, rt, 0x080);
        case 10: return I(0x24, S0, rt, 0x081);
        case 11: return I(0x23, S1, rt, 0x080);
        case 12: return I(0x25, S2, rt, 0x070);
        case 13: return I(0x25, S2, rt, 0xDAE);
        case 14: return 0x01090018u;                /* mult t0, t1 */
        case 15: return 0x00006812u;                /* mflo t5 */
    }
    return 0;
}
static void build(int x, int y, int a, int b, int c) {
    int n = 0;
    uint32_t* p = paCode;
    p[n++] = I(0x25, S2, 2, 0x120);                 /* lhu v0, T2 */
    for (int i = 0; i < a; i++) p[n++] = 0;
    p[n++] = kind(x, 12);
    for (int i = 0; i < b; i++) p[n++] = 0;
    p[n++] = kind(y, PA_DEP ? 12 : 13);
    for (int i = 0; i < c; i++) p[n++] = 0;
    p[n++] = I(0x25, S2, 3, 0x120);                 /* lhu v1, T2 */
    p[n++] = 0;
    p[n++] = 0x03E00008u;                           /* jr ra */
    p[n++] = 0;
}
static uint32_t cell() {
#if PA_COLD
    uint32_t m = 0xFFFF;
    for (int t = 0; t < TRIALS; t++) { paEvict(); const uint32_t v = paTrials(paCode, t + 1 > 3 ? 1 : 1); if (v < m) m = v; for (volatile int i = 0; i < t * 3; i++) {} }
    return m;
#else
    paEvict();
    return paTrials(PA_UNC ? (uint32_t*)((uint32_t)paCode | 0xA0000000u) : paCode, TRIALS);
#endif
}

int main() {
    printf("\ncpu/%s\n", PA_NAME);
    printf("T2; a nops; X; b nops; Y; c nops; T2, %s, fastest of %d. P X Y, then A a b and c = 0..%d. No assertions.\n\n", PA_UNC ? "uncached" : PA_COLD ? "cold" : "warm", TRIALS, CS - 1);
    EnterCriticalSection();
    paCop2();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    static uint16_t row[CS];
    for (int x = 0; x < KINDS; x++)
        for (int y = 0; y < KINDS; y++) {
            printf("P %d %d\n", x, y);
            for (int a = 0; a < AS; a++)
                for (int b = 0; b < BS; b++) {
                    for (int c = 0; c < CS; c++) { build(x, y, a, b, c); row[c] = (uint16_t)cell(); }
                    printf("A %d %d", a, b);
                    for (int c = 0; c < CS; c++) printf(" %u", row[c]);
                    printf("\n");
                }
        }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/%s: Done.\n", PA_NAME);
    for (;;) VSync(0);
}
