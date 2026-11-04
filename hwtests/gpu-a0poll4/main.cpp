/*
 * gpu/gpu-a0poll4 - gpu-a0poll's lone-read loop, read turn by turn without a second read: each turn one MADR2 read (16 bytes a 4-word
 * block), its low half kept in the scratchpad, n nops, until MADR2 reaches the upload's end; 4- and 8-word SyncMode 1 uploads
 * (2026-11-04, for the SCPH-5501). Written by experiments/mka0poll4.js.
 *
 * WHY. hwtests/gpu-a0poll's one CHCR2 read a turn runs at the loop's own period on the second SCPH-5501 once it is slower than the
 * blocks (10 + n cycles from n = 10) with the upload unslowed; gpu-dma-hold holds a single read 11 cycles of a 4-word block's 14 on
 * the same console, and this emulator locks the loop to two blocks a turn. gpu-a0poll3's Timer 2 stamp made each turn a pair (and
 * such loops lock on the console too). The blocks that pass between two turns, with nothing but the one read a turn, say whether the
 * loop and the blocks lock or slide.
 *
 * WHAT IT DOES. gpu-a0poll's set-up (2,048 words into 1,024x4 at (0,256), vblank's first hblank edge + 500 cycles); one asm block per
 * n: Timer 2, CHCR2's store, the loop, Timer 2 at its end.
 *
 * READ IT LIKE THIS. S kind n rep turns t: then MADR2's moves turn to turn in blocks (the first from MADR's start). t the cycles from
 * the store to the end. No assertions - compare against your emulator.
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
static uint16_t marks[512];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}
static __attribute__((noinline, noclone)) uint32_t poll(uint32_t chcr, uint32_t n, uint32_t endAddr, uint16_t* tout) {
    uint32_t t0 = 0, t9 = 0, endp = 0;
    switch (n) {
        case 0: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        case 4: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n"
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        case 5: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        case 6: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        case 8: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        case 10: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        case 14: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword marks */
            "lhu   %0, 0x1120($t0)\n"
            "sw    %4, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a0($t0)\n"   /* MADR2, alone */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t2, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "bne   $t2, %3, 1b\n"
            "nop\n"
            "lhu   %1, 0x1120($t0)\n"
            "move  $t1, $t3\n"
            "sw    $t1, %2\n"
            ".set pop\n" : "=&r"(t0), "=&r"(t9), "=m"(endp) : "r"(endAddr), "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "memory"); break;
        default: break;
    }
    *tout = (uint16_t)(t9 - t0);
    return (endp - 0x1F800000u) / 2;
}
static void one(int a, uint32_t n, int rep) {
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
    uint16_t t;
    const uint32_t turns = poll(0x01000201u, n, (start + WORDS * 4) & 0xFFFFFCu, &t);
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    for (uint32_t i = 0; i < turns && i < 510; i++) marks[i] = *(volatile uint16_t*)(0x1F800000u + 2 * i);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("S%d %u %d %u %u: %u", a, (unsigned)n, rep, (unsigned)turns, (unsigned)t, (unsigned)((uint16_t)(marks[0] - (uint16_t)start) / (4 * a)));
    for (uint32_t i = 1; i < turns && i < 510; i++) printf(" %u", (unsigned)((uint16_t)(marks[i] - marks[i - 1]) / (4 * a)));
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-a0poll4\n");
    printf("S kind n rep turns t: then MADR2's moves turn to turn in blocks (one MADR2 read a turn, n nops). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one(4, 0, 0);   /* cache the code */
    static const int A[2] = { 4, 8 };
    static const uint32_t NS[7] = { 0, 4, 5, 6, 8, 10, 14 };
    for (int k = 0; k < 2; k++) for (int i = 0; i < 7; i++) for (int r = 0; r < 2; r++) one(A[k], NS[i], r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0poll4: Done.\n");
    for (;;) VSync(0);
}
