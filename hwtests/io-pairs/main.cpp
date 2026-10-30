/*
 * cpu/io-pairs - what two register reads back to back cost, register by register.
 *
 * WHY (2026-10-08). ps1-tests' timers loop - delay(100), then `lhu count; lhu mode` of the timer, two ANDs, a branch, a
 * store of the count - steps its counter 123 a pass on the ps1-tests author's console AND on the SCPH-5501, 7501
 * and 9001 (every "Pause until Hblank... Free Run" / "Reset at Vblanks" row), 122 here. Its 115 instructions are
 * cached; the "1000 cycles delay" rows of the same program (two count reads around the same delay) are already exact.
 * So the cycle is in the read pair or what follows it. This core runs a second load into another register a cycle
 * sooner (M209's overlap: counter-bracket's empty bracket, two COUNT reads, is 4 on the consoles), whatever the
 * registers are - and a MODE read is not a count read (it clears the reached-flags, bits 11-12).
 *
 * WHAT IT DOES. Interrupts off, cached code. For each pair (A, B) of registers, 64 passes of 8 x `lhu t0, A; lhu t1,
 * B`, timed by Timer 2 at the system clock around the whole loop; the same loop with the 16 loads made nops is the
 * baseline. Printed: cycles per PAIR over the baseline, and per single `lhu A; nop` likewise. Registers: timer 2's
 * count, mode and target, timer 0's count and mode, I_STAT, I_MASK, DPCR, DICR, MADR2, GPUSTAT, JOY_STAT and MDEC's
 * status. Pairs: each with itself, count/mode both ways, count/target, mode/target, and timer 2 against the others.
 *
 * READ IT LIKE THIS. A pair that costs one more than its two singles say has no overlap; one that costs one more than
 * count/count has the MODE read's extra cycle. The timers loop needs exactly one of those somewhere.
 *
 * ON THIS CORE AT v0.377.41: every register alike - a single read 3.75 cycles over the nops, every pair 5.87 (the second
 * read overlapping the first, M209).
 *
 * THE RULE: run it here first: `node hwtests/run.js io-pairs`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define IO 0x1F800000u
struct Reg { const char* name; uint16_t off; };
static const Reg regs[] = {
    { "T2CNT", 0x1120 }, { "T2MODE", 0x1124 }, { "T2TGT", 0x1128 }, { "T0CNT", 0x1100 }, { "T0MODE", 0x1104 },
    { "ISTAT", 0x1070 }, { "IMASK", 0x1074 }, { "DPCR", 0x10F0 }, { "DICR", 0x10F4 }, { "MADR2", 0x10A0 },
    { "GPUSTAT", 0x1814 }, { "JOYSTAT", 0x1044 }, { "MDECST", 0x1824 },
};
#define NREG (int)(sizeof(regs) / sizeof(regs[0]))

static inline uint16_t t2() { return read16(0x1F801120); }

/* 64 passes of 8 pairs (or singles); the asm keeps every instruction as written. */
static uint32_t __attribute__((noinline, aligned(64))) runPairs(uint32_t a, uint32_t b) {
    const uint16_t t0 = t2();
    __asm__ volatile(".set noreorder\n li $t4, 64\n 1:\n"
                     ".rept 8\n lhu $t0, 0(%0)\n lhu $t1, 0(%1)\n .endr\n"
                     " addiu $t4, $t4, -1\n bnez $t4, 1b\n nop\n .set reorder\n" :: "r"(a), "r"(b) : "t0", "t1", "t4", "memory");
    return (uint16_t)(t2() - t0);
}
static uint32_t __attribute__((noinline, aligned(64))) runSingles(uint32_t a) {
    const uint16_t t0 = t2();
    __asm__ volatile(".set noreorder\n li $t4, 64\n 1:\n"
                     ".rept 8\n lhu $t0, 0(%0)\n nop\n .endr\n"
                     " addiu $t4, $t4, -1\n bnez $t4, 1b\n nop\n .set reorder\n" :: "r"(a) : "t0", "t4", "memory");
    return (uint16_t)(t2() - t0);
}
static uint32_t __attribute__((noinline, aligned(64))) runBase(void) {
    const uint16_t t0 = t2();
    __asm__ volatile(".set noreorder\n li $t4, 64\n 1:\n"
                     ".rept 8\n nop\n nop\n .endr\n"
                     " addiu $t4, $t4, -1\n bnez $t4, 1b\n nop\n .set reorder\n" ::: "t4", "memory");
    return (uint16_t)(t2() - t0);
}

/* Each timed twice; the second (warm) kept. */
static uint32_t best(uint32_t (*f)()) { f(); return f(); }
static int g_a, g_b;
static uint32_t pairFn() { return runPairs(IO + regs[g_a].off, IO + regs[g_b].off); }
static uint32_t singleFn() { return runSingles(IO + regs[g_a].off); }
static uint32_t baseFn() { return runBase(); }

static void print2(int num, int den) {             /* num/den, two decimals, signed */
    const int neg = num < 0; if (neg) num = -num;
    printf("%s%d.%02d", neg ? "-" : " ", num / den, (100 * (num % den)) / den);
}

int main() {
    printf("\ncpu/io-pairs\n");
    printf("Two register reads back to back (lhu A; lhu B), 512 pairs a case, cached code: cycles a pair over the same\n");
    printf("loop of nops, and a single read (lhu A; nop). No assertions - compare against your emulator.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    const int base = (int)best(baseFn);
    printf("baseline (16 nops x 512): %d cycles\n\nsingles:\n", base);
    for (int a = 0; a < NREG; a++) { g_a = a; printf("  %-8s", regs[a].name); print2((int)best(singleFn) - base, 512); printf("\n"); }
    static const int pairs[][2] = {
        { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 }, { 7, 7 }, { 8, 8 }, { 9, 9 }, { 10, 10 }, { 11, 11 }, { 12, 12 },
        { 0, 1 }, { 1, 0 }, { 3, 4 }, { 4, 3 }, { 0, 2 }, { 2, 0 }, { 1, 2 }, { 2, 1 },
        { 0, 3 }, { 0, 5 }, { 5, 0 }, { 0, 6 }, { 0, 7 }, { 7, 0 }, { 0, 10 }, { 10, 0 }, { 0, 11 }, { 0, 12 }, { 5, 6 },
    };
    printf("\npairs (A then B):\n");
    for (unsigned p = 0; p < sizeof(pairs) / sizeof(pairs[0]); p++) {
        g_a = pairs[p][0]; g_b = pairs[p][1];
        printf("  %-8s %-8s", regs[g_a].name, regs[g_b].name); print2((int)best(pairFn) - base, 512); printf("\n");
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    printf("\ncpu/io-pairs: Done.\n");
    for (;;) VSync(0);
}
