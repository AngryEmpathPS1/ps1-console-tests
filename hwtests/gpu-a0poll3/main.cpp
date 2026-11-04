/*
 * gpu/gpu-a0poll3 - gpu-a0poll's lone-read loop with every read timed: each turn a CHCR2 read, a Timer 2 read right behind it (the
 * second of a back-to-back pair, never held: gpu-dma-pair, gpu-dma-hold), its value stored in the scratchpad, n nops; 4- and 8-word
 * SyncMode 1 uploads (2026-11-04, for the SCPH-5501). Written by experiments/mka0poll3.js.
 *
 * WHY. hwtests/gpu-a0poll on the second SCPH-5501: one CHCR2 read a turn locks to the blocks while the loop is quicker than them, drags
 * them when it is a little slower, and runs at its own period beyond (10 + n cycles a turn from n = 10) with the upload unslowed -
 * where gpu-dma-hold holds a single read 11 cycles of a 4-word block's 14, and this emulator locks the loop to two blocks a turn (each
 * read issued a cycle into a block and held 8). This reads the loop turn by turn.
 *
 * WHAT IT DOES. gpu-a0poll's set-up (2,048 words into 1,024x4 at (0,256), vblank's first hblank edge + 500 cycles); one asm block per
 * n: Timer 2, CHCR2's store, the loop (510 stamps at most); then the stamps.
 *
 * READ IT LIKE THIS. S kind n rep turns: then each turn's period (stamp minus the one before), the first stamp after the store. No
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
static uint16_t stamps[512];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}
static __attribute__((noinline, noclone)) uint32_t poll(uint32_t chcr, uint32_t n, uint16_t* t0out) {
    uint32_t t0 = 0, endp = 0;
    switch (n) {
        case 0: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        case 4: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n"
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        case 5: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        case 6: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        case 8: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        case 10: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        case 14: __asm__ volatile(".set push\n.set noreorder\n"
            "lui   $t0, 0x1f80\n"
            "lui   $t3, 0x1f80\n"        /* the scratchpad, 1F800000h */
            "addiu $t4, $t3, 1020\n"     /* 510 halfword stamps */
            "lui   $t6, 0x0100\n"
            "lhu   %0, 0x1120($t0)\n"
            "sw    %2, 0x10a8($t0)\n"    /* CHCR2: start */
            "1:\n"
            "lw    $t2, 0x10a8($t0)\n"   /* CHCR2 */
            "lhu   $t1, 0x1120($t0)\n"   /* Timer 2 right behind it */
            "sltu  $t5, $t3, $t4\n"
            "beqz  $t5, 2f\n"
            "nop\n"
            "sh    $t1, 0($t3)\n"
            "addiu $t3, $t3, 2\n"
            "2:\n"
            "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n" "nop\n"
            "and   $t2, $t2, $t6\n"
            "bnez  $t2, 1b\n"
            "nop\n"
            "move  %1, $t3\n"
            ".set pop\n" : "=&r"(t0), "=&r"(endp) : "r"(chcr) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory"); break;
        default: break;
    }
    *t0out = (uint16_t)t0;
    return (endp - 0x1F800000u) / 2;
}
static void one(int a, uint32_t n, int rep) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / a) << 16) | (uint32_t)a);
    toPlace();
    uint16_t t0;
    const uint32_t turns = poll(0x01000201u, n, &t0);
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    for (uint32_t i = 0; i < turns && i < 510; i++) stamps[i] = *(volatile uint16_t*)(0x1F800000u + 2 * i);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("S%d %u %d %u: %u", a, (unsigned)n, rep, (unsigned)turns, (unsigned)(uint16_t)(stamps[0] - t0));
    for (uint32_t i = 1; i < turns && i < 510; i++) printf(" %u", (unsigned)(uint16_t)(stamps[i] - stamps[i - 1]));
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-a0poll3\n");
    printf("S kind n rep turns: the first stamp after the store, then each turn's period (a CHCR2 read, Timer 2 right behind it, n nops). No assertions.\n\n");
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
    printf("\ngpu/gpu-a0poll3: Done.\n");
    for (;;) VSync(0);
}
