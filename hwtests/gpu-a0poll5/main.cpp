/*
 * gpu/gpu-a0poll5 - is it the register or the loop? gpu-a0poll's lone-read loop (mode C: a nop, the read, a jr into a run of nops, the
 * count, and, bnez) reading CHCR2 as gpu-a0poll did, or MADR2 (leaving when it reaches the upload's end), 0-16 nops more, on 4- and
 * 8-word SyncMode 1 uploads (2026-11-04, for the SCPH-5501).
 *
 * WHY. On the second SCPH-5501, hwtests/gpu-a0poll's CHCR2 loop runs at its own period once slower than the blocks (432, 390, 364, 304
 * turns at n = 6, 8, 10, 14: 16.9-24.0 cycles a turn, the upload quick), but hwtests/gpu-a0poll4's MADR2 loop of the same length (a
 * branch and a scratchpad store where this has the jr) locks to two blocks a turn there (342, 259, 256, 255) - as this emulator locks
 * both. And hwtests/gpu-dma-hold holds a CHCR2 read exactly as a Timer 2 read when it follows a long cached wait. One loop, two
 * registers, says which.
 *
 * WHAT IT DOES. gpu-a0poll's set-up and start; then in one asm block a Timer 2 read, CHCR2's store and the loop: mode C, lw CHCR2, jr
 * into the nops, the count, and with the busy bit, bnez; mode M, lw MADR2 in the same place, bne against the upload's end address in
 * the bnez's; Timer 2 after it. Twice each.
 *
 * READ IT LIKE THIS. P kind mode n rep t turns: t the cycles from before the store to after the loop, turns the loop's. No assertions -
 * compare against your emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}
struct R { uint32_t t, turns; };
/* mode C: gpu-a0poll's pollC exactly */
static __attribute__((noinline, noclone)) R pollC(uint32_t chcr, uint32_t n) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "lui   $t6, 0x0100\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw    $t2, 0x10a8($t0)\n"
        "jr    $t3\n"
        "addiu %2, %2, 1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "beq   %2, $t5, 8f\n"
        "and   $t2, $t2, $t6\n"
        "bnez  $t2, 1b\n"
        "nop\n"
        "8:\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9), "=&r"(turns)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
/* mode M: the same loop reading MADR2, the and/bnez pair a nop and a bne against the end address */
static __attribute__((noinline, noclone)) R pollM(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw    $t2, 0x10a0($t0)\n"
        "jr    $t3\n"
        "addiu %2, %2, 1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "beq   %2, $t5, 8f\n"
        "nop\n"
        "bne   $t2, %5, 1b\n"
        "nop\n"
        "8:\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9), "=&r"(turns)
        : "r"(n), "r"(chcr), "r"(endAddr)
        : "t0", "t2", "t3", "t4", "t5", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}

static void one(int a, char mode, uint32_t n, int rep) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    const uint32_t start = (uint32_t)buf & 0xFFFFFCu;
    write32(MADR2, start);
    write32(BCR2, ((uint32_t)(WORDS / a) << 16) | (uint32_t)a);
    toPlace();
    const R r = mode == 'C' ? pollC(0x01000201u, n) : pollM(0x01000201u, n, (start + WORDS * 4) & 0xFFFFFCu);
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("P S%d %c %u %d %u %u\n", a, mode, (unsigned)n, rep, (unsigned)r.t, (unsigned)r.turns);
}

int main() {
    printf("\ngpu/gpu-a0poll5\n");
    printf("P kind mode n rep t turns (2,048 words into 1,024x4 at (0,256) in vblank; mode C a CHCR2 read a turn, M a MADR2 read; n nops). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one(4, 'C', 0, 0); one(4, 'M', 0, 0);   /* cache the code */
    static const int A[2] = { 4, 8 };
    static const char M[2] = { 'C', 'M' };
    for (int k = 0; k < 2; k++) for (int m = 0; m < 2; m++) for (uint32_t n = 0; n <= 16; n++) for (int r = 0; r < 2; r++) one(A[k], M[m], n, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0poll5: Done.\n");
    for (;;) VSync(0);
}
