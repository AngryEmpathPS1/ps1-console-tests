/*
 * gpu/gpu-dma-span - a SyncMode 1 upload's length against its block count: the fixed cost of a transfer apart from each block's
 * (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-pace on the second SCPH-5501: this emulator's 2,048-word uploads run ~5-15 cycles long at every block size
 * (1-16 words: 128 to 2,048 blocks) and in every place and row width, polled or (with the intake's events on their own time,
 * experiments/_mkfnow279.js) quiet. A cost that does not grow with the block count is the transfer's start or end, and
 * hwtests/gpu-dma-size's single blocks match. This measures uploads of K blocks, K 1 to 512, so a line through them separates the
 * two: its intercept the transfer's own cost, its slope a block's.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting hblanks. K blocks of 4 or 16 words (K 1, 2, 4, 8, 16,
 * 32, 64, 128, 256, 512) by GP0(A0h) at (0,256) into rows of 256 pixels (the rectangle exactly the words: h = words / 128), started
 * 500 cycles after vblank's first hblank edge. P - polled by gpu-a0poll's mode C loop at n 0; Q - the CPU in registers W cycles,
 * then one CHCR2 read, W by halving to 2 cycles. Twice each. A line: S bs K rep P t Q W.
 *
 * READ IT LIKE THIS. Fit Q (and P) against K for each bs on each machine: the slopes are the blocks' pace, the intercepts the
 * transfer's own cost; this emulator against the console. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-span --frames=8000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
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
static uint32_t per100;
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void spin(uint32_t n) {
    if (!n) return;
    __asm__ volatile(".set noreorder\n1: addiu %0, %0, -1\n bnez %0, 1b\n nop\n.set reorder" : "+r"(n) : : "memory");
}
static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    for (int i = 0; i < 200000 && read16(T1_COUNT) == e; i++) {}
    waitT2(500);
}

/* gpu-a0poll's pollC at n = 0 (the jr lands on the count), as gpu-dma-pace */
static __attribute__((noinline, noclone)) uint32_t pollC0(uint32_t chcr) {
    uint32_t t0, t9, turns;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "la    $t3, 9f\n"
        "lui   $t6, 0x0100\n"
        "move  %2, $zero\n"
        "lui   $t5, 0x1\n"
        "lhu   %0, 0x1120($t0)\n"
        "sw    %3, 0x10a8($t0)\n"
        "1:\n"
        "nop\n"
        "lw    $t2, 0x10a8($t0)\n"
        "jr    $t3\n"
        "addiu %2, %2, 1\n"
        "9:\n"
        "beq   %2, $t5, 8f\n"
        "and   $t2, $t2, $t6\n"
        "bnez  $t2, 1b\n"
        "nop\n"
        "8:\n"
        "lhu   %1, 0x1120($t0)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t9), "=&r"(turns)
        : "r"(chcr)
        : "t0", "t2", "t3", "t5", "t6", "memory");
    return (uint16_t)(t9 - t0);
}

static void setup(uint32_t bs, uint32_t k) {
    const uint32_t words = bs * k, h = (words * 2 + 255) / 256;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (h << 16) | (words * 2 >= 256 ? 256u : words * 2));
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (k << 16) | bs);
    toPlace();
}
static void finish() {
    for (int i = 0; i < 400000 && (read32(CHCR2) & 0x01000000u); i++) {}
    waitT2(1000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int z = 0; z < 8; z++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
}

static void cell(uint32_t bs, uint32_t k, int rep, int print) {
    setup(bs, k);
    const uint32_t p = pollC0(0x01000201u);
    finish();
    uint32_t lo = p > 400 ? p - 400 : 0, hi = p + 200;   /* done at hi, busy at lo */
    while (hi - lo > 2) {
        const uint32_t mid = (lo + hi) / 2;
        setup(bs, k);
        write32(CHCR2, 0x01000201u);
        spin(mid * 100u / per100);
        const int busy = (read32(CHCR2) & 0x01000000u) != 0;
        finish();
        if (busy) lo = mid; else hi = mid;
    }
    if (print) printf("S %u %u %d P %u Q %u\n", (unsigned)bs, (unsigned)k, rep, (unsigned)p, (unsigned)hi);
}

int main() {
    printf("\ngpu/gpu-dma-span\n");
    printf("S bs K rep P t Q W (K SyncMode 1 blocks of bs words into rows of 256 pixels in vblank; P polled by one CHCR2 read a turn, Q the CPU off the bus W cycles then one read, the first W done). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    const uint16_t s0 = read16(T2_COUNT); spin(10000); const uint16_t s1 = read16(T2_COUNT);
    per100 = (uint32_t)(uint16_t)(s1 - s0) / 100u;
    printf("spin: %u cycles a 100 turns\n", (unsigned)per100);
    cell(4, 16, 0, 0);   /* cache the code */
    static const uint32_t BS[2] = { 4, 16 };
    for (int b = 0; b < 2; b++) for (uint32_t k = 1; k <= 512; k *= 2) for (int r = 0; r < 2; r++) cell(BS[b], k, r, 1);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-span: Done.\n");
    for (;;) VSync(0);
}
