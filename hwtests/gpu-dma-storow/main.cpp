/*
 * gpu/gpu-dma-storow - hwtests/gpu-dma-storun's dense store unit during uploads to VRAM rows of five widths, beside a unit with no store:
 * does the gap's third store come where a copy's row ends? (2026-11-05, for the SCPH-5501.)
 *
 * WHY. gpu-dma-storun on the second SCPH-5501: dense RAM stores during an upload by 16-word blocks drain 2.11-2.18 a block at every
 * spacing - one gap in seven or eight takes a third store - where 4-word blocks drain exactly 2.00-2.03. Its copy is 256 halfwords
 * wide: 8 blocks of 16 words a row, 32 of 4. If the third store comes where the GPU's request waits at a row's end, the fraction over
 * two tracks a block's share of a row: 1/2, 1/4, 1/8, 1/16, 1/32 at widths 64, 128, 256, 512, 1024.
 *
 * WHAT IT DOES. storun's instrument: the CHCR2 store, R units, then one BCR2 read (the blocks left) and a Timer 2 read, then CHCR2
 * polled to the end. Unit S: `sw t4, 0(cell); addiu; bnez; nop` (storun's k 0; R 300 at 16-word blocks, 600 at 4-word). Unit N: the
 * same with a nop for the sw (R 1000 / 2000: the blocks' own pace under a running CPU). An 8,192-word upload by blocks of 16 and 4 to
 * a rectangle W halfwords wide (64 128 256 512 1024), four tries at four places of the DRAM refresh. A line: U bs W unit try t left end.
 *
 * READ IT LIKE THIS. Blocks moved during the units = total - left; the block period = t / moved; S's stores a block = R / moved.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-storow --frames=3000 --quiet=1500`.
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

/* unit S: R turns of sw t4, 0(cell); addiu; bnez; nop - then BCR2 and Timer 2 */
static __attribute__((noinline, noclone)) uint32_t unitS(uint32_t r, uint32_t chcr, uint32_t *left) {
    uint32_t t0, t1, b;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "move  $t4, %3\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "sw    $t4, 0(%5)\n"
        "addiu $t4, $t4, -1\n"
        "bnez  $t4, 1b\n"
        "nop\n"
        "lw    %2, 0x10a4($t0)\n"
        "lhu   %1, 0x1120($t0)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t1), "=&r"(b)
        : "r"(r), "r"(chcr), "r"(cell)
        : "t0", "t4", "memory");
    *left = b >> 16; return (uint16_t)(t1 - t0);
}
/* unit N: the same with a nop for the store */
static __attribute__((noinline, noclone)) uint32_t unitN(uint32_t r, uint32_t chcr, uint32_t *left) {
    uint32_t t0, t1, b;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "move  $t4, %3\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %4, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "addiu $t4, $t4, -1\n"
        "bnez  $t4, 1b\n"
        "nop\n"
        "lw    %2, 0x10a4($t0)\n"
        "lhu   %1, 0x1120($t0)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t1), "=&r"(b)
        : "r"(r), "r"(chcr), "r"(cell)
        : "t0", "t4", "memory");
    *left = b >> 16; return (uint16_t)(t1 - t0);
}

static uint16_t pollEnd() {
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) { __asm__ volatile(".rept 40\nnop\n.endr\n"); }
    return read16(T2_COUNT);
}

static void one(uint32_t bs, uint32_t w, int unit, int tr, uint32_t p, int print) {
    const uint32_t blocks = WORDS / bs, r = unit ? (bs == 16 ? 1000u : 2000u) : (bs == 16 ? 300u : 600u), h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (h << 16) | w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (blocks << 16) | bs);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint16_t s0 = read16(T2_COUNT);
    uint32_t left = 0;
    const uint32_t t = unit ? unitN(r, 0x01000201u, &left) : unitS(r, 0x01000201u, &left);
    const uint16_t e = pollEnd();
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (print) printf("U %u %u %c %d %u %u %u\n", (unsigned)bs, (unsigned)w, unit ? 'N' : 'S', tr, (unsigned)t, (unsigned)left, (unsigned)(uint16_t)(e - s0));
}

int main() {
    printf("\ngpu/gpu-dma-storow\n");
    printf("U bs W unit try t left end (R units - S: sw to RAM; addiu; bnez; nop, N: the same with a nop for the sw - during a SyncMode 1 upload of 8,192 words to a rectangle W halfwords wide; t their cycles, left the blocks BCR2 held after them, end the upload's time to its poll). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one(16, 256, 0, 0, 0, 0); one(16, 256, 1, 0, 0, 0);   /* cache the code (not printed) */
    static const uint32_t BS[2] = { 16, 4 }, W[5] = { 64, 128, 256, 512, 1024 };
    for (int b = 0; b < 2; b++) for (int wi = 0; wi < 5; wi++) for (int u = 0; u < 2; u++) for (int tr = 0; tr < 4; tr++)
        one(BS[b], W[wi], u, tr, (uint32_t)tr * 64u, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-storow: Done.\n");
    for (;;) VSync(0);
}
