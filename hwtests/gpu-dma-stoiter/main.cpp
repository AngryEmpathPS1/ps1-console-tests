/*
 * gpu/gpu-dma-stoiter - dma-contention's STORES loop timed iteration by iteration: W stores to consecutive RAM words, a Timer 2 read
 * kept in the scratchpad, a CHCR2 read - during an upload into a 128-wide rectangle by blocks of 16 (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/dma-contention's STORES row (64 stores to consecutive words, then a CHCR read, each iteration, until the upload ends)
 * runs 18 iterations in 15,542 cycles on most consoles; this emulator with the read path's knobs (experiments/_mkrdf.js, STATUS "The
 * read path by the commit rule") 19 in 15,506 - the total the console's, each iteration ~47 cycles short - while gpu-dma-storow's same
 * rows (64 words, 4 blocks a row) drain the console's 2.30 stores a block. So what an iteration costs past its stores - the CHCR read
 * after them, the buffer's state when it issues - is what differs. This times each.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. An 8,192-word SyncMode 1 upload by blocks of 16 into a rectangle 128
 * halfwords wide (dma-contention's 128x128 but twice the height); after the CHCR2 store, iterations of W stores (`sw` to cell[0..W-1],
 * W 1, 2, 4, 8, 16, 64), `lhu` Timer 2 into the scratchpad (no bus), a CHCR2 read, loop while busy - at most 500 iterations. Two tries
 * at two places of the DRAM refresh. A line per try: I W try n d1 d2 ... d24 (n the iterations; d the cycles of each of the first 24,
 * from the Timer 2 read before the CHCR2 store).
 *
 * READ IT LIKE THIS. The steady d at each W against the blocks' 29.7 cycles: the iteration's stores and its read, block by block.
 * No assertions - compare against your emulator. A slip (found 2026-11-05 writing gpu-dma-stoiter2): each Timer 2 value is stored in
 * its load's delay slot, so the store sees the register's OLD value - d2..d24 are the durations of iterations 1..23 and d1 means
 * nothing. The same binary runs on a console and an emulator, so the comparison holds; gpu-dma-stoiter2 stores it after the slot.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-stoiter --frames=3000 --quiet=1500`.
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
#define SPAD ((volatile uint16_t *)0x1F800000u)

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static volatile uint32_t cell[64] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the kick, then iterations of W stores + a Timer 2 read into the scratchpad + a CHCR2 read, while busy and under 500 */
#define ITW(W) \
static __attribute__((noinline, noclone)) uint32_t it##W(uint32_t chcr) { \
    uint32_t n; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "move  $t6, $t0\n" \
        "li    $t8, 500\n" \
        "move  %0, $zero\n" \
        "lhu   $t5, 0x1120($t0)\n" \
        "sh    $t5, 0($t6)\n" \
        "addiu $t6, $t6, 2\n" \
        "sw    %1, 0x10a8($t0)\n" \
        "1:\n" \
        ".set noat\n" \
        "move  $at, %2\n" \
        ".rept " #W "\nsw %0, 0($at)\naddiu $at, $at, 4\n.endr\n" \
        ".set at\n" \
        "lhu   $t5, 0x1120($t0)\n" \
        "sh    $t5, 0($t6)\n" \
        "addiu $t6, $t6, 2\n" \
        "lw    $t7, 0x10a8($t0)\n" \
        "addiu %0, %0, 1\n" \
        "srl   $t7, $t7, 24\n" \
        "andi  $t7, $t7, 1\n" \
        "beqz  $t7, 2f\n" \
        "nop\n" \
        "bne   %0, $t8, 1b\n" \
        "nop\n" \
        "2:\n" \
        ".set pop\n" \
        : "=&r"(n) \
        : "r"(chcr), "r"(cell) \
        : "t0", "t5", "t6", "t7", "t8", "memory"); \
    return n; \
}
ITW(1) ITW(2) ITW(4) ITW(8) ITW(16) ITW(64)
typedef uint32_t (*Itf)(uint32_t);
static const Itf ITF[6] = { it1, it2, it4, it8, it16, it64 };
static const uint32_t WS[6] = { 1, 2, 4, 8, 16, 64 };

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
    for (uint32_t i = 1; i <= 24 && i <= n; i++) printf(" %u", (unsigned)(uint16_t)(SPAD[i] - SPAD[i - 1]));
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-dma-stoiter\n");
    printf("I W try n d1 .. d24 (iterations of W stores to consecutive RAM words, a Timer 2 read into the scratchpad and a CHCR2 read during an 8,192-word upload by blocks of 16 into a 128-wide rectangle; n the iterations, d each one's cycles). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int wi = 0; wi < 6; wi++) one(wi, 0, 0, 0);   /* cache the code (not printed) */
    for (int wi = 0; wi < 6; wi++) for (int tr = 0; tr < 2; tr++) one(wi, tr, (uint32_t)tr * 128u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-stoiter: Done.\n");
    for (;;) VSync(0);
}
