/*
 * cpu/fuzz-cpu - random programs, timed: the same 4,096 on every console and every emulator (2026-11-01)
 *
 * WHY. Every test here is something a person thought of. Today's three console maps moved more than a month of fitting, and the losses beside them
 * sit in combinations nobody wrote: an on-die read behind a store chain, a multiply beside a load. So let the machine choose. No judgement goes
 * into what is tested, only into what is safe, and a console answers it whole. As far as a search could find nobody has done this on a PlayStation.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc. Program i is 16 instructions drawn from xorshift32 seeded by i, so the same id is
 * the same program anywhere: nops, ALU, RAM loads and stores of every width in two 1 KB pages 32 KB apart, the scratchpad, mult, div, mfhi,
 * mflo, and reads into r0 of registers that cannot do anything (I_MASK, DPCR, GPUSTAT, JOY_STAT, SPUSTAT, Timer 1). No jumps, no hardware
 * writes, nothing that can hang: a program cannot leave its 32 instructions of RAM. A load's delay slot is left to chance, on purpose.
 *   COLD  the whole instruction cache evicted by a 4 KB sled, then the program, 4 trials at different start phases.
 *   WARM  the program run once, then again, timed, 4 trials.
 * Each line: the id, cold fastest and slowest, warm fastest and slowest, and a hash of t0-t7 and the 64 + 64 sandbox words after the last run. A cost is
 * the block between two reads of Timer 2, so it holds a jalr and a jr; it is not the program alone.
 *
 * READ IT LIKE THIS. fastest = no refresh met; slowest = the worst of four phases, a sample, not the spread (rule 3). The hash says the values are right, so a
 * wrong load delay shows. experiments/scratch_fuzzcmp.js compares two logs and prints the worst programs, disassembled.
 * There are no assertions - compare against your emulator. Any emulator can: run this, diff the lines.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-cpu --frames=4000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define PROGRAMS 4096
#define LEN 16
#define TRIALS 4
#define WORDS 64

extern "C" {
uint32_t fzSandbox[0x2100] __attribute__((aligned(1024)));   /* s0 at 0, s3 32 KB on */
uint32_t fzCode[32] __attribute__((aligned(16)));
uint32_t fzRegs[8];
uint32_t fzAfter[8];
uint32_t fzTime(uint32_t* code, uint32_t* regs);
void fzEvict(void);
}

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".balign 4096\n"
    ".global fzEvict\n"
    "fzEvict:\n"
    ".rept 1024\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 64\n"
    ".global fzTime\n"
    "fzTime:\n"
    "addiu $sp, $sp, -24\n"
    "sw $ra, 0($sp)\n"
    "sw $s0, 4($sp)\n"
    "sw $s1, 8($sp)\n"
    "sw $s2, 12($sp)\n"
    "sw $s3, 16($sp)\n"
    "lw $t0, 0($a1)\n"
    "lw $t1, 4($a1)\n"
    "lw $t2, 8($a1)\n"
    "lw $t3, 12($a1)\n"
    "lw $t4, 16($a1)\n"
    "lw $t5, 20($a1)\n"
    "lw $t6, 24($a1)\n"
    "lw $t7, 28($a1)\n"
    "lui $s0, %hi(fzSandbox)\n"
    "addiu $s0, $s0, %lo(fzSandbox)\n"
    "lui $s3, 1\n"
    "ori $s3, $s3, 0\n"
    "addiu $s3, $s3, -32768\n"
    "addu $s3, $s3, $s0\n"
    "lui $s1, 0x1F80\n"
    "lui $s2, 0x1F80\n"
    "ori $s2, $s2, 0x1000\n"
    "nop\n"
    "lhu $v0, 0x120($s2)\n"
    "nop\n"
    "jalr $a0\n"
    "nop\n"
    "lhu $v1, 0x120($s2)\n"
    "nop\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "lui $at, %hi(fzAfter)\n"
    "addiu $at, $at, %lo(fzAfter)\n"
    "sw $t0, 0($at)\n"
    "sw $t1, 4($at)\n"
    "sw $t2, 8($at)\n"
    "sw $t3, 12($at)\n"
    "sw $t4, 16($at)\n"
    "sw $t5, 20($at)\n"
    "sw $t6, 24($at)\n"
    "sw $t7, 28($at)\n"
    "lw $ra, 0($sp)\n"
    "lw $s0, 4($sp)\n"
    "lw $s1, 8($sp)\n"
    "lw $s2, 12($sp)\n"
    "lw $s3, 16($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 24\n"
    ".set pop\n"
);

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t R(uint32_t fn, uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa) { return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn; }
static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
#define T() (8u + rnd() % 8u)
#define S0 16u
#define S1 17u
#define S2 18u
#define S3 19u

static const uint16_t IOW[4] = { 0x074, 0x0F0, 0x814, 0x044 };   /* I_MASK, DPCR, GPUSTAT, JOY_STAT: lw into r0 */

static uint32_t gen1() {
    const uint32_t k = rnd() % 100;
    const uint32_t base = (rnd() & 1) ? S0 : S3;
    if (k < 20) return 0;                                                       /* nop */
    if (k < 30) return R(0x21, T(), T(), T(), 0);                               /* addu */
    if (k < 36) return R(0x26, T(), T(), T(), 0);                               /* xor */
    if (k < 40) return R(0x00, 0, T(), T(), rnd() % 32);                        /* sll */
    if (k < 44) return I(0x0D, T(), T(), rnd());                                /* ori */
    if (k < 52) return I(0x23, base, T(), (rnd() % WORDS) * 4);                 /* lw */
    if (k < 56) return I(0x25, base, T(), (rnd() % (WORDS * 2)) * 2);           /* lhu */
    if (k < 59) return I(0x21, base, T(), (rnd() % (WORDS * 2)) * 2);           /* lh */
    if (k < 62) return I(0x24, base, T(), rnd() % (WORDS * 4));                 /* lbu */
    if (k < 64) return I(0x20, base, T(), rnd() % (WORDS * 4));                 /* lb */
    if (k < 71) return I(0x2B, base, T(), (rnd() % WORDS) * 4);                 /* sw */
    if (k < 74) return I(0x29, base, T(), (rnd() % (WORDS * 2)) * 2);           /* sh */
    if (k < 76) return I(0x28, base, T(), rnd() % (WORDS * 4));                 /* sb */
    if (k < 79) return I(0x23, S1, T(), (rnd() % WORDS) * 4);                   /* lw scratchpad */
    if (k < 81) return I(0x2B, S1, T(), (rnd() % WORDS) * 4);                   /* sw scratchpad */
    if (k < 87) return I(0x23, S2, 0, IOW[rnd() % 4]);                          /* lw register, into r0 */
    if (k < 90) return I(0x25, S2, 0, 0x110);                                   /* lhu Timer 1, into r0 */
    if (k < 92) return I(0x25, S2, 0, 0xDAE);                                   /* lhu SPUSTAT, into r0 */
    if (k < 95) return R(0x19, T(), T(), 0, 0);                                 /* multu */
    if (k < 96) return R(0x1B, T(), T(), 0, 0);                                 /* divu */
    if (k < 98) return R(0x10, 0, 0, T(), 0);                                   /* mfhi */
    return R(0x12, 0, 0, T(), 0);                                               /* mflo */
}

static uint32_t master[WORDS * 2];
static void gen(int id) {
    rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
    for (int i = 0; i < 8; i++) fzRegs[i] = rnd();
    for (int i = 0; i < WORDS * 2; i++) master[i] = rnd();
    for (int i = 0; i < LEN; i++) fzCode[i] = gen1();
    fzCode[LEN] = 0x03E00008;                                                   /* jr ra */
    fzCode[LEN + 1] = 0;
}
static void sandboxIn() {
    for (int i = 0; i < WORDS; i++) { fzSandbox[i] = master[i]; fzSandbox[0x2000 + i] = master[WORDS + i]; }
    volatile uint32_t* sp = (volatile uint32_t*)0x1F800000u;
    for (int i = 0; i < WORDS; i++) sp[i] = master[i] ^ 0x5A5A5A5Au;
}
static uint32_t hash() {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 8; i++) h = (h ^ fzAfter[i]) * 16777619u;
    for (int i = 0; i < WORDS; i++) { h = (h ^ fzSandbox[i]) * 16777619u; h = (h ^ fzSandbox[0x2000 + i]) * 16777619u; }
    volatile uint32_t* sp = (volatile uint32_t*)0x1F800000u;
    for (int i = 0; i < WORDS; i++) h = (h ^ sp[i]) * 16777619u;
    return h;
}
static void spin(int n) { for (volatile int i = 0; i < n; i++) {} }

int main() {
    printf("\ncpu/fuzz-cpu\n");
    printf("%d random programs of %d instructions. id, cold fastest slowest, warm fastest slowest, hash. No assertions.\n\n", PROGRAMS, LEN);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int id = 0; id < PROGRAMS; id++) {
        gen(id);
        uint32_t cmin = 0xFFFF, cmax = 0, wmin = 0xFFFF, wmax = 0;
        for (int t = 0; t < TRIALS; t++) {
            spin(id % 7 + t * 11);
            sandboxIn();
            fzEvict();
            const uint32_t c = fzTime(fzCode, fzRegs);
            if (c < cmin) cmin = c;
            if (c > cmax) cmax = c;
        }
        for (int t = 0; t < TRIALS; t++) {
            spin(id % 5 + t * 13);
            sandboxIn();
            fzTime(fzCode, fzRegs);
            sandboxIn();
            const uint32_t w = fzTime(fzCode, fzRegs);
            if (w < wmin) wmin = w;
            if (w > wmax) wmax = w;
        }
        printf("P %d %u %u %u %u %08x\n", id, cmin, cmax, wmin, wmax, hash());
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/fuzz-cpu: Done.\n");
    for (;;) VSync(0);
}
