/*
 * gpu/gpu-a0poll11 - which instruction behind an I/O read a GPU block holds ends the CPU's run past it: gpu-a0poll9's sliding loop (a nop,
 * the Timer 2 read, three slots, a jr into n nops, the count, bnez) for 200 turns during a 4- or 8-word upload, the slots nops but one:
 * W an ALU op writing another register, Z one writing $zero, S a scratchpad store, L a scratchpad load, U a reader of the read's register,
 * in the slot right behind the read (1), the next (2) or the one after (3) (2026-11-04, for the SCPH-5501). Written by
 * experiments/mka0poll10.js.
 *
 * WHY. On the second SCPH-5501 a lone read a turn slides once slower than the blocks (gpu-a0poll5, -a0poll7, -a0poll9: the CPU runs on
 * past the held read) unless something behind it uses the value or touches memory - and yet in hwtests/gpu-dma-pair the CPU waits out a
 * held read with only nops and an "li" behind it. This asks the slots one at a time.
 *
 * READ IT LIKE THIS. Q kind variant n rep t: t / 200 the turn; the loop's own turn 11 + n (sliding), a lock reads two blocks (28.5 /
 * 36.6). No assertions - compare against your emulator.
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
static void settle() { for (unsigned k = 0; k < 200000 && (read32(CHCR2) & 0x01000000u); k++) {} }
static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}
static __attribute__((noinline, noclone)) uint32_t poll0(uint32_t chcr, uint32_t n) {   /* base */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll1(uint32_t chcr, uint32_t n) {   /* N1 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "bne   $t8, $t8, 30f\n"
        "nop\n"
        "30:\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll2(uint32_t chcr, uint32_t n) {   /* F1 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "beq   $zero, $zero, 31f\n"
        "nop\n"
        "nop\n"
        "31:\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll3(uint32_t chcr, uint32_t n) {   /* J1 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "j     32f\n"
        "nop\n"
        "nop\n"
        "32:\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll4(uint32_t chcr, uint32_t n) {   /* B1 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "li    $t7, 2\n"
        "33:\n"
        "addiu $t7, $t7, -1\n"
        "bnez  $t7, 33b\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll5(uint32_t chcr, uint32_t n) {   /* N2 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "bne   $t8, $t8, 34f\n"
        "nop\n"
        "34:\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll6(uint32_t chcr, uint32_t n) {   /* F2 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "beq   $zero, $zero, 35f\n"
        "nop\n"
        "nop\n"
        "35:\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll7(uint32_t chcr, uint32_t n) {   /* J2 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "j     36f\n"
        "nop\n"
        "nop\n"
        "36:\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll8(uint32_t chcr, uint32_t n) {   /* B2 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "li    $t7, 2\n"
        "37:\n"
        "addiu $t7, $t7, -1\n"
        "bnez  $t7, 37b\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll9(uint32_t chcr, uint32_t n) {   /* N3 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "nop\n"
        "bne   $t8, $t8, 38f\n"
        "nop\n"
        "38:\n"
        "nop\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll10(uint32_t chcr, uint32_t n) {   /* F3 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "nop\n"
        "beq   $zero, $zero, 39f\n"
        "nop\n"
        "nop\n"
        "39:\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll11(uint32_t chcr, uint32_t n) {   /* J3 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "nop\n"
        "j     40f\n"
        "nop\n"
        "nop\n"
        "40:\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t poll12(uint32_t chcr, uint32_t n) {   /* B3 */
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t8, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "move  $t6, $zero\n"
        "move  $t7, $zero\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
        "nop\n"
        "nop\n"
        "li    $t7, 2\n"
        "41:\n"
        "addiu $t7, $t7, -1\n"
        "bnez  $t7, 41b\n"
        "nop\n"
        "jr    $t3\n"
        "addiu $t5, $t5, -1\n"
        ".rept 32\nnop\n.endr\n"
        "9:\n"
        "bnez  $t5, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9)
        : "r"(n), "r"(chcr)
        : "t0", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return (uint16_t)(t9 - t0);
}

static const char* const NAMES[13] = { "base", "N1", "F1", "J1", "B1", "N2", "F2", "J2", "B2", "N3", "F3", "J3", "B3" };
static void one(int a, int v, uint32_t n, int rep) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / a) << 16) | (uint32_t)a);
    toPlace();
    uint32_t t = 0;
    switch (v) {
        case 0: t = poll0(0x01000201u, n); break;
        case 1: t = poll1(0x01000201u, n); break;
        case 2: t = poll2(0x01000201u, n); break;
        case 3: t = poll3(0x01000201u, n); break;
        case 4: t = poll4(0x01000201u, n); break;
        case 5: t = poll5(0x01000201u, n); break;
        case 6: t = poll6(0x01000201u, n); break;
        case 7: t = poll7(0x01000201u, n); break;
        case 8: t = poll8(0x01000201u, n); break;
        case 9: t = poll9(0x01000201u, n); break;
        case 10: t = poll10(0x01000201u, n); break;
        case 11: t = poll11(0x01000201u, n); break;
        case 12: t = poll12(0x01000201u, n); break;
        default: break;
    }
    settle();
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("Q S%d %s %u %d %u\n", a, NAMES[v], (unsigned)n, rep, (unsigned)t);
}

int main() {
    printf("\ngpu/gpu-a0poll11\n");
    printf("Q kind variant n rep t (200 turns of nop / Timer 2 / three slots / jr into n nops during a 2,048-word upload). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int v = 0; v < 13; v++) one(4, v, 10, 0);   /* cache the code */
    static const int A[2] = { 4, 8 };
    static const uint32_t NS[2][2] = { { 8, 12 }, { 12, 16 } };
    for (int k = 0; k < 2; k++) for (int v = 0; v < 13; v++) for (int q = 0; q < 2; q++) for (int r = 0; r < 2; r++) one(A[k], v, NS[k][q], r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0poll11: Done.\n");
    for (;;) VSync(0);
}
