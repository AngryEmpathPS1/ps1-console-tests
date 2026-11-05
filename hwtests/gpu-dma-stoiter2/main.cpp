/*
 * gpu/gpu-dma-stoiter2 - gpu-dma-stoiter with the upload's progress read every iteration: W stores, a Timer 2 read, then BCR2's
 * block count (in place of stoiter's CHCR2 read), both kept in the scratchpad - so each iteration says when it ended AND how many
 * blocks the DMA had moved by then (2026-11-05, for the SCPH-5501).
 *
 * WHY. gpu-dma-stoiter on unit 2 against this emulator (v0.377.282): the iteration counts agree within one or two at every W, but the
 * iterations themselves run short here - at W 8 the two kinds of iteration read 112 / 140 cycles where the console reads 114 / 142, at
 * W 16 ~231 against ~238 - a quarter to half a cycle a block, as gpu-dma-storun's 16-word cells read ~0.25 a block short. Either the
 * console's blocks are slower while the CPU's stores fill their gaps (the DMA's pace), or its CPU takes longer for the same blocks (the
 * iteration's place among them). The block count read at each iteration's end tells the two apart: blocks against time is the DMA's
 * pace, blocks per iteration the CPU's share.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. stoiter's upload - 8,192 words by blocks of 16 into a rectangle 128
 * halfwords wide (512 blocks) - and after the CHCR2 store, iterations of W stores (`sw` to cell[0..W-1], W 0, 1, 2, 4, 8, 16, 64), `lhu`
 * Timer 2 into the scratchpad, `lw` BCR2 and its block count (bits 16-31: SyncMode 1 counts it down to 0) into the scratchpad, loop
 * while it is not 0 - at most 250 iterations. Two tries at two places of the DRAM refresh. A line per try: I W try n, then for every
 * iteration t:b - t its end in cycles from the Timer 2 read before the CHCR2 store, b the blocks still to go as read.
 *
 * READ IT LIKE THIS. Blocks done (512 - b) against t is the DMA's pace (its slope the cycles a block, W 0 the pace with two reads an
 * iteration and no stores); blocks per iteration against W is what an iteration's stores cost in blocks. No assertions - compare
 * against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-stoiter2 --frames=3000 --quiet=1500`.
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

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static volatile uint32_t cell[64] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the kick, then iterations of W stores + a Timer 2 read into the scratchpad + a BCR2 read (its block count into the scratchpad),
 * while the count is not 0 and under MAXIT. The pointer's addiu sits in each Timer 2 load's delay slot: stoiter stored there, so its
 * store saw $t5's OLD value - each d of stoiter's is the iteration before's, its d1 meaningless (found writing this one). */
#define ITW(W) \
static __attribute__((noinline, noclone)) uint32_t it##W(uint32_t chcr) { \
    uint32_t n; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "move  $t6, $t0\n" \
        "addiu $t9, $t0, 0x202\n" \
        "li    $t8, 250\n" \
        "move  %0, $zero\n" \
        "lhu   $t5, 0x1120($t0)\n" \
        "addiu $t6, $t6, 2\n" \
        "sh    $t5, -2($t6)\n" \
        "sw    %1, 0x10a8($t0)\n" \
        "1:\n" \
        ".set noat\n" \
        "move  $at, %2\n" \
        ".rept " #W "\nsw %0, 0($at)\naddiu $at, $at, 4\n.endr\n" \
        ".set at\n" \
        "lhu   $t5, 0x1120($t0)\n" \
        "addiu $t6, $t6, 2\n" \
        "sh    $t5, -2($t6)\n" \
        "lw    $t7, 0x10a4($t0)\n" \
        "addiu %0, %0, 1\n" \
        "srl   $t7, $t7, 16\n" \
        "sh    $t7, 0($t9)\n" \
        "addiu $t9, $t9, 2\n" \
        "beqz  $t7, 2f\n" \
        "nop\n" \
        "bne   %0, $t8, 1b\n" \
        "nop\n" \
        "2:\n" \
        ".set pop\n" \
        : "=&r"(n) \
        : "r"(chcr), "r"(cell) \
        : "t0", "t5", "t6", "t7", "t8", "t9", "memory"); \
    return n; \
}
ITW(0) ITW(1) ITW(2) ITW(4) ITW(8) ITW(16) ITW(64)
typedef uint32_t (*Itf)(uint32_t);
static const Itf ITF[7] = { it0, it1, it2, it4, it8, it16, it64 };
static const uint32_t WS[7] = { 0, 1, 2, 4, 8, 16, 64 };

static void one(int wi, int tr, uint32_t p, int print) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / 128) << 16) | 128u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((WORDS / 16) << 16) | 16u);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint32_t n = ITF[wi](0x01000201u);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!print) return;
    printf("I %u %d %u", (unsigned)WS[wi], tr, (unsigned)n);
    for (uint32_t i = 1; i <= MAXIT && i <= n; i++) printf(" %u:%u", (unsigned)(uint16_t)(SPAD[i] - SPAD[0]), (unsigned)SPADB[i]);
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-dma-stoiter2\n");
    printf("I W try n t:b ... (iterations of W stores to consecutive RAM words, a Timer 2 read and a BCR2 read into the scratchpad during an 8,192-word upload by blocks of 16 into a 128-wide rectangle; n the iterations, t each one's end in cycles from the kick's Timer 2 read, b the blocks still to go). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int wi = 0; wi < 7; wi++) one(wi, 0, 0, 0);   /* cache the code (not printed) */
    for (int wi = 0; wi < 7; wi++) for (int tr = 0; tr < 2; tr++) one(wi, tr, (uint32_t)tr * 128u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-stoiter2: Done.\n");
    for (;;) VSync(0);
}
