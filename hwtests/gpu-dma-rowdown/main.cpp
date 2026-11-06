/*
 * gpu/gpu-dma-rowdown - a GPU-to-RAM transfer's blocks across main RAM's 1 KB rows (2026-11-06 small hours, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-ramrow and gpu-dma-rowburst on the second SCPH-5501: a GPU upload's SyncMode 1 block whose words cross a 1 KB
 * row of main RAM costs ~4 cycles (v0.377.286), a burst 4 reading RAM and DMA6 5 writing it (v0.377.287). The other way - VRAM to RAM
 * by SyncMode 1 blocks, the RAM side a write - is not measured, and the emulator gives it nothing (its read-back is paced by the GPU's
 * 32-word read buffer, a word every 2.06 cycles past 12 words a block: hwtests/dma-len, v0.377.245).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. GP0(C0h) of a rectangle at (0,256) 128 halfwords wide, 8,192 words, read
 * by DMA2 to RAM (GP1(04h)=3, CHCR2 01000200h) by blocks of 16 into a buffer that starts O words (0-15) past a 1 KB row boundary -
 * every 16th block then crosses a row at its word 16 - O (O 0: between blocks) - and after the CHCR2 store iterations of K turns of a
 * cached loop (K 0 / 200), a Timer 2 read and a BCR2 read into the scratchpad, while the block count is not 0. Two tries at two places
 * of the DRAM refresh. A line per try: I O-K try n, then t:b for every iteration (gpu-dma-ramrow's format).
 *
 * READ IT LIKE THIS. The pace (cycles a block) by O and K against O 0: a crossing's cost on a write into RAM, once in 16 blocks, if the
 * GPU's pace does not hide it. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rowdown --frames=6000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define WORDS 8192
#define MAXIT 250
#define SPAD ((volatile uint16_t *)0x1F800000u)    /* Timer 2: [0] before the CHCR2 store, [i] at iteration i's end */
#define SPADB ((volatile uint16_t *)0x1F800200u)   /* the block count read at iteration i's end, [i] */

static uint32_t big[WORDS + 512] __attribute__((aligned(1024)));   /* the source starts O words past a 1 KB row */
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the kick, then iterations of K turns of a cached loop + a Timer 2 read and a BCR2 read (both into the scratchpad), while the count is
 * not 0 and under MAXIT. The pointers' addiu sit in the loads' delay slots (gpu-dma-stoiter's slip: a store there sees the old value). */
static __attribute__((noinline, noclone)) uint32_t itK(uint32_t chcr, uint32_t k) {
    uint32_t n;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "move  $t6, $t0\n"
        "addiu $t9, $t0, 0x202\n"
        "li    $t8, 250\n"
        "move  %0, $zero\n"
        "lhu   $t5, 0x1120($t0)\n"
        "addiu $t6, $t6, 2\n"
        "sh    $t5, -2($t6)\n"
        "sw    %1, 0x10a8($t0)\n"
        "1:\n"
        "move  $t3, %2\n"
        "beqz  $t3, 3f\n"
        "nop\n"
        "4:\n"
        "addiu $t3, $t3, -1\n"
        "bnez  $t3, 4b\n"
        "nop\n"
        "3:\n"
        "lhu   $t5, 0x1120($t0)\n"
        "addiu $t6, $t6, 2\n"
        "sh    $t5, -2($t6)\n"
        "lw    $t7, 0x10a4($t0)\n"
        "addiu %0, %0, 1\n"
        "srl   $t7, $t7, 16\n"
        "sh    $t7, 0($t9)\n"
        "addiu $t9, $t9, 2\n"
        "beqz  $t7, 2f\n"
        "nop\n"
        "bne   %0, $t8, 1b\n"
        "nop\n"
        "2:\n"
        ".set pop\n"
        : "=&r"(n)
        : "r"(chcr), "r"(k)
        : "t0", "t3", "t5", "t6", "t7", "t8", "t9", "memory");
    return n;
}
static const uint32_t KS[2] = { 0, 200 };

static void one(uint32_t o, int ki, int tr, uint32_t p, int print) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000003u);   /* VRAM to CPU */
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / 128) << 16) | 128u);
    write32(MADR2, (uint32_t)&big[o] & 0xFFFFFCu);
    write32(BCR2, ((WORDS / 16) << 16) | 16u);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint32_t n = itK(0x01000200u, KS[ki]);   /* to RAM, SyncMode 1 */
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!print) return;
    printf("I %u-%u %d %u", (unsigned)o, (unsigned)KS[ki], tr, (unsigned)n);
    for (uint32_t i = 1; i <= MAXIT && i <= n; i++) printf(" %u:%u", (unsigned)(uint16_t)(SPAD[i] - SPAD[0]), (unsigned)SPADB[i]);
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-dma-rowdown\n");
    printf("I O-K try n t:b ... (8,192 words of GP0(C0h) by DMA to RAM by blocks of 16 into a buffer O words past a 1 KB row boundary; "
           "iterations of K cached turns, then a Timer 2 and a BCR2 read into the scratchpad; t from the Timer 2 read before the kick, b the "
           "blocks still to go). No assertions.\n\n");
    for (int i = 0; i < WORDS + 512; i++) big[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int ki = 0; ki < 2; ki++) one(0, ki, 0, 0, 0);   /* cache the code (not printed) */
    for (uint32_t o = 0; o < 16; o++) for (int ki = 0; ki < 2; ki++) for (int tr = 0; tr < 2; tr++) one(o, ki, tr, (uint32_t)tr * 128u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rowdown: Done.\n");
    for (;;) VSync(0);
}
