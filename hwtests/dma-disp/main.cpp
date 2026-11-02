/*
 * dma/dma-disp - a GPU DMA with the display on and off (2026-11-01)
 *
 * WHY. hwtests/dma-rows, display off: a rectangle's row costs a block of 4 or more about 3 cycles. dma/chopping on the same console, display on at
 * 640x480, the same 64x64: none of it, by 200 cycles over 2,048 words. Either the display or the rectangle's place in VRAM makes the difference.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. Up, RAM to VRAM, by blocks of 1, 4, 8 and 16 words: 2,048 words into 64x64 at
 * (0,0), at (448,0), at (64,64) and at (512,256), into 1024x4 at (0,0), and 256 words into 64x8; with the display off, on at 320x240 and on at
 * 640x480 interlaced. Three transfers of a shape, timed from CHCR's write by a poll with 40 cached nops between reads, the bus left to the DMA, and
 * the same six shapes timed in a tight poll on CHCR.
 *
 * READ IT LIKE THIS. L disp bs ba x y w h poll t0 t1 t2. disp 0 off, 1 320x240, 2 640x480i; poll 0 slowed, 1 tight. No assertions - compare against
 * your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-disp --frames=3000 --quiet=2500`.
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

__attribute__((noinline)) static uint32_t timedS(uint32_t chcr) {
    uint32_t p = 0;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && p < POLLS) { p++; __asm__ volatile(".rept 40\nnop\n.endr\n"); }
    const uint16_t t1 = read16(T2_COUNT);
    return (uint16_t)(t1 - t0);
}
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

static uint32_t one(uint32_t bs, uint32_t ba, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int poll) {
    gpuReady();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u);
    write32(GP0, (y << 16) | x);
    write32(GP0, (h << 16) | w);
    write32(DICR, 0x7F000000u);
    write32(I_STAT, ~8u);
    write32(MADR2, (uint32_t)&buf[0] & 0xFFFFFCu);
    write32(BCR2, (ba << 16) | bs);
    waitT2(6000);
    const uint32_t t = poll ? timedC(0x01000201u) : timedS(0x01000201u);
    leftOver();
    return t;
}

int main() {
    static const uint32_t sizes[4] = { 1, 4, 8, 16 };
    static const uint16_t shapes[6][5] = { { 2048, 0, 0, 64, 64 }, { 2048, 448, 0, 64, 64 }, { 2048, 64, 64, 64, 64 }, { 2048, 512, 256, 64, 64 }, { 2048, 0, 0, 1024, 4 }, { 256, 0, 0, 64, 8 } };
    printf("\ndma/dma-disp\n");
    printf("L disp bs ba x y w h poll t0 t1 t2. No assertions.\n\n");
    for (int i = 0; i < 2048 + 64; i++) buf[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(DPCR, dp0 | (1u << 11));
    for (int d = 0; d < 3; d++) {
        write32(GP1, 0);
        if (d == 0) write32(GP1, 0x03000001u);
        else { write32(GP1, d == 1 ? 0x08000001u : 0x08000027u); write32(GP1, 0x06C60260u); write32(GP1, d == 1 ? 0x07040010u : 0x07048010u); write32(GP1, 0x05000000u); write32(GP1, 0x03000000u); }
        waitT2(40000);
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 6; c++)
                for (int poll = 0; poll < 2; poll++) {
                    const uint32_t T = shapes[c][0], x = shapes[c][1], y = shapes[c][2], w = shapes[c][3], h = shapes[c][4], bs = sizes[b], ba = T / bs;
                    const uint32_t t0 = one(bs, ba, x, y, w, h, poll), t1 = one(bs, ba, x, y, w, h, poll), t2 = one(bs, ba, x, y, w, h, poll);
                    printf("L %d %u %u %u %u %u %u %d %u %u %u\n", d, bs, ba, x, y, w, h, poll, t0, t1, t2);
                }
    }
    write32(GP1, 0x03000001u);
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write32(DICR, di0 & 0x00FFFFFFu);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ndma/dma-disp: Done.\n", timeouts);
    for (;;) VSync(0);
}
