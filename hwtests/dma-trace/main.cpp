/*
 * dma/dma-trace - a GPU DMA followed as it runs: when MADR moved, block by block (2026-11-01)
 *
 * WHY. hwtests/dma-len has the totals: 2,048 words by blocks of 4 and up run 55 to 157 cycles past our emulator with the CPU out of the way, and
 * the shortfall grows with the DRAM refreshes the transfer meets. A total cannot say which block paid, or how much. The trail can: every refresh
 * and every scanline's pause stands out as one long step, and the steps' sizes are the model.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. 2,048 words, RAM to VRAM and VRAM to RAM, by blocks of 4, 8, 16, 32 (up
 * only), each shape twice. The CPU, in cached code, reads Timer 2 and MADR in turn and each time MADR has moved stores (words moved, Timer 2) in the
 * scratchpad, which never touches the bus: 255 steps at most. Afterwards the steps are printed.
 *
 * READ IT LIKE THIS. S rep kind bs ba n: then R lines of words:dt pairs, dt the cycles since the last move, the first since CHCR's write. P the poll's
 * own loop on an idle channel, for how often it can look. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-trace --frames=3000 --quiet=2500`.
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
#define PAD      ((volatile uint32_t*)0x1F800000u)
#define SLOTS    255

static uint32_t buf[2048 + 64] __attribute__((aligned(16)));
static int timeouts;

static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

__attribute__((noinline)) static uint32_t trace(uint32_t chcr, uint32_t start, uint32_t end) {
    uint32_t n = 0, last = start;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    for (;;) {
        const uint16_t t = read16(T2_COUNT);
        const uint32_t m = read32(MADR2) & 0xFFFFFFu;
        if (m != last) {
            last = m;
            if (n < SLOTS) PAD[n++] = (((m - start) >> 2) << 16) | (uint16_t)(t - t0);
            if (m == end) break;
        }
        if ((uint16_t)(t - t0) > 40000) { timeouts++; break; }
    }
    return n;
}

static void leftOver(int kind) {
    uint32_t n = 0;
    waitT2(3000);
    if (kind == 0) { while (!(read32(GP1) & (1u << 26)) && n < 4096) { write32(GP0, 0); n++; for (volatile int i = 0; i < 8; i++) {} } }
    else { while ((read32(GP1) & (1u << 27)) && n < 4096) { (void)read32(GP0); n++; } }
}

static void gpuReady() { for (int i = 0; i < 30000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); write32(GP1, 0x01000000u); write32(GP1, 0x02000000u); }

int main() {
    static const uint32_t sizes[4] = { 4, 8, 16, 32 };
    printf("\ndma/dma-trace\n");
    printf("S rep kind bs ba n, then R lines of words:dt. No assertions.\n\n");
    for (int i = 0; i < 2048 + 64; i++) buf[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(GP1, 0x03000001u);
    write32(DPCR, dp0 | (1u << 11));
    {   /* the poll on an idle channel */
        uint32_t n = 0, last = 0;
        const uint16_t t0 = read16(T2_COUNT);
        for (int i = 0; i < 16; i++) { const uint16_t t = read16(T2_COUNT); const uint32_t m = read32(MADR2) & 0xFFFFFFu; if (m != last) { last = m; n++; } }
        printf("P %u %u\n", (uint16_t)(read16(T2_COUNT) - t0), n);
    }
    for (int r = 0; r < 2; r++)
        for (int k = 0; k < 2; k++)
            for (int b = 0; b < 4; b++) {
                const uint32_t bs = sizes[b], ba = 2048 / bs, w = 64, h = 64;
                if (k == 1 && bs > 16) continue;
                gpuReady();
                write32(GP1, 0x04000000u | (k == 0 ? 2u : 3u));
                write32(GP0, k == 0 ? 0xA0000000u : 0xC0000000u);
                write32(GP0, (256u << 16) | 0);
                write32(GP0, (h << 16) | w);
                if (k == 1) for (int i = 0; i < 40000; i++) if (read32(GP1) & (1u << 27)) break;
                write32(DICR, 0x7F000000u);
                write32(I_STAT, ~8u);
                const uint32_t start = (uint32_t)&buf[0] & 0xFFFFFCu;
                write32(MADR2, start);
                write32(BCR2, (ba << 16) | bs);
                waitT2(6000);
                const uint32_t n = trace(0x01000200u | (k == 0 ? 1u : 0u), start, (start + 2048 * 4) & 0xFFFFFCu);
                static uint32_t copy[SLOTS];
                for (uint32_t i = 0; i < n; i++) copy[i] = PAD[i];
                leftOver(k);
                printf("S %d %d %u %u %u\n", r, k, bs, ba, n);
                uint32_t pt = 0;
                for (uint32_t i = 0; i < n; i += 12) {
                    printf("R");
                    for (uint32_t j = i; j < i + 12 && j < n; j++) { const uint32_t t = copy[j] & 0xFFFFu; printf(" %u:%u", copy[j] >> 16, t - pt); pt = t; }
                    printf("\n");
                }
            }
    write32(GP1, 0x04000000u);
    write32(DPCR, dp0);
    write32(DICR, di0 & 0x00FFFFFFu);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n%d timeouts\ndma/dma-trace: Done.\n", timeouts);
    for (;;) VSync(0);
}
