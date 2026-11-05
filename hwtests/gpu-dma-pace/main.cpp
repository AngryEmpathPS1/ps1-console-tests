/*
 * gpu/gpu-dma-pace - a 2,048-word SyncMode 1 upload's own length, quiet and polled, by block size, by where in the frame it runs
 * and by the rectangle's row width (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-a0pollbs on the second SCPH-5501: the uploads polled by a loop locked to the blocks read 6 / 9 / 12 / 13 / 25
 * cycles longer here than on the console at blocks of 1 / 2 / 4 / 8 / 16 (2,048 words into 1,024x4 at (0,256), in vblank) - the same
 * per-transfer excess gpu-a0rate2 shows (~13 at every block size, 128 words, the visible lines). But hwtests/gpu-dma-iorun's 8,192-word
 * 4-word upload into 256-wide rows ends within 2-4 cycles of the console's. So the excess depends on the place, the rows or the poll.
 * This measures each.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting hblanks. 2,048 words by GP0(A0h) at (0,256), rows of
 * 1,024 or 256 pixels (512 or 128 words), SyncMode 1 blocks of 1, 2, 4, 8, 16, started (V) 500 cycles after vblank's first hblank
 * edge, or (A) 500 cycles after the 100th hblank edge after it (the picture's lines). Each cell: P - polled by gpu-a0poll's mode C
 * loop at n = 0 (one CHCR2 read a turn, locked to the blocks), Timer 2 from before the CHCR2 store to after the loop; Q - the CPU in
 * registers W cycles after the store, then one CHCR2 read: W found by halving to 2 cycles (the first W that reads done). Twice each.
 * A line: C bs place width rep P t Q W.
 *
 * READ IT LIKE THIS. Q is the upload's length (plus the read's fixed offset, the same on every machine); P - Q the poll's cost; Q
 * across place and width says where an upload's pace moves. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-pace --frames=8000 --quiet=1500`.
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
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static uint32_t per100;   /* the spin's cycles a 100 turns, measured */
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* registers only: n turns of a 3-instruction loop (addiu, bnez, nop) */
static void spin(uint32_t n) {
    if (!n) return;
    __asm__ volatile(".set noreorder\n1: addiu %0, %0, -1\n bnez %0, 1b\n nop\n.set reorder" : "+r"(n) : : "memory");
}

/* to the place: vblank's first hblank edge (V) or the 100th edge after it (A), then 500 cycles */
static void toPlace(int act) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    uint16_t e = read16(T1_COUNT);
    for (int i = 0; i < 200000 && read16(T1_COUNT) == e; i++) {}
    if (act) { e = read16(T1_COUNT); for (int i = 0; i < 2000000 && (uint16_t)(read16(T1_COUNT) - e) < 100; i++) {} }
    waitT2(500);
}

/* gpu-a0poll's pollC at n = 0, instruction for instruction (the jr lands on the count) */
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

static void setup(uint32_t bs, uint32_t width, int act) {
    const uint32_t h = WORDS * 2 / width;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (h << 16) | width);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (h << 16) | width);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / bs) << 16) | bs);
    toPlace(act);
}
static void finish() {
    for (int i = 0; i < 400000 && (read32(CHCR2) & 0x01000000u); i++) {}
    waitT2(2000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
}

static void cell(uint32_t bs, int act, uint32_t width, int rep, int print) {
    setup(bs, width, act);
    const uint32_t p = pollC0(0x01000201u);
    finish();
    uint32_t lo = p > 900 ? p - 900 : 0, hi = p + 300;   /* done at hi, busy at lo */
    while (hi - lo > 2) {
        const uint32_t mid = (lo + hi) / 2;
        setup(bs, width, act);
        write32(CHCR2, 0x01000201u);
        spin(mid * 100u / per100);
        const int busy = (read32(CHCR2) & 0x01000000u) != 0;
        finish();
        if (busy) lo = mid; else hi = mid;
    }
    if (print) printf("C %u %c %u %d P %u Q %u\n", (unsigned)bs, act ? 'A' : 'V', (unsigned)width, rep, (unsigned)p, (unsigned)hi);
}

int main() {
    printf("\ngpu/gpu-dma-pace\n");
    printf("C bs place width rep P t Q W (2,048 words by SyncMode 1 blocks into rows of 1,024 or 256 pixels, started in vblank V or the picture's lines A; P polled by one CHCR2 read a turn, Q the CPU off the bus W cycles then one read, the first W done). No assertions.\n\n");
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
    cell(4, 0, 1024, 0, 0);   /* cache the code */
    static const uint32_t BS[5] = { 1, 2, 4, 8, 16 };
    static const uint32_t WD[2] = { 1024, 256 };
    for (int b = 0; b < 5; b++) for (int a = 0; a < 2; a++) for (int w = 0; w < 2; w++) for (int r = 0; r < 2; r++) cell(BS[b], a, WD[w], r, 1);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-pace: Done.\n");
    for (;;) VSync(0);
}
