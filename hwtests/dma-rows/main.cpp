/*
 * dma/dma-rows - what a GPU DMA pays for the rectangle's rows (2026-11-01)
 *
 * WHY. hwtests/dma-len: 2,048 words into a rectangle 64 pixels wide run about 0.07 cycles a word past our emulator with the CPU out of the way, by any
 * block of 4 to 32 words, and hwtests/fuzz-dma's one-row rectangles do not. A word or a row? The same transfer into rectangles of other widths says.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. 2,048 words by blocks of 1, 2, 4, 8 and 16 words, RAM to VRAM and VRAM to RAM,
 * into rectangles 8 to 1,024 pixels wide by powers of two, 4,096 pixels each. Three transfers of a shape, timed from CHCR's write by a poll with 40
 * cached nops between reads, the bus left to the DMA. Then what the transfer left, counted by hand as in hwtests/dma-len.
 *
 * READ IT LIKE THIS. L kind bs ba w h t0 t1 t2 l0 l1 l2. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-rows --frames=3000 --quiet=2500`.
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
static uint32_t lastL;
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

static uint32_t leftOver(int kind) {
    uint32_t n = 0;
    waitT2(3000);
    if (kind == 0) { while (!(read32(GP1) & (1u << 26)) && n < 4096) { write32(GP0, 0); n++; for (volatile int i = 0; i < 8; i++) {} } }
    else { while ((read32(GP1) & (1u << 27)) && n < 4096) { (void)read32(GP0); n++; } }
    return n;
}

static void gpuReady() { for (int i = 0; i < 30000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); write32(GP1, 0x01000000u); write32(GP1, 0x02000000u); }

static uint32_t one(int kind, uint32_t bs, uint32_t ba, uint32_t w, uint32_t h) {
    gpuReady();
    write32(GP1, 0x04000000u | (kind == 0 ? 2u : 3u));
    write32(GP0, kind == 0 ? 0xA0000000u : 0xC0000000u);
    write32(GP0, (0u << 16) | 0);
    write32(GP0, (h << 16) | w);
    if (kind == 1) for (int i = 0; i < 40000; i++) if (read32(GP1) & (1u << 27)) break;
    write32(DICR, 0x7F000000u);
    write32(I_STAT, ~8u);
    write32(MADR2, (uint32_t)&buf[0] & 0xFFFFFCu);
    write32(BCR2, (ba << 16) | bs);
    waitT2(6000);
    const uint32_t t = timedS(0x01000200u | (kind == 0 ? 1u : 0u));
    lastL = leftOver(kind);
    return t;
}

int main() {
    static const uint32_t widths[8] = { 8, 16, 32, 64, 128, 256, 512, 1024 }, sizes[5] = { 1, 2, 4, 8, 16 };
    printf("\ndma/dma-rows\n");
    printf("L kind bs ba w h t0 t1 t2 l0 l1 l2. No assertions.\n\n");
    for (int i = 0; i < 2048 + 64; i++) buf[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    write32(DPCR, dp0 | (1u << 11));
    for (int k = 0; k < 2; k++)
        for (int b = 0; b < 5; b++)
            for (int c = 0; c < 8; c++) {
                const uint32_t w = widths[c], T = 2048, h = T * 2 / w, bs = sizes[b], ba = T / bs;
                const uint32_t t0 = one(k, bs, ba, w, h), l0 = lastL;
                const uint32_t t1 = one(k, bs, ba, w, h), l1 = lastL;
                const uint32_t t2 = one(k, bs, ba, w, h), l2 = lastL;
                printf("L %d %u %u %u %u %u %u %u %u %u %u\n", k, bs, ba, w, h, t0, t1, t2, l0, l1, l2);
            }
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write32(DICR, di0 & 0x00FFFFFFu);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ndma/dma-rows: Done.\n", timeouts);
    for (;;) VSync(0);
}
