/*
 * cpu/icache-valid - which words of a code line are in the instruction cache after a loop, read by what runs (2026-11-02)
 *
 * WHY. hwtests/loop-cold: a loop "nop; addiu t8; X; bnez t8; nop" entered cold costs the same each pass on the SCPH-5501 and here,
 * but after two passes the console leaves the loop exactly as after one (every cell n=2 is n=1 + one pass, with X a SPUSTAT read),
 * where this core leaves 8 cycles sooner: the line holding the branch is not, after the first pass, what this core keeps of it.
 * Timing only shows that something differs. This shows which words.
 *
 * HOW. The instruction cache does not watch stores. After the loop has run (n passes, from cold), one routine - no C between, so
 * nothing else is fetched into that part of the cache - overwrites the branch's line in RAM with "addiu t9, t9, 1", then jumps
 * into it at word k with t8 = 0 and t9 = 0. A word still cached runs the old instruction (the branch is not taken with t8 = 0);
 * one that is not is fetched from RAM - the new one - and so is every word of the line after it, which the miss fills. t9 counts
 * the words that came from RAM. Entered at each word of the line, the counts give the line's valid bits.
 *
 * WHAT IT DOES. Interrupts off. The code (hwtests/loop-cold's, a = 0, c = 0, at cache index 400h): T2; L: nop; addiu t8, -1; X;
 * bnez t8, L; nop; T2; jr ra. Words 4-7 are the branch's line: bnez, nop, lhu T2, nop. X is nop, lw A, lbu A, lhu I_STAT, lhu
 * SPUSTAT, sw A, sw scratchpad; n 1, 2, 3, 8. A line: V X n, then t9 entered at words 4 5 6 7 (4 = every word was missed, 0 = all
 * cached, from that word on).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js icache-valid`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define KINDS 7

extern "C" {
uint32_t paSandbox[0x2200] __attribute__((aligned(1024)));
uint32_t paBuf[2048] __attribute__((aligned(4096)));
uint32_t paProbe(uint32_t* code, uint32_t n, uint32_t* entry, uint32_t* line);
void paEvict(void);
}
#define paCode (paBuf + 0x100)                       /* cache index 400h */

/* paProbe(code, n, entry, line): the loop run n times from code, then line[0..3] = addiu t9, t9, 1 (from RAM, by stores), then a
 * jump to entry with t8 = t9 = 0; returns t9. All here, at the start of a 4 KB page: cache index 000h on, clear of 400h. */
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
    ".global paProbe\n"
    "paProbe:\n"
    "addiu $sp, $sp, -32\n"
    "sw $ra, 0($sp)\n"
    "sw $s0, 4($sp)\n"
    "sw $s1, 8($sp)\n"
    "sw $s2, 12($sp)\n"
    "sw $s3, 16($sp)\n"
    "sw $s4, 20($sp)\n"
    "move $s3, $a2\n"
    "move $s4, $a3\n"
    "lui $s0, %hi(paSandbox)\n"
    "addiu $s0, $s0, %lo(paSandbox)\n"
    "lui $s1, 0x1F80\n"
    "lui $s2, 0x1F80\n"
    "ori $s2, $s2, 0x1000\n"
    "li $t0, 1\n"
    "move $t8, $a1\n"
    "jalr $a0\n"
    "nop\n"
    "lui $t1, 0x2739\n"
    "ori $t1, $t1, 0x0001\n"                     /* addiu t9, t9, 1 */
    "sw $t1, 0($s4)\n"
    "sw $t1, 4($s4)\n"
    "sw $t1, 8($s4)\n"
    "sw $t1, 12($s4)\n"
    "lw $t1, 12($s4)\n"                          /* the stores are in RAM before anything is fetched from it */
    "move $t8, $zero\n"
    "move $t9, $zero\n"
    "jalr $s3\n"
    "nop\n"
    "move $v0, $t9\n"
    "lw $ra, 0($sp)\n"
    "lw $s0, 4($sp)\n"
    "lw $s1, 8($sp)\n"
    "lw $s2, 12($sp)\n"
    "lw $s3, 16($sp)\n"
    "lw $s4, 20($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 32\n"
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
static void build(int x) {
    uint32_t* p = paCode;
    p[0] = I(0x25, S2, 2, 0x120);                   /* lhu v0, T2 */
    p[1] = 0;                                       /* L: nop */
    p[2] = I(0x09, T8, T8, 0xFFFF);                 /* addiu t8, t8, -1 */
    p[3] = kind(x);
    p[4] = 0x17000000u | ((uint32_t)(1 - 5) & 0xFFFF);   /* bnez t8, L */
    p[5] = 0;
    p[6] = I(0x25, S2, 3, 0x120);                   /* lhu v1, T2 */
    p[7] = 0;
    p[8] = 0x03E00008u;                             /* jr ra */
    p[9] = 0;
}

int main() {
    static const uint16_t NS[4] = { 1, 2, 3, 8 };
    printf("\ncpu/icache-valid\n");
    printf("The branch's line (words 4-7) after n passes of a cold loop: t9 = the words that came from RAM, entered at 4 5 6 7. V X n.\n");
    printf("X: 0 nop, 1 lw A, 2 lbu A, 3 lhu I_STAT, 4 lhu SPUSTAT, 5 sw A, 6 sw scratchpad. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int x = 0; x < KINDS; x++)
        for (int k = 0; k < 4; k++) {
            uint32_t v[4];
            for (int e = 0; e < 4; e++) {
                build(x);
                paEvict();
                v[e] = paProbe(paCode, NS[k], paCode + 4 + e, paCode + 4);
            }
            printf("V %d %u %u %u %u %u\n", x, NS[k], (unsigned)v[0], (unsigned)v[1], (unsigned)v[2], (unsigned)v[3]);
        }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) { }
}
