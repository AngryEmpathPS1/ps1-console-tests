/*
 * dma/dma-len - how long a GPU DMA stays busy against its length, with the code that times it warm and cold (2026-11-01)
 *
 * WHY. Our emulator holds Busy 91 cycles past a GPU transfer's last block, a number fitted on dma/chopping's totals, 2,048 words a row, timed by
 * code that has just been pushed out of the cache. hwtests/fuzz-dma, 240 words at most, reads about 100 cycles less on two consoles, and with the
 * 91 taken out chopping reads 50 short. So either the transfer's end depends on its length, or chopping's cold code is what took the 91's place.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. For each of 16, 64, 256, 1,024 and 2,048 words, by blocks of 1..128 (sync 1)
 * and in one burst (sync 0, printed as block size 0), RAM to VRAM and VRAM to RAM, into a rectangle 64 pixels wide as chopping's: three transfers
 * of one shape, the first with the timing code just evicted from the instruction cache, the next two warm. Each is timed from CHCR's write to the
 * end, seen two ways: polling CHCR's busy bit, and polling I_STAT's DMA bit, with DICR set for channel 2. A wait of 6,000 cycles stands between
 * the GPU's command and the start in all three. The whole table runs four times.
 *
 * READ IT LIKE THIS. L rep kind how bs ba w h t0 t1 t2 p0 p1 p2 e: kind 0 up, 1 down; how 0 CHCR, 1 I_STAT; t cold, warm, warm; p the polls; e of
 * the last one: how 0 I_STAT's bit 3 straight after, how 1 CHCR's busy bit straight after. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-len --frames=3000 --quiet=2500`.
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
#define REPS     4

extern "C" void dlEvict(void);
__asm__(".set noreorder\n.text\n.align 4\n.globl dlEvict\ndlEvict:\n.rept 1040\nnop\n.endr\njr $ra\nnop\n.set reorder\n");

static uint32_t buf[2048 + 64] __attribute__((aligned(16)));
static uint32_t lastP, lastE;
static int timeouts;

__attribute__((noinline)) static uint32_t timedC(uint32_t chcr) {
    uint32_t p = 0;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && p < POLLS) p++;
    const uint16_t t1 = read16(T2_COUNT);
    lastP = p; lastE = read32(I_STAT) & 8u;
    return (uint16_t)(t1 - t0);
}
__attribute__((noinline)) static uint32_t timedI(uint32_t chcr) {
    uint32_t p = 0;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    while (!(read32(I_STAT) & 8u) && p < POLLS) p++;
    const uint16_t t1 = read16(T2_COUNT);
    lastP = p; lastE = (read32(CHCR2) >> 24) & 1u;
    return (uint16_t)(t1 - t0);
}

static void gpuReady() { for (int i = 0; i < 400000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); }

static uint32_t one(int kind, int how, uint32_t bs, uint32_t ba, uint32_t w, uint32_t h, int cold) {
    const uint32_t words = bs ? bs * ba : ba;
    gpuReady();
    write32(GP1, 0x04000000u | (kind == 0 ? 2u : 3u));
    write32(GP0, kind == 0 ? 0xA0000000u : 0xC0000000u);
    write32(GP0, (256u << 16) | 0);
    write32(GP0, (h << 16) | w);
    if (kind == 1) for (int i = 0; i < 40000; i++) if (read32(GP1) & (1u << 27)) break;
    write32(DICR, 0x7F800000u | (1u << 18));
    write32(I_STAT, ~8u);
    write32(MADR2, (uint32_t)&buf[0] & 0xFFFFFCu);
    write32(BCR2, bs ? (ba << 16) | bs : words);
    const uint16_t s = read16(T2_COUNT);
    if (cold) dlEvict();
    while ((uint16_t)(read16(T2_COUNT) - s) < 6000) {}
    const uint32_t chcr = (bs ? 0x01000200u : 0x11000000u) | (kind == 0 ? 1u : 0u);
    const uint32_t t = how ? timedI(chcr) : timedC(chcr);
    if (lastP >= POLLS) { timeouts++; write32(CHCR2, 0); }
    write32(DICR, 0x7F000000u);
    write32(I_STAT, ~8u);
    return t;
}

int main() {
    static const uint32_t lens[5] = { 16, 64, 256, 1024, 2048 }, sizes[9] = { 0, 1, 2, 4, 8, 16, 32, 64, 128 };
    printf("\ndma/dma-len\n");
    printf("L rep kind how bs ba w h t0 t1 t2 p0 p1 p2 e, cold, warm, warm. No assertions.\n\n");
    for (int i = 0; i < 2048 + 64; i++) buf[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    write32(DPCR, dp0 | (1u << 11));
    for (int r = 0; r < REPS; r++)
        for (int k = 0; k < 2; k++)
            for (int how = 0; how < 2; how++)
                for (int l = 0; l < 5; l++)
                    for (int b = 0; b < 9; b++) {
                        const uint32_t T = lens[l], bs = sizes[b];
                        if (bs > T) continue;
                        const uint32_t ba = bs ? T / bs : T, w = T * 2 < 64 ? T * 2 : 64, h = T * 2 / w;
                        const uint32_t t0 = one(k, how, bs, ba, w, h, 1), p0 = lastP;
                        const uint32_t t1 = one(k, how, bs, ba, w, h, 0), p1 = lastP;
                        const uint32_t t2 = one(k, how, bs, ba, w, h, 0), p2 = lastP;
                        printf("L %d %d %d %u %u %u %u %u %u %u %u %u %u %u\n", r, k, how, bs, ba, w, h, t0, t1, t2, p0, p1, p2, lastE);
                    }
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write32(DICR, di0 & 0x00FFFFFFu);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ndma/dma-len: Done.\n", timeouts);
    for (;;) VSync(0);
}
