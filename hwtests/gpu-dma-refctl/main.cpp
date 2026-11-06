/*
 * gpu/gpu-dma-refctl - an upload's pace against the DRAM controller's refresh controls, 1F801060h bits 0-2 (refresh hold-off during
 * DMA) and 4-5 (refresh period), by row width (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-rfpace2 on the second SCPH-5501: the new GPU's upload pays ~1 cycle at every rectangle row start at any width
 * (16-word rows +1.02 a block, 64-word +0.28), where this emulator's row-start cycle is hidden at 64-word rows by refreshes it moves
 * into the gaps between blocks (cheap there, +10 inside a block). Which of the two costs is the console's is the refresh's, not the
 * GPU's, to say: the live psx-spx (DRAM_CTRL, added 2026-09-26) documents bits 0-2 as how long a refresh may wait while the controller
 * handles DMA (0 = it yields at once; 1..6 = 4 << (N-1) cycles; 7 = unlimited) and bits 4-5 as the refresh period (256 + 64 N
 * cycles). Changing the period separates the pace's refresh share from its row-start share; changing the hold-off says whether a
 * refresh may wait for a gap.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. gpu-dma-rfpace2's upload (8,192 words by blocks of 16, iterations of 200
 * cached turns, then a Timer 2 and a BCR2 read into the scratchpad) into rectangles 32, 128 and 1,024 halfwords wide (rows of 16, 64,
 * 512 words), for hold-off H 0, 3 (16 cycles) and 6 (128 cycles) and period P 0-3 (256, 320, 384, 448 cycles): 1F801060h read, its
 * bits 0-2 and 4-5 replaced (every other bit kept), the upload, the register written back - so nothing else runs with the change. 7
 * (unlimited) is left out. Two tries at two places of the refresh. A line per try: I H-P-W try n t:b ... (rfpace's format:
 * experiments/rfpacescore.js reads it), and once at the start the register as found.
 *
 * READ IT LIKE THIS. The pace (cycles a block) by P at each W and H: the refresh's share falls as 1/period, the row start's does not;
 * by H at each W: a hold-off that lets refreshes wait for gaps moves the in-block cost out. No assertions - compare against your
 * emulator (which does not model these bits: every H and P read alike there).
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-refctl --frames=6000 --quiet=1500`.
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
#define DRAM_CTRL 0x1F801060u
static const uint32_t WS[3] = { 32, 128, 1024 };
static const uint32_t HS[3] = { 0, 3, 6 };
static void one(uint32_t h, uint32_t pc, int wi, int tr, uint32_t p, int print, uint32_t saved) {
    const uint32_t w = WS[wi];
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / w) << 16) | w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((WORDS / 16) << 16) | 16u);
    write32(DRAM_CTRL, (saved & ~0x37u) | (h & 7u) | ((pc & 3u) << 4));
    for (int i = 0; i < 4000; i++) { const uint32_t dd = (p - read16(T2_COUNT)) & 0xFFu; if (dd < 8) break; }
    const uint32_t n = itK(0x01000201u, 200);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    write32(DRAM_CTRL, saved);
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!print) return;
    printf("I %u-%u-%u %d %u", (unsigned)h, (unsigned)pc, (unsigned)w, tr, (unsigned)n);
    for (uint32_t i = 1; i <= MAXIT && i <= n; i++) printf(" %u:%u", (unsigned)(uint16_t)(SPAD[i] - SPAD[0]), (unsigned)SPADB[i]);
    printf("\n");
}
int main() {
    printf("\ngpu/gpu-dma-refctl\n");
    printf("I H-P-W try n t:b ... (8,192 words by blocks of 16 into a rectangle W halfwords wide, 1F801060h bits 0-2 = H (refresh hold-off "
           "during DMA) and 4-5 = P (refresh period 256 + 64 P) for the upload only; iterations of 200 cached turns, then a Timer 2 and a "
           "BCR2 read; t from the Timer 2 read before the kick, b the blocks still to go). No assertions.\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint32_t saved = read32(DRAM_CTRL);
    printf("DRAM_CTRL as found: %08x\n\n", (unsigned)saved);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int wi = 0; wi < 3; wi++) one(0, 0, wi, 0, 0, 0, saved);   /* cache the code (not printed) */
    for (int hi = 0; hi < 3; hi++) for (uint32_t pc = 0; pc < 4; pc++) for (int wi = 0; wi < 3; wi++) for (int tr = 0; tr < 2; tr++)
        one(HS[hi], pc, wi, tr, (uint32_t)tr * 128u, 1, saved);
    write32(DRAM_CTRL, saved);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDRAM_CTRL at the end: %08x\n", (unsigned)read32(DRAM_CTRL));
    printf("\ngpu/gpu-dma-refctl: Done.\n");
    for (;;) VSync(0);
}
