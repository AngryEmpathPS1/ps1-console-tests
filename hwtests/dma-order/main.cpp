/*
 * dma/dma-order - what in a GPU DMA's preparation makes a narrow rectangle's rows cost it (2026-11-01)
 *
 * WHY. On one SCPH-5501, 2,048 words into 64x64 by blocks of 4 take 7,314 cycles under dma/chopping and 7,510 under hwtests/dma-disp, which is 7,320 for
 * a wide rectangle; the display and the place in VRAM were not it. The two prepare the transfer differently: chopping resets the command buffer
 * first, sends the rectangle, then sets the DMA direction, and has every other channel off in DPCR, and starts at once.
 *
 * WHAT IT DOES. Interrupts off, no disc, display off, Timer 2 at the system clock. Up, RAM to VRAM, 64x64, by blocks of 4, 8 and 16 words, with each
 * of four things either way: bit 1 GP1(01h) before the rectangle, bit 2 GP1(04h) after it rather than before, bit 4 DPCR with only channel 2
 * enabled, bit 8 no 6,000 cycle wait before the start. Three transfers of each, timed from CHCR's write by a tight poll on CHCR.
 *
 * READ IT LIKE THIS. L var bs ba t0 t1 t2. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-order --frames=3000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u
#define I_STAT   0x1F801070u
#define DPCR     0x1F8010F0u
#define DICR     0x1F8010F4u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define POLLS    60000

static uint32_t buf[2048 + 64] __attribute__((aligned(16)));
static int timeouts;

static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

__attribute__((noinline)) static uint32_t timedC(uint32_t chcr) {
    uint32_t p = 0;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && p < POLLS) p++;
    const uint16_t t1 = read16(T2_COUNT);
    return (uint16_t)(t1 - t0);
}

static void leftOver() {
    uint32_t n = 0;
    waitT2(3000);
    while (!(read32(GP1) & (1u << 26)) && n < 4096) { write32(GP0, 0); n++; for (volatile int i = 0; i < 8; i++) {} }
}

static void gpuReady() { for (int i = 0; i < 30000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); write32(GP1, 0x01000000u); write32(GP1, 0x02000000u); }

static uint32_t one(int v, uint32_t bs, uint32_t ba, uint32_t dp0) {
    if (v & 1) write32(GP1, 0x01000000u);
    gpuReady();
    if (!(v & 2)) write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u);
    write32(GP0, (256u << 16) | 0);
    write32(GP0, (64u << 16) | 64u);
    if (v & 2) write32(GP1, 0x04000002u);
    write32(DPCR, (v & 4) ? (dp0 & 0x00000F00u) | (1u << 11) : dp0 | (1u << 11));
    write32(DICR, 0x7F000000u);
    write32(I_STAT, ~8u);
    write32(MADR2, (uint32_t)&buf[0] & 0xFFFFFCu);
    write32(BCR2, (ba << 16) | bs);
    if (!(v & 8)) waitT2(6000);
    const uint32_t t = timedC(0x01000201u);
    leftOver();
    return t;
}

int main() {
    static const uint32_t sizes[3] = { 4, 8, 16 };
    printf("\ndma/dma-order\n");
    printf("L var bs ba t0 t1 t2. No assertions.\n\n");
    for (int i = 0; i < 2048 + 64; i++) buf[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    for (int v = 0; v < 16; v++)
        for (int b = 0; b < 3; b++) {
            const uint32_t bs = sizes[b], ba = 2048 / bs;
            const uint32_t t0 = one(v, bs, ba, dp0), t1 = one(v, bs, ba, dp0), t2 = one(v, bs, ba, dp0);
            printf("L %d %u %u %u %u %u\n", v, bs, ba, t0, t1, t2);
        }
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write32(DICR, di0 & 0x00FFFFFFu);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ndma/dma-order: Done.\n", timeouts);
    for (;;) VSync(0);
}
