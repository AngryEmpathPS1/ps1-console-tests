/*
 * dma/dma-setup - dma/chopping's set-up, a piece at a time, under a transfer that shows what it changes (2026-11-01)
 *
 * WHY. hwtests/dma-order found nothing in the steps before a transfer. What is left between dma/chopping's 7,314 cycles and our 7,480 for the same
 * 2,048 words into 64x64 by blocks of 4, on one console, is what chopping does long before it: the library's video set-up (ResetGraph, a display
 * and a drawing environment at 640x480), a screen clear, random pixels, a buffer from malloc.
 *
 * WHAT IT DOES. Timer 2 at the system clock, interrupts off for the timing. Up, RAM to VRAM, 64x64 at (192,0), by blocks of 4 and 16 words, a tight poll
 * on CHCR, three times. Pass 0 with GP1 resets as our other tests; pass 1 after the library's set-up; pass 2 after the screen clear as well. In each,
 * the pixels our pattern, random as chopping's, all 0, all FFFFh, from a static buffer and from malloc's. And one row with GP0(E1h) set by hand.
 *
 * READ IT LIKE THIS. L pass data buffer bs ba t0 t1 t2. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-setup --frames=3000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <malloc.h>
#include <twister.h>
#include <io.h>
#include <gpu.h>

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

static uint32_t sbuf[2048 + 64] __attribute__((aligned(16)));
static uint8_t* hbuf;
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

static uint32_t one(uint32_t addr, uint32_t bs, uint32_t ba) {
    gpuReady();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u);
    write32(GP0, (0u << 16) | 192u);
    write32(GP0, (64u << 16) | 64u);
    write32(DICR, 0x7F000000u);
    write32(I_STAT, ~8u);
    write32(MADR2, addr & 0xFFFFFCu);
    write32(BCR2, (ba << 16) | bs);
    waitT2(6000);
    const uint32_t t = timedC(0x01000201u);
    leftOver();
    return t;
}

static void fill(uint8_t* b, int data) {
    for (int i = 0; i < 8192; i += 2) {
        const uint32_t v = data == 0 ? 0x9E3779B9u * (uint32_t)(i + 1) : data == 1 ? randomMT() : data == 2 ? 0 : 0xFFFFFFFFu;
        b[i] = v & 0xFF; b[i + 1] = (v >> 8) & 0xFF;
    }
}

static void table(int pass) {
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR);
    write16(T2_MODE, 0);
    write32(DPCR, dp0 | (1u << 11));
    for (int d = 0; d < 4; d++)
        for (int h = 0; h < 2; h++)
            for (int b = 0; b < 2; b++) {
                uint8_t* p = h ? hbuf : (uint8_t*)sbuf;
                fill(p, d);
                const uint32_t bs = b ? 16 : 4, ba = 2048 / bs, a = (uint32_t)p;
                const uint32_t t0 = one(a, bs, ba), t1 = one(a, bs, ba), t2 = one(a, bs, ba);
                printf("L %d %d %d %u %u %u %u %u\n", pass, d, h, bs, ba, t0, t1, t2);
            }
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
}

int main() {
    printf("\ndma/dma-setup\n");
    printf("L pass data buffer bs ba t0 t1 t2. No assertions.\n\n");
    seedMT(1337);
    hbuf = (uint8_t*)malloc(8192);
    printf("buffers %x %x\n", (uint32_t)sbuf, (uint32_t)hbuf);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    table(0);
    initVideo(640, 480);
    table(1);
    clearScreen();
    table(2);
    printf("\n%d timeouts\ndma/dma-setup: Done.\n", timeouts);
    for (;;) VSync(0);
}
