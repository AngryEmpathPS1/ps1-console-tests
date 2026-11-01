/*
 * cpu/mul-shadow - a multiply in a read's shadow: when does its time start? (2026-11-01)
 *
 * WHY. hwtests/mul-grid: `lhu T2; nop; mult; mflo` reads 4 on the SCPH-7502 where this core reads 8, every operand, and cpu-extras, with no read just before, agrees with
 * both at 6. This core charges a read's whole time when it issues and refunds it behind (shadowStep), so what runs in the shadow starts late on this core's clock; a
 * multiply's end is taken from it. This asks by what, for each kind of read, at each distance.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc, warm, generated code: T2 read; 8 nops; R; D nops; mult; K nops; mflo; 8 nops; T2 read. R is none, lhu I_STAT,
 * lhu Timer 1, lhu SPUSTAT, lw RAM, lw scratchpad; D 0..4; K 0..6; rs 1 (a 7-cycle multiply), then rs 100000h (14). Each cell the fastest of 4, less the block with
 * nops for mult and mflo, so a multiply that needed no wait reads 0 behind D + K.
 *
 * READ IT LIKE THIS. With no read in front the row is cpu-extras' 7 - D... the wait at each K. A read that shifts the multiply's start shifts its row by that many.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js mul-shadow --frames=2500 --quiet=800`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define NOP 0u

extern "C" {
uint32_t msRam[16] __attribute__((aligned(16)));
uint32_t msCode[64] __attribute__((aligned(64)));
uint32_t msTime(uint32_t* code, uint32_t rs);
void msEvict(void);
}
__asm__(
    ".text\n.set push\n.set noreorder\n.set noat\n"
    ".balign 4096\n"
    ".global msEvict\n"
    "msEvict:\n"
    ".rept 1024\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 64\n"
    ".global msTime\n"
    "msTime:\n"
    "addiu $sp, $sp, -16\n"
    "sw $ra, 0($sp)\n"
    "sw $s0, 4($sp)\n"
    "sw $s1, 8($sp)\n"
    "sw $s2, 12($sp)\n"
    "move $t0, $a1\n"
    "li $t1, 1\n"
    "lui $s0, %hi(msRam)\n"
    "addiu $s0, $s0, %lo(msRam)\n"
    "lui $s1, 0x1F80\n"
    "lui $s2, 0x1F80\n"
    "ori $s2, $s2, 0x1000\n"
    "jalr $a0\n"
    "nop\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "lw $ra, 0($sp)\n"
    "lw $s0, 4($sp)\n"
    "lw $s1, 8($sp)\n"
    "lw $s2, 12($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 16\n"
    ".set pop\n"
);

static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }
#define S0 16u
#define S1 17u
#define S2 18u
#define MULT 0x01090018u      /* mult t0, t1 */
#define MFLO 0x00005012u      /* mflo t2 */
static const char* const RN[6] = { "none", "I_STAT", "TIMER1", "SPUSTAT", "RAM", "SCRATCH" };
static uint32_t readOf(int r) {
    switch (r) {
        case 1: return I(0x25, S2, 12, 0x070);
        case 2: return I(0x25, S2, 12, 0x110);
        case 3: return I(0x25, S2, 12, 0xDAE);
        case 4: return I(0x23, S0, 12, 0x010);
        case 5: return I(0x23, S1, 12, 0x010);
    }
    return NOP;
}
static void build(int r, int d, int k, bool mul) {
    int n = 0;
    msCode[n++] = I(0x25, S2, 2, 0x120);                       /* lhu v0, T2 */
    for (int i = 0; i < 8; i++) msCode[n++] = NOP;
    msCode[n++] = readOf(r);
    for (int i = 0; i < d; i++) msCode[n++] = NOP;
    msCode[n++] = mul ? MULT : NOP;
    for (int i = 0; i < k; i++) msCode[n++] = NOP;
    msCode[n++] = mul ? MFLO : NOP;
    for (int i = 0; i < 8; i++) msCode[n++] = NOP;
    msCode[n++] = I(0x25, S2, 3, 0x120);                       /* lhu v1, T2 */
    msCode[n++] = NOP;
    msCode[n++] = 0x03E00008u;                                 /* jr ra */
    msCode[n++] = NOP;
}
static uint32_t cell(int r, int d, int k, bool mul, uint32_t rs) {
    build(r, d, k, mul);
    msEvict();
    uint32_t m = 0xFFFF;
    for (int t = 0; t < 6; t++) { const uint32_t v = msTime(msCode, rs); if (t && v < m) m = v; }
    return m;
}

int main() {
    printf("\ncpu/mul-shadow\n");
    printf("R, D nops, mult, K nops, mflo: cycles over the block with nops for mult and mflo, K = 0..6. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    static const uint32_t RS[2] = { 1, 0x100000 };
    for (int s = 0; s < 2; s++) {
        printf("\nrs %x:\n", RS[s]);
        for (int r = 0; r < 6; r++)
            for (int d = 0; d < 5; d++) {
                printf(" %-8s D %d:", RN[r], d);
                for (int k = 0; k < 7; k++) {
                    const int a = (int)cell(r, d, k, true, RS[s]), b = (int)cell(r, d, k, false, RS[s]);
                    printf(" %3d", a - b);
                }
                printf("\n");
            }
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/mul-shadow: Done.\n");
    for (;;) VSync(0);
}
