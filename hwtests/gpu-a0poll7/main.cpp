/*
 * gpu/gpu-a0poll7 - which of the instructions around a lone read decides whether the loop slides or locks: lone MADR2 read loops, each
 * [the head below] then a jr into a run of n nops with the count in its delay slot, the bound and a bne to the upload's end address;
 * 1-16 nops, 4- and 8-word uploads (2026-11-04, for the SCPH-5501). Written by experiments/mka0poll7.js.
 *   M: nop / read / jr (gpu-a0poll5 and -a0poll6: slides)
 *   J: read / nop / jr (the read first, a nop in its delay slot)
 *   K: nop / read / nop / jr (a nop between the read and the jr)
 *   2: read / nop / or (the value used 2 on) / jr
 *   4: read / nop x3 / or (the value used 4 on) / jr
 *   B: gpu-a0poll4's: read / sltu / beqz / nop / sh the value / addiu (gpu-a0poll6: locks)
 *   b: B with its store writing another register
 *
 * WHY. On the second SCPH-5501 (hwtests/gpu-a0poll5, -a0poll6) a lone read a turn slides once the loop is slower than the blocks in the
 * shape M and locks to two blocks a turn in gpu-a0poll4's shape B, one register, one length; this emulator locks both. B alone uses the
 * value soon (its store writes it 4 instructions on): the CPU may run past a held read until it needs the value.
 *
 * READ IT LIKE THIS. P kind mode n rep t turns. A head of h instructions makes the turn h - 2 longer than M's at the same n. No
 * assertions - compare against your emulator.
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
static __attribute__((noinline, noclone)) R pollM(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
static __attribute__((noinline, noclone)) R pollJ(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t2, 0x10a0($t0)\n"
        "nop\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
static __attribute__((noinline, noclone)) R pollK(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw    $t2, 0x10a0($t0)\n"
        "nop\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
static __attribute__((noinline, noclone)) R pollU2(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t2, 0x10a0($t0)\n"
        "nop\n"
        "or    $t1, $t2, $zero\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
static __attribute__((noinline, noclone)) R pollU4(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t2, 0x10a0($t0)\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "or    $t1, $t2, $zero\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
static __attribute__((noinline, noclone)) R pollB(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t2, 0x10a0($t0)\n"
        "sltu  $t1, $t7, $t6\n"
        "beqz  $t1, 2f\n"
        "nop\n"
        "sh    $t2, 0($t7)\n"
        "addiu $t7, $t7, 2\n"
        "2:\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
    R r; r.t = (uint16_t)(t9 - t0); r.turns = turns; return r;
}
static __attribute__((noinline, noclone)) R pollB2(uint32_t chcr, uint32_t n, uint32_t endAddr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t7, 0x1f80\n"
        "addiu $t6, $t7, 1020\n"
        "la    $t3, 9f\n"
        "sll   $t4, %3, 2\n"
        "subu  $t3, $t3, $t4\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "move  $t1, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t2, 0x10a0($t0)\n"
        "sltu  $t1, $t7, $t6\n"
        "beqz  $t1, 2f\n"
        "nop\n"
        "sh    $t1, 0($t7)\n"
        "addiu $t7, $t7, 2\n"
        "2:\n"
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
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "memory");
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
    const uint32_t e = (start + WORDS * 4) & 0xFFFFFCu;
    R r = { 0, 0 };
    switch (mode) {
        case 'M': r = pollM(0x01000201u, n, e); break;
        case 'J': r = pollJ(0x01000201u, n, e); break;
        case 'K': r = pollK(0x01000201u, n, e); break;
        case '2': r = pollU2(0x01000201u, n, e); break;
        case '4': r = pollU4(0x01000201u, n, e); break;
        case 'B': r = pollB(0x01000201u, n, e); break;
        case 'b': r = pollB2(0x01000201u, n, e); break;
        default: break;
    }
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("P S%d %c %u %d %u %u\n", a, mode, (unsigned)n, rep, (unsigned)r.t, (unsigned)r.turns);
}

int main() {
    printf("\ngpu/gpu-a0poll7\n");
    printf("P kind mode n rep t turns (2,048 words into 1,024x4 at (0,256) in vblank; one MADR2 read a turn, modes M J K 2 4 B b; n nops). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const char M[7] = { 'M', 'J', 'K', '2', '4', 'B', 'b' };
    for (int m = 0; m < 7; m++) one(4, M[m], 1, 0);   /* cache the code */
    static const int A[2] = { 4, 8 };
    for (int k = 0; k < 2; k++) for (int m = 0; m < 7; m++) for (uint32_t n = 1; n <= 16; n++) for (int r = 0; r < 2; r++) one(A[k], M[m], n, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0poll7: Done.\n");
    for (;;) VSync(0);
}
