/*
 * gpu/gpu-dma-rfpace - a GPU upload's own pace, the CPU kept off the bus: between reads of BCR2's block count the CPU spins in cached
 * code for K turns, so most blocks run with nothing of the CPU's beside them - what the DRAM refresh alone does to an upload
 * (2026-11-05, for the SCPH-1001 and the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-stoiter2 on the second SCPH-5501: with the CPU reading Timer 2 and BCR2 back to back (no stores) the blocks come
 * every 27.39 cycles where this emulator's come every 27.07, and the iterations a DRAM refresh lands in are 25% of the console's against
 * 20% here, with lengths (+5) this emulator rarely makes. So the refresh costs an upload more on the console - or the reads do. Spacing
 * the reads out separates the two: the pace against the spacing, extrapolated to no reads, is the refresh's own share.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. gpu-dma-stoiter2's upload - 8,192 words by blocks of 16 into a rectangle 128
 * halfwords wide (512 blocks) - and after the CHCR2 store, iterations of: K turns of a cached three-instruction loop (`addiu` / `bnez` / `nop`, ~3K
 * cycles, no bus access), `lhu` Timer 2 and `lw` BCR2 into the scratchpad, loop while the block count is not 0 - at most 250 iterations.
 * K 0, 25, 50, 100, 200, 400. Two tries at two places of the DRAM refresh. A line per try: I K try n, then t:b for every iteration - t
 * its end in cycles from the Timer 2 read before the CHCR2 store, b the blocks still to go as read. (The format of gpu-dma-stoiter2's,
 * K in W's place: experiments/stoiter2score.js reads both.)
 *
 * READ IT LIKE THIS. Blocks done against t: the slope is the cycles a block. At K 0 it is stoiter2's W 0; as K grows the reads thin out
 * and the slope goes to the upload's own pace with the refresh. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rfpace --frames=3000 --quiet=1500`.
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
static const uint32_t KS[6] = { 0, 25, 50, 100, 200, 400 };

static void one(int ki, int tr, uint32_t p, int print) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / 128) << 16) | 128u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((WORDS / 16) << 16) | 16u);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint32_t n = itK(0x01000201u, KS[ki]);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!print) return;
    printf("I %u %d %u", (unsigned)KS[ki], tr, (unsigned)n);
    for (uint32_t i = 1; i <= MAXIT && i <= n; i++) printf(" %u:%u", (unsigned)(uint16_t)(SPAD[i] - SPAD[0]), (unsigned)SPADB[i]);
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-dma-rfpace\n");
    printf("I K try n t:b ... (iterations of K turns of a cached loop, then a Timer 2 read and a BCR2 read into the scratchpad, during an 8,192-word upload by blocks of 16 into a 128-wide rectangle; n the iterations, t each one's end in cycles from the kick's Timer 2 read, b the blocks still to go). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int ki = 0; ki < 6; ki++) one(ki, 0, 0, 0);   /* cache the code (not printed) */
    for (int ki = 0; ki < 6; ki++) for (int tr = 0; tr < 2; tr++) one(ki, tr, (uint32_t)tr * 128u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rfpace: Done.\n");
    for (;;) VSync(0);
}
