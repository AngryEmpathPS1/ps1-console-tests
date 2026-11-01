/*
 * cpu/fuzz-trail - fuzz-cpu's programs a step at a time: where a program's time goes (2026-11-01)
 *
 * WHY. On working out why a program is wrong: "Could you specifically write the test to show how it got to where it is, so you'd have an exact trail to follow?" A
 * timer read in mid-program disturbs what it times. So run the same program stopped after 0, 1, 2 .. 16 instructions: each run is undisturbed up to its stop, and the rungs
 * are its trail. Laid beside an emulator's, the first rung that differs names the instruction, and all before it are proven.
 *
 * WHAT IT DOES. As fuzz-cpu (same ids, same programs, same harness): interrupts off, Timer 2 at the system clock, no disc. For each of the first N programs, for each length
 * 0..16, warm then cold, the fastest of 4. A line: T id, the 17 warm rungs, the 17 cold rungs.
 *
 * READ IT LIKE THIS. A rung is a block ending in the jr, so it is a program in its own right: step k's cost is rung k - rung k-1 with whatever a stop there changed.
 * experiments/scratch_trail.js lays two logs side by side and counts, by the instruction and its neighbours, where they first part.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-trail --frames=9000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define PROGRAMS 1024
#define LEN 16
#define TRIALS 4
#define WORDS 64

extern "C" {
uint32_t fzSandbox[0x2100] __attribute__((aligned(1024)));
uint32_t fzCode[32] __attribute__((aligned(16)));
uint32_t fzRegs[8];
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
static const uint16_t IOW[4] = { 0x074, 0x0F0, 0x814, 0x044 };

static uint32_t gen1() {
    const uint32_t k = rnd() % 100;
    const uint32_t base = (rnd() & 1) ? S0 : S3;
    if (k < 20) return 0;
    if (k < 30) return R(0x21, T(), T(), T(), 0);
    if (k < 36) return R(0x26, T(), T(), T(), 0);
    if (k < 40) return R(0x00, 0, T(), T(), rnd() % 32);
    if (k < 44) return I(0x0D, T(), T(), rnd());
    if (k < 52) return I(0x23, base, T(), (rnd() % WORDS) * 4);
    if (k < 56) return I(0x25, base, T(), (rnd() % (WORDS * 2)) * 2);
    if (k < 59) return I(0x21, base, T(), (rnd() % (WORDS * 2)) * 2);
    if (k < 62) return I(0x24, base, T(), rnd() % (WORDS * 4));
    if (k < 64) return I(0x20, base, T(), rnd() % (WORDS * 4));
    if (k < 71) return I(0x2B, base, T(), (rnd() % WORDS) * 4);
    if (k < 74) return I(0x29, base, T(), (rnd() % (WORDS * 2)) * 2);
    if (k < 76) return I(0x28, base, T(), rnd() % (WORDS * 4));
    if (k < 79) return I(0x23, S1, T(), (rnd() % WORDS) * 4);
    if (k < 81) return I(0x2B, S1, T(), (rnd() % WORDS) * 4);
    if (k < 87) return I(0x23, S2, 0, IOW[rnd() % 4]);
    if (k < 90) return I(0x25, S2, 0, 0x110);
    if (k < 92) return I(0x25, S2, 0, 0xDAE);
    if (k < 95) return R(0x19, T(), T(), 0, 0);
    if (k < 96) return R(0x1B, T(), T(), 0, 0);
    if (k < 98) return R(0x10, 0, 0, T(), 0);
    return R(0x12, 0, 0, T(), 0);
}

static uint32_t full[LEN], master[WORDS * 2];
static void gen(int id) {
    rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
    for (int i = 0; i < 8; i++) fzRegs[i] = rnd();
    for (int i = 0; i < WORDS * 2; i++) master[i] = rnd();
    for (int i = 0; i < LEN; i++) full[i] = gen1();
}
static void cut(int n) {
    for (int i = 0; i < n; i++) fzCode[i] = full[i];
    fzCode[n] = 0x03E00008;
    fzCode[n + 1] = 0;
}
static void sandboxIn() {
    for (int i = 0; i < WORDS; i++) { fzSandbox[i] = master[i]; fzSandbox[0x2000 + i] = master[WORDS + i]; }
    volatile uint32_t* sp = (volatile uint32_t*)0x1F800000u;
    for (int i = 0; i < WORDS; i++) sp[i] = master[i] ^ 0x5A5A5A5Au;
}
static void spin(int n) { for (volatile int i = 0; i < n; i++) {} }
static uint16_t wr[LEN + 1], cr[LEN + 1];

int main() {
    printf("\ncpu/fuzz-trail\n");
    printf("fuzz-cpu's first %d programs stopped after 0..%d instructions: id, warm rungs, cold rungs. No assertions.\n\n", PROGRAMS, LEN);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int id = 0; id < PROGRAMS; id++) {
        gen(id);
        for (int n = 0; n <= LEN; n++) {
            cut(n);
            uint32_t wm = 0xFFFF, cm = 0xFFFF;
            for (int t = 0; t < TRIALS; t++) {
                spin(id % 5 + t * 13);
                sandboxIn(); fzEvict();                          /* the new code is in no line, whatever stood there */
                sandboxIn();
                fzTime(fzCode, fzRegs);
                sandboxIn();
                const uint32_t w = fzTime(fzCode, fzRegs);
                if (w < wm) wm = w;
            }
            for (int t = 0; t < TRIALS; t++) {
                spin(id % 7 + t * 11);
                sandboxIn();
                fzEvict();
                const uint32_t c = fzTime(fzCode, fzRegs);
                if (c < cm) cm = c;
            }
            wr[n] = (uint16_t)wm; cr[n] = (uint16_t)cm;
        }
        printf("T %d", id);
        for (int n = 0; n <= LEN; n++) printf(" %u", wr[n]);
        printf(" |");
        for (int n = 0; n <= LEN; n++) printf(" %u", cr[n]);
        printf("\n");
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/fuzz-trail: Done.\n");
    for (;;) VSync(0);
}
