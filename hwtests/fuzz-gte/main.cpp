/*
 * cpu/fuzz-gte - random GTE commands on random registers, the same everywhere (2026-11-01)
 *
 * WHY. Every GTE test so far was written by someone who knew what to ask. This asks what nobody chose: all 64 registers loaded from a mix of small numbers, full words and
 * edges, a command with random fields, and every register read back. A program id is the whole test: the generator is xorshift32 from 0x9E3779B9 * (id + 1), so a line that
 * differs can be rebuilt anywhere.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, no disc. For each of 4,096 ids: load every control and data register, read them all back (what a write keeps), run
 * the command warm with mfc2 right behind it, read them all back again. A line: G cmd time pre post, the command word, the bracket, a hash of the registers before and after
 * the command. For the first 256 ids a D line follows with the 32 data registers and the FLAG.
 *
 * READ IT LIKE THIS. pre differs: a register write or read. post differs and pre does not: the command. time differs: its latency, or a refresh in the bracket (+-5).
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-gte --frames=20000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE  0x1F801124u
#define IDS 4096
#define DETAIL 256

extern "C" {
uint32_t gtCtrl[32], gtData[32], gtOut[64];
uint32_t gtCode[16] __attribute__((aligned(16)));
void gtEnable(void);
void gtLoad(void);
void gtSave(void);
uint32_t gtRun(uint32_t* code);
void gtEvict(void);
}

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".global gtEnable\n"
    "gtEnable:\n"
    "mfc0 $t0, $12\n"
    "lui $t1, 0x4000\n"
    "or $t0, $t0, $t1\n"
    "mtc0 $t0, $12\n"
    "nop\n"
    "jr $ra\n"
    "nop\n"
    ".global gtLoad\n"
    "gtLoad:\n"
    "lui $t0, %hi(gtCtrl)\n"
    "addiu $t0, $t0, %lo(gtCtrl)\n"
    ".irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"
    "lw $t1, (\\n*4)($t0)\n"
    "nop\n"
    "ctc2 $t1, $\\n\n"
    "nop\n"
    ".endr\n"
    "lui $t0, %hi(gtData)\n"
    "addiu $t0, $t0, %lo(gtData)\n"
    ".irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"
    "lw $t1, (\\n*4)($t0)\n"
    "nop\n"
    "mtc2 $t1, $\\n\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".global gtSave\n"
    "gtSave:\n"
    "lui $t0, %hi(gtOut)\n"
    "addiu $t0, $t0, %lo(gtOut)\n"
    ".irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"
    "mfc2 $t1, $\\n\n"
    "nop\n"
    "sw $t1, (\\n*4)($t0)\n"
    ".endr\n"
    ".irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"
    "cfc2 $t1, $\\n\n"
    "nop\n"
    "sw $t1, (128+\\n*4)($t0)\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 4096\n"
    ".global gtEvict\n"
    "gtEvict:\n"
    ".rept 1024\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 64\n"
    ".global gtRun\n"
    "gtRun:\n"
    "addiu $sp, $sp, -8\n"
    "sw $ra, 0($sp)\n"
    "lui $t9, 0x1F80\n"
    ".rept 8\n"
    "nop\n"
    ".endr\n"
    "jalr $a0\n"
    "nop\n"
    "subu $v0, $v1, $v0\n"
    "andi $v0, $v0, 0xFFFF\n"
    "lw $ra, 0($sp)\n"
    "nop\n"
    "jr $ra\n"
    "addiu $sp, $sp, 8\n"
    ".set pop\n"
);

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static const uint32_t EDGE[12] = { 0, 1, 0xFFFFFFFFu, 0x7FFF, 0x8000, 0xFFFF, 0xFFFF8000u, 0x7FFFFFFF, 0x80000000u, 0x1000, 0xFFFFF000u, 0x10000 };
static uint32_t val() {
    switch (rnd() % 8) {
        case 0: case 1: case 2: return (uint32_t)((int32_t)(rnd() & 0x1FFF) - 0x1000);
        case 3: return (uint32_t)((int32_t)(rnd() & 0xFFFF) - 0x8000);
        case 4: return rnd();
        case 5: return EDGE[rnd() % 12];
        case 6: return 0;
    }
    return rnd() & 0xFFF;
}
static const uint8_t FN[22] = { 0x01, 0x06, 0x0C, 0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x1B, 0x1C, 0x1E, 0x20, 0x28, 0x29, 0x2A, 0x2D, 0x2E, 0x30, 0x3D, 0x3E, 0x3F };
static uint32_t gen() {
    for (int i = 0; i < 32; i++) gtCtrl[i] = val();
    for (int i = 0; i < 32; i++) gtData[i] = val();
    uint32_t fn = rnd() % 10 ? FN[rnd() % 22] : rnd() % 64;
    uint32_t cmd = fn | (rnd() & 0x000FE400u);          /* sf, mx, v, cv, lm */
    if (!(rnd() % 4)) cmd |= rnd() & 0x01F00000u;       /* the fake bits */
    return cmd;
}
static void build(uint32_t cmd) {
    int n = 0;
    gtCode[n++] = 0x97221120u;                          /* lhu v0, 1120h(t9): Timer 2 */
    gtCode[n++] = 0;
    gtCode[n++] = 0x4A000000u | cmd;
    gtCode[n++] = 0x480AC800u;                          /* mfc2 t2, $25 */
    gtCode[n++] = 0;
    gtCode[n++] = 0x97231120u;                          /* lhu v1, 1120h(t9) */
    gtCode[n++] = 0;
    gtCode[n++] = 0x03E00008u;                          /* jr ra */
    gtCode[n++] = 0;
}
static uint32_t hash() { uint32_t h = 2166136261u; for (int i = 0; i < 64; i++) { uint32_t v = gtOut[i]; for (int b = 0; b < 4; b++) { h = (h ^ (v & 255)) * 16777619u; v >>= 8; } } return h; }

int main() {
    printf("\ncpu/fuzz-gte\n");
    printf("%d ids: G cmd time pre post; the first %d with D, data registers and FLAG. No assertions.\n\n", IDS, DETAIL);
    EnterCriticalSection();
    gtEnable();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    for (int id = 0; id < IDS; id++) {
        rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
        const uint32_t cmd = gen();
        build(cmd);
        gtEvict();
        gtLoad();
        gtRun(gtCode);
        gtLoad();
        gtSave();
        const uint32_t pre = hash();
        gtLoad();
        const uint32_t t = gtRun(gtCode);
        gtSave();
        const uint32_t post = hash();
        printf("G %x %u %x %x\n", cmd, t, pre, post);
        if (id < DETAIL) { printf("D"); for (int i = 0; i < 32; i++) printf(" %x", gtOut[i]); printf(" %x\n", gtOut[63]); }
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncpu/fuzz-gte: Done.\n");
    for (;;) VSync(0);
}
