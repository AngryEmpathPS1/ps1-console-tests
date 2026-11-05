/*
 * gpu/gpu-burstq - gpu-burstline's 4-word upload, polled and quiet: what the CPU's CHCR polling costs the console (2026-11-05)
 *
 * WHY. hwtests/gpu-burstline on the second SCPH-5501: its 4-word-block upload (1,024 words, a 256-wide rectangle, CHCR2 polled the
 * whole time) takes 3,880 cycles there at every place in the line, 3,675 here. gpu-dma-quiet's CPU-off-the-bus 4-word uploads were
 * the console's pace (14 a block and its refreshes) - so is the 205 the polls (dma-arb's ~0.9 a read held, CURRENT's item 1), or
 * this upload's own pace? This times the same upload both ways at the same places.
 *
 * WHAT IT DOES. As gpu-burstline: interrupts off, Timer 2 at the system clock, 1,024 words by DMA2 to GP0(A0h) at (0,256), 256 wide,
 * in 4-word blocks, started d cycles after a GPUSTAT bit-31 change (d 0..2,048 by 128). P: CHCR2 polled to the end, its time (as
 * gpu-burstline). Q: the CPU spins in registers only (no load or store) for W cycles after CHCR, then reads CHCR2 once - busy or
 * done - and W is found by halving (3,000-4,600, to 4 cycles): the first W that reads done is the upload's end with no CPU on the
 * bus. A line: P d t / Q d W.
 *
 * READ IT LIKE THIS. Q against P at each d is what the polling costs; Q against 256 x 14 + its refreshes is the pace.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-burstq --frames=3000 --quiet=1500`.
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
#define WORDS 1024

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* registers only: n turns of a 2-instruction loop (addiu, bnez - the delay slot a nop), ~2 cycles a turn in the I-cache */
static void spin(uint32_t n) {
    if (!n) return;
    __asm__ volatile(".set noreorder\n1: addiu %0, %0, -1\n bnez %0, 1b\n nop\n.set reorder" : "+r"(n) : : "memory");
}

/* start the upload d cycles after a line edge; returns with CHCR written (the caller times or waits) */
static void start(uint32_t d) {
    const int w = 256, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / 4) << 16) | 4u);
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
}
static void finish() {
    for (int i = 0; i < 400000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
}

int main() {
    printf("\ngpu/gpu-burstq\n");
    printf("P d t (CHCR2 polled) / Q d W (the CPU off the bus W cycles, then one read: the first done). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    /* the spin's cycles a turn, measured here (Timer 2): turns for a W */
    const uint16_t s0 = read16(T2_COUNT); spin(10000); const uint16_t s1 = read16(T2_COUNT);
    const uint32_t per100 = (uint32_t)(uint16_t)(s1 - s0) / 100u;   /* cycles a 100 turns */
    printf("spin: %u cycles a 100 turns\n", (unsigned)per100);
    for (uint32_t d = 0; d <= 2048; d += 128) {
        start(d);
        const uint16_t t0 = read16(T2_COUNT);
        write32(CHCR2, 0x01000201u);
        uint32_t polls = 0;
        while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
        const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
        finish();
        printf("P %u %u\n", (unsigned)d, (unsigned)t);
        uint32_t lo = 3000, hi = 4600;   /* done at hi, busy at lo (checked below) */
        while (hi - lo > 4) {
            const uint32_t mid = (lo + hi) / 2;
            start(d);
            write32(CHCR2, 0x01000201u);
            spin(mid * 100u / per100);
            const int busy = (read32(CHCR2) & 0x01000000u) != 0;
            finish();
            if (busy) lo = mid; else hi = mid;
        }
        printf("Q %u %u\n", (unsigned)d, (unsigned)hi);
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-burstq: Done.\n");
    for (;;) VSync(0);
}
