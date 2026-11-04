/*
 * gpu/gpu-a0poll9 - which registers' reads let the CPU run on past a GPU block: gpu-a0poll7's sliding loop M (nop, the read, jr into n
 * nops, the count) run a fixed 200 turns while a 4- or 8-word upload runs, reading Timer 2, MADR2, CHCR2 or I_STAT, n = 4-16 (2026-11-04,
 * for the SCPH-5501). Written by experiments/mka0poll9.js.
 *
 * WHY. On the second SCPH-5501, hwtests/gpu-a0poll7: a lone MADR2 read a turn slides when nothing after it uses the value or touches
 * memory before the next turn (M, J, K) and locks to the blocks when something does (the value used 2 or 4 on, a store) - the CPU runs
 * on past a pending read until then. But hwtests/gpu-dma-pair's two Timer 2 reads with one nop between them land a block apart where
 * none between land 4 cycles apart: there the nop waited for the first read. Timer 2 or the DMA's registers?
 *
 * WHAT IT DOES. gpu-a0poll's set-up and start (2,048 words into 1,024x4 at (0,256) in vblank), then 200 turns of the loop - over before
 * the upload ends - and the cycles for them. Twice each.
 *
 * READ IT LIKE THIS. Q kind register n rep t: t / 200 the turn; the loop's own period is 10 + n cycles. No assertions - compare
 * against your emulator.
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
static __attribute__((noinline, noclone)) uint32_t pollT2(uint32_t chcr, uint32_t n) {
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lhu   $t2, 0x1120($t0)\n"
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
        : "t0", "t2", "t3", "t4", "t5", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t pollMADR2(uint32_t chcr, uint32_t n) {
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw   $t2, 0x10a0($t0)\n"
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
        : "t0", "t2", "t3", "t4", "t5", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t pollCHCR2(uint32_t chcr, uint32_t n) {
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw   $t2, 0x10a8($t0)\n"
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
        : "t0", "t2", "t3", "t4", "t5", "memory");
    return (uint16_t)(t9 - t0);
}
static __attribute__((noinline, noclone)) uint32_t pollISTAT(uint32_t chcr, uint32_t n) {
    uint32_t t0, t9;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "sll   $t4, %2, 2\n"
        "subu  $t3, $t3, $t4\n"
        "li    $t5, 200\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw   $t2, 0x1070($t0)\n"
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
        : "t0", "t2", "t3", "t4", "t5", "memory");
    return (uint16_t)(t9 - t0);
}

static void one(int a, int r, uint32_t n, int rep) {
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
    switch (r) {
        case 0: t = pollT2(0x01000201u, n); break;
        case 1: t = pollMADR2(0x01000201u, n); break;
        case 2: t = pollCHCR2(0x01000201u, n); break;
        case 3: t = pollISTAT(0x01000201u, n); break;
        default: break;
    }
    settle();
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    static const char* const NM[4] = { "T2", "MADR2", "CHCR2", "ISTAT" };
    printf("Q S%d %s %u %d %u\n", a, NM[r], (unsigned)n, rep, (unsigned)t);
}

int main() {
    printf("\ngpu/gpu-a0poll9\n");
    printf("Q kind register n rep t (200 turns of nop / the read / jr into n nops during a 2,048-word upload; the loop's own turn 10 + n). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int r = 0; r < 4; r++) one(4, r, 8, 0);   /* cache the code */
    static const int A[2] = { 4, 8 };
    for (int k = 0; k < 2; k++) for (int r = 0; r < 4; r++) for (uint32_t n = 4; n <= 16; n++) for (int rep = 0; rep < 2; rep++) one(A[k], r, n, rep);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0poll9: Done.\n");
    for (;;) VSync(0);
}
