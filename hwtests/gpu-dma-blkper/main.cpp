/*
 * gpu/gpu-dma-blkper - an upload's blocks one by one, by row width and by the refresh's place (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-refctl on the second SCPH-5501: at the stock DRAM_CTRL (0B88h) an upload pays the new GPU's row-start cycle in full
 * at every width (pace = 26.92 + ~1.0 a row start: 27.98 / 27.23 / 26.95 at rows of 16 / 64 / 512 words), and with a refresh hold-off of
 * 16 or 128 cycles the row starts cost less (~0.7 / ~0.3 a row) - refreshes that may wait fill the row-start gaps. This emulator hides
 * the row start at 64-word rows (its refreshes land in gaps more often there and cost little). Which refreshes land where on the
 * console, and what each costs, the blocks one by one can say (an outside analysis's suggestion).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. gpu-dma-rfpace2's upload (8,192 words by blocks of 16) into rectangles 32,
 * 128 and 1,024 halfwords wide (rows of 16, 64, 512 words), the CPU reading Timer 2 and BCR2 back to back after the CHCR2 store (rfpace's
 * loop with K 0: every iteration a block or two), at 16 places of the DRAM refresh (the start on Timer 2's count mod 256 = 16 k). A line
 * per place: I W-p 0 n t:b ... (rfpace's format: t the iteration's end from the Timer 2 read before the kick, b the blocks still to go).
 *
 * READ IT LIKE THIS. The iterations' lengths: two blocks' worth normally, +5..+10 where a refresh lands, +1 after a row start. By place
 * and width: where each refresh lands (in a block, in a gap) and what it costs. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-blkper --frames=6000 --quiet=1500`.
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
static const uint32_t WS[3] = { 32, 128, 1024 };
static void one(int d, int wi, int tr, uint32_t p, int print) {
    const uint32_t w = WS[wi];
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / w) << 16) | w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((WORDS / 16) << 16) | 16u);
    for (int i = 0; i < 4000; i++) { const uint32_t dd = (p - read16(T2_COUNT)) & 0xFFu; if (dd < 8) break; }
    const uint32_t n = itK(0x01000201u, 0);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!print) return;
    printf("I %u-%u 0 %u", (unsigned)w, (unsigned)p, (unsigned)n);
    for (uint32_t i = 1; i <= MAXIT && i <= n; i++) printf(" %u:%u", (unsigned)(uint16_t)(SPAD[i] - SPAD[0]), (unsigned)SPADB[i]);
    printf("\n");
}
int main() {
    printf("\ngpu/gpu-dma-blkper\n");
    printf("I W-p 0 n t:b ... (8,192 words by blocks of 16 into a rectangle W halfwords wide, the CPU reading Timer 2 and BCR2 back to "
           "back; p the start place on the refresh grid; t from the Timer 2 read before the kick, b the blocks still to go). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int wi = 0; wi < 3; wi++) one(0, wi, 0, 0, 0);   /* cache the code (not printed) */
    for (int wi = 0; wi < 3; wi++) for (uint32_t k = 0; k < 16; k++) one(0, wi, 0, k * 16u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-blkper: Done.\n");
    for (;;) VSync(0);
}
