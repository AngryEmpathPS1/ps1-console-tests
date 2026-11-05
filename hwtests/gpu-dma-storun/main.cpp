/*
 * gpu/gpu-dma-storun - dma-arb's ST unit (sw to RAM; addiu; bnez; nop) run a fixed number of times during a GPU upload, k nops more a
 * unit: how many blocks the DMA moves meanwhile, so what dense RAM stores cost the blocks (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/dma-arb's ST0 / ST2 rows (one sw to RAM a unit, 0 / 2 addu after it, 512 blocks of 16 words) end 517 / 806 cycles
 * earlier here than on every console and run 15 / 19 turns where the consoles run 16-17 / 21-22. hwtests/gpu-dma-iorun showed its IOR
 * rows were the turn's quantisation, not a cost (the units' reads cost the console's blocks what they cost here), and gpu-dma-turn
 * that one RAM store a turn costs the same on both. Dense stores fill the write buffer; this times the blocks under them directly.
 *
 * WHAT IT DOES. gpu-dma-iorun's instrument with the unit `sw t4, 0(cell); addiu; k nops; bnez; nop` (k 0..8): the CHCR2 store, R units
 * (300 at 16-word blocks, 600 at 4-word, well inside the 8,192-word upload), then one BCR2 read (the blocks left) and a Timer 2 read;
 * then CHCR2 polled to the end (a 40-nop poll). Each cell 4 times, at four places of the DRAM refresh. A line: U bs k try t left end.
 *
 * READ IT LIKE THIS. Blocks moved during the units = total - left; the block period under them = t / moved; units a block = R / moved.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-storun --frames=3000 --quiet=1500`.
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
static volatile uint32_t cell[64] __attribute__((aligned(16)));   /* the stores' target, as dma-arb's ramCell */
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the units: R turns of sw t4, 0(cell); addiu; K nops; bnez; nop - then BCR2 and Timer 2 */
#define STK(K) \
static __attribute__((noinline, noclone)) uint32_t st##K(uint32_t r, uint32_t chcr, uint32_t *left) { \
    uint32_t t0, t1, b; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "move  $t4, %3\n" \
        "lhu   %0, 0x1120($t0)\n" \
        "sw    %4, 0x10a8($t0)\n" \
        "1:\n" \
        "sw    $t4, 0(%5)\n" \
        "addiu $t4, $t4, -1\n" \
        ".rept " #K "\nnop\n.endr\n" \
        "bnez  $t4, 1b\n" \
        "nop\n" \
        "lw    %2, 0x10a4($t0)\n" \
        "lhu   %1, 0x1120($t0)\n" \
        "nop\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1), "=&r"(b) \
        : "r"(r), "r"(chcr), "r"(cell) \
        : "t0", "t4", "memory"); \
    *left = b >> 16; return (uint16_t)(t1 - t0); \
}
STK(0) STK(1) STK(2) STK(3) STK(4) STK(5) STK(6) STK(7) STK(8)
typedef uint32_t (*Stk)(uint32_t, uint32_t, uint32_t *);
static const Stk STF[9] = { st0, st1, st2, st3, st4, st5, st6, st7, st8 };

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
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint16_t s0 = read16(T2_COUNT);
    uint32_t left = 0;
    const uint32_t t = STF[k](r, 0x01000201u, &left);
    const uint16_t e = pollEnd();
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (print) printf("U %u %u %d %u %u %u\n", (unsigned)bs, (unsigned)k, tr, (unsigned)t, (unsigned)left, (unsigned)(uint16_t)(e - s0));
}

int main() {
    printf("\ngpu/gpu-dma-storun\n");
    printf("U bs k try t left end (R units of sw to RAM; addiu; k nops; bnez; nop during a SyncMode 1 upload of 8,192 words; t their cycles, left the blocks BCR2 held after them, end the upload's time to its poll). No assertions.\n\n");
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
    printf("\ngpu/gpu-dma-storun: Done.\n");
    for (;;) VSync(0);
}
