/*
 * gpu/gpu-dma-rfpace2 - the upload's pace by the rectangle's row width and the display (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-rfpace on the second SCPH-5501: gpu-dma-stoiter2's upload (8,192 words by blocks of 16 into a rectangle 64 words
 * wide) comes 27.25-27.46 cycles a block at every spacing of the CPU's reads, this emulator 26.97-27.05 - about 28 cycles a line - where
 * the SCPH-1001 reads 27.23-27.35 and v0.377.284's line-start freeze takes this core there; the 5501 has no such freeze (hwtests/
 * gpu-a0stall). gpu-a0mode's 16-word blocks into a 1,024-wide rectangle are the 5501's to ~8 cycles in every display. Two things differ:
 * rfpace's rows start every four blocks (the new GPU's row-start stall, A0_START_STALL), and it sets no display (the runner's 512x240:
 * three scan-out refills a line). This takes the same upload at four row widths in two displays.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. 8,192 words by blocks of 16 into a rectangle W halfwords wide (32, 128, 512,
 * 1,024: rows of 16, 64, 256, 512 words) at (0,256), after the CHCR2 store iterations of K = 200 turns of a cached three-instruction loop,
 * a Timer 2 read and a BCR2 read into the scratchpad (rfpace's), while the block count is not 0. D 0: the display as the test finds it
 * (the runner's); D 1: 256x240 NTSC 15-bit set by the test (one refill a line). Two tries at two places of the DRAM refresh. A line per
 * try: I D-W try n, then t:b for every iteration (rfpace's format: experiments/rfpacescore.js reads it, D-W in K's place).
 *
 * READ IT LIKE THIS. The pace (cycles a block, the slope of t against the blocks gone) by W and D: a row-start cost moves it with W, the
 * scan-out's refills with D. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rfpace2 --frames=4000 --quiet=1500`.
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
static const uint32_t WS[4] = { 32, 128, 512, 1024 };
static void one(int d, int wi, int tr, uint32_t p, int print) {
    const uint32_t w = WS[wi];
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / w) << 16) | w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((WORDS / 16) << 16) | 16u);
    for (int i = 0; i < 4000; i++) { const uint32_t dd = (p - read16(T2_COUNT)) & 0xFFu; if (dd < 8) break; }
    const uint32_t n = itK(0x01000201u, 200);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!print) return;
    printf("I %d-%u %d %u", d, (unsigned)w, tr, (unsigned)n);
    for (uint32_t i = 1; i <= MAXIT && i <= n; i++) printf(" %u:%u", (unsigned)(uint16_t)(SPAD[i] - SPAD[0]), (unsigned)SPADB[i]);
    printf("\n");
}
int main() {
    printf("\ngpu/gpu-dma-rfpace2\n");
    printf("I D-W try n t:b ... (8,192 words by blocks of 16 into a rectangle W halfwords wide; iterations of 200 cached turns, then a Timer 2 "
           "and a BCR2 read into the scratchpad; D 0 the display as found, 1 256x240 set here; t from the Timer 2 read before the kick, b the "
           "blocks still to go). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int wi = 0; wi < 4; wi++) one(0, wi, 0, 0, 0);   /* cache the code (not printed) */
    for (int d = 0; d < 2; d++) {
        if (d == 1) {   /* 256x240, NTSC, 15-bit, the reset's ranges */
            write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);
            write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
            write32(GP1, 0x08000000u);
        }
        for (int wi = 0; wi < 4; wi++) for (int tr = 0; tr < 2; tr++) one(d, wi, tr, (uint32_t)tr * 128u, 1);
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-rfpace2: Done.\n");
    for (;;) VSync(0);
}
