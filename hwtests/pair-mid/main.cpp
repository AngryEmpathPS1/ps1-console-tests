/*
 * cpu/pair-mid - cold code, two instructions and then two timestamps: when the first read happened, not just what it cost the second (2026-11-01)
 *
 * WHY. hwtests/pair-cold reads one number a cell, the last read, and in cold code a store is on the bus behind everything else: in store; read; T2 the same row has a read going ahead
 * of the store, a fill going ahead of it, or neither, and the end of the row cannot say which. A timer read in the middle says where the bus was at that moment.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc, generated code, every line cold: T2; a nops; X; b nops; Y; c nops; T2 (the middle); d nops; T2 (the end). X one of
 * nop, sw A, lw A, lhu I_STAT; Y one of nop, sw A, sw B (32 KB on, another DRAM page), lw A, lw B, lhu I_STAT, lhu SPUSTAT. a 0..6, b 0..2, c 0..3, d 0..6: 16,464 cells, the fastest
 * of 6 trials a few cycles apart. Nothing depends on anything.
 *
 * READ IT LIKE THIS. P X Y opens a pair; a line: A a b c, then middle/end at d = 0..6, both from the first read. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js pair-mid --frames=30000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define AS 7
#define BS 3
#define CS 4
#define DS 7
#define TRIALS 6

extern "C" {
uint32_t paSandbox[0x2200] __attribute__((aligned(1024)));
uint32_t paBuf[2048] __attribute__((aligned(4096)));
uint32_t paMid;
uint32_t paTrial(uint32_t* code);
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
    "li $t8, 32\n"
    "2:\n"
    "bnez $t8, 2b\n"
    "addiu $t8, $t8, -1\n"
    ".rept 16\n"
    "nop\n"
    ".endr\n"
    "jalr $a0\n"
    "nop\n"
    "subu $t3, $t3, $v0\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "andi $t3, $t3, 0xFFFF\n"
    "lui $at, %hi(paMid)\n"
    "sw $t3, %lo(paMid)($at)\n"
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
#define S2 18u
#define S3 19u
#define T0 8u
static const int XK[] = { 0, 1, 3, 5 };
static const int YK[] = { 0, 1, 2, 3, 4, 5, 6 };
static uint32_t kind(int k, uint32_t rt) {
    switch (k) {
        case 1: return I(0x2B, S0, T0, 0x040);      /* sw A */
        case 2: return I(0x2B, S3, T0, 0x040);      /* sw B */
        case 3: return I(0x23, S0, rt, 0x080);      /* lw A */
        case 4: return I(0x23, S3, rt, 0x080);      /* lw B */
        case 5: return I(0x25, S2, rt, 0x070);      /* lhu I_STAT */
        case 6: return I(0x25, S2, rt, 0xDAE);      /* lhu SPUSTAT */
    }
    return 0;
}
static void build(int x, int y, int a, int b, int c, int d) {
    int n = 0;
    uint32_t* p = paCode;
    p[n++] = I(0x25, S2, 2, 0x120);                 /* lhu v0, T2 */
    for (int i = 0; i < a; i++) p[n++] = 0;
    p[n++] = kind(x, 12);
    for (int i = 0; i < b; i++) p[n++] = 0;
    p[n++] = kind(y, 13);
    for (int i = 0; i < c; i++) p[n++] = 0;
    p[n++] = I(0x25, S2, 11, 0x120);                /* lhu t3, T2 */
    for (int i = 0; i < d; i++) p[n++] = 0;
    p[n++] = I(0x25, S2, 3, 0x120);                 /* lhu v1, T2 */
    p[n++] = 0;
    p[n++] = 0x03E00008u;                           /* jr ra */
    p[n++] = 0;
}
static uint32_t cellMid;
static uint32_t cell() {
    uint32_t m = 0xFFFF;
    for (int t = 0; t < TRIALS; t++) { paEvict(); const uint32_t v = paTrial(paCode); if (v < m) { m = v; cellMid = paMid; } for (volatile int i = 0; i < t * 3; i++) {} }
    return m;
}

int main() {
    printf("\ncpu/pair-mid\n");
    printf("T2; a nops; X; b nops; Y; c nops; T2; d nops; T2, cold, fastest of %d. P X Y, then A a b c and middle/end at d = 0..%d. No assertions.\n\n", TRIALS, DS - 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    static uint16_t rowM[DS], rowE[DS];
    for (unsigned x = 0; x < sizeof(XK) / sizeof(XK[0]); x++)
        for (unsigned y = 0; y < sizeof(YK) / sizeof(YK[0]); y++) {
            printf("P %d %d\n", XK[x], YK[y]);
            for (int a = 0; a < AS; a++)
                for (int b = 0; b < BS; b++)
                    for (int c = 0; c < CS; c++) {
                        for (int d = 0; d < DS; d++) { build(XK[x], YK[y], a, b, c, d); rowE[d] = (uint16_t)cell(); rowM[d] = (uint16_t)cellMid; }
                        printf("A %d %d %d", a, b, c);
                        for (int d = 0; d < DS; d++) printf(" %u/%u", rowM[d], rowE[d]);
                        printf("\n");
                    }
        }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/pair-mid: Done.\n");
    for (;;) VSync(0);
}
