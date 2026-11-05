/*
 * gpu/gpu-dma-iorun - dma-arb's IOR unit (lw DPCR; addiu; bnez; nop) run a fixed number of times during a GPU upload, k nops more a
 * unit: how many blocks the DMA moves meanwhile, so what each read costs the blocks and how many reads a block the CPU makes
 * (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/dma-arb's IOR and TMR rows (48 such units a turn, 512 blocks of 16 words) end ~430 cycles later on every console
 * than here and run 11 turns where this emulator runs 10: here the loop locks one read a block - each read held by a block,
 * released a cycle after its end, the next issued 7 cycles on, inside the next block (scratch/mkiortr279.js) - and the blocks never
 * wait. A shadow refund for the read's next instructions (experiments/_mkhr12.js) puts the ends right but runs 13 turns. This times
 * the loop and the blocks directly, by the unit's spacing.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For blocks of 16 (512 blocks) and 4 words (2,048 blocks) into GP0(A0h)
 * at (0,256), 256 wide: the CHCR2 store, then R units of `lw t5, DPCR; addiu; k nops; bnez; nop` (k 0..8; R 300 at 16-word, 600 at
 * 4-word: well inside the upload), then one BCR2 read (the blocks left) and a Timer 2 read; then CHCR2 polled to the end (a 40-nop
 * poll) and Timer 2 again. Each cell 4 times, at four places of the DRAM refresh (Timer 2's count mod 256). A line:
 * U bs k try t left end - t the units' cycles (store to after the BCR2 read), left the blocks BCR2 still held, end the upload's
 * time to the poll that read it done.
 *
 * READ IT LIKE THIS. Blocks moved during the units = total - left; the block period under them = t / moved, against the quiet
 * pace (bs + 10); reads a block = R / moved. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-iorun --frames=3000 --quiet=1500`.
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

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the units: R turns of lw DPCR; addiu; K nops; bnez; nop - then BCR2 and Timer 2. Returns the cycles (16 bits), *left the BCR2 high half */
#define IORK(K) \
static __attribute__((noinline, noclone)) uint32_t ior##K(uint32_t r, uint32_t chcr, uint32_t *left) { \
    uint32_t t0, t1, b; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "move  $t4, %3\n" \
        "lhu   %0, 0x1120($t0)\n" \
        "sw    %4, 0x10a8($t0)\n" \
        "1:\n" \
        "lw    $t5, 0x10f0($t0)\n" \
        "addiu $t4, $t4, -1\n" \
        ".rept " #K "\nnop\n.endr\n" \
        "bnez  $t4, 1b\n" \
        "nop\n" \
        "lw    %2, 0x10a4($t0)\n" \
        "lhu   %1, 0x1120($t0)\n" \
        "nop\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1), "=&r"(b) \
        : "r"(r), "r"(chcr) \
        : "t0", "t4", "t5", "memory"); \
    *left = b >> 16; return (uint16_t)(t1 - t0); \
}
IORK(0) IORK(1) IORK(2) IORK(3) IORK(4) IORK(5) IORK(6) IORK(7) IORK(8)
typedef uint32_t (*Ior)(uint32_t, uint32_t, uint32_t *);
static const Ior IOR[9] = { ior0, ior1, ior2, ior3, ior4, ior5, ior6, ior7, ior8 };

/* 40 cached nops between CHCR2 reads, Timer 2 at the end: bounded */
static uint16_t pollEnd() {
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) { __asm__ volatile(".rept 40\nnop\n.endr\n"); }
    return read16(T2_COUNT);
}

static void one(uint32_t bs, uint32_t k, int tr, uint32_t p, int print) {
    const uint32_t blocks = WORDS / bs, r = bs == 16 ? 300u : 600u;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)(WORDS * 2 / 256) << 16) | 256u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (blocks << 16) | bs);
    /* start at Timer 2's count mod 256 = p (a few cycles' slack: the try's spread is in the log) */
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint16_t s0 = read16(T2_COUNT);
    uint32_t left = 0;
    const uint32_t t = IOR[k](r, 0x01000201u, &left);
    const uint16_t e = pollEnd();
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (print) printf("U %u %u %d %u %u %u\n", (unsigned)bs, (unsigned)k, tr, (unsigned)t, (unsigned)left, (unsigned)(uint16_t)(e - s0));
}

int main() {
    printf("\ngpu/gpu-dma-iorun\n");
    printf("U bs k try t left end (R units of lw DPCR; addiu; k nops; bnez; nop during a SyncMode 1 upload of 8,192 words; t their cycles, left the blocks BCR2 held after them, end the upload's time to its poll). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (uint32_t k = 0; k < 9; k++) one(16, k, 0, 0, 0);   /* cache the code (not printed) */
    static const uint32_t BS[2] = { 16, 4 };
    for (int b = 0; b < 2; b++) for (uint32_t k = 0; k < 9; k++) for (int tr = 0; tr < 4; tr++) one(BS[b], k, tr, (uint32_t)tr * 64u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-iorun: Done.\n");
    for (;;) VSync(0);
}
