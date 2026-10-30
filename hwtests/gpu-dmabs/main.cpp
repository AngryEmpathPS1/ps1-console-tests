/*
 * gpu/gpu-dmabs - a GPU DMA's cost by its block size, down (VRAM -> RAM) and up (RAM -> VRAM) (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-scan6 on the SCPH-7501: a 128x64 download by DMA2 in 16-word blocks takes 8,902-8,991 cycles there and
 * 9,517-9,533 here, while the same upload sits within 0.4% and a CPU-fed download of the same pixels matches to the cycle -
 * so the GPU's read-out is right and the channel's pacing of a DOWNLOAD is not: ~600 cycles over 256 blocks, ~2.4 a block.
 * DMA_GPU_READ (1.5 cycles a word) was fitted to gpu/bandwidth's full-screen download; v0.377.88's block gaps to uploads
 * (hwtests/dma-arb). Whether the difference is a block's or a word's needs the block size varied.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; in the vertical blanking (20,000 cycles after the VBlank IRQ),
 * four times each, the fastest printed: a 128x64 transfer (4,096 words) at (512,300) by DMA2 SyncMode 1 in blocks of 4 /
 * 8 / 16 / 32 / 64 / 128 words, DOWN (GP0(C0h), GPUSTAT bit 27 waited, then CHCR 01000200h) and UP (GP0(A0h), CHCR
 * 01000201h), timed from the first GP0 word to CHCR bit 24 clear, CHCR polled in a loop with Timer 2 (as gpu-scan6).
 * Then the 16-word download with the poll slowed by 64 nops between reads.
 *
 * READ IT LIKE THIS. Cycles against blocks (4,096 / size): the slope a block's cost, the rest the words'; the slowed poll
 * says how much of a gap is the polling CPU's. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dmabs --frames=3000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t buf[4096];

static void toBlank() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
}
/* down: 1 = VRAM -> RAM, 0 = RAM -> VRAM; slow: nops between polls */
static uint32_t one(int down, int bs, int slow) {
    write32(GP1, 0x04000000u | (down ? 3u : 2u));
    toBlank();
    const uint32_t t0 = now();
    write32(GP0, down ? 0xC0000000u : 0xA0000000u); write32(GP0, xy(512, 300)); write32(GP0, xy(128, 64));
    if (down) while (!(read32(GP1) & 0x08000000u) && now() - t0 < 40000000u) {}
    write32(D2_MADR, (uint32_t)buf & 0x1FFFFF);
    write32(D2_BCR, ((uint32_t)(4096 / bs) << 16) | (uint32_t)bs);
    write32(D2_CHCR, down ? 0x01000200u : 0x01000201u);
    if (slow) {
        for (;;) {
            if (!(read32(D2_CHCR) & 0x01000000u)) break;
            if (now() - t0 > 40000000u) break;
            __asm__ volatile(".rept 64\n nop\n .endr");
        }
    } else while ((read32(D2_CHCR) & 0x01000000u) && now() - t0 < 40000000u) {}
    const uint32_t t = now() - t0;
    if (!down) waitReady();
    write32(GP1, 0x04000000u);
    return t;
}
static uint32_t best(int down, int bs, int slow) { uint32_t b = 0xFFFFFFFFu; for (int r = 0; r < 4; r++) { const uint32_t t = one(down, bs, slow); b = t < b ? t : b; } return b; }

int main() {
    printf("\ngpu/gpu-dmabs\n");
    printf("A 128x64 transfer (4,096 words) by DMA2 by block size, in the blanking, fastest of four, cycles to CHCR bit 24\n");
    printf("clear. No assertions.\n\n");
    for (int i = 0; i < 4096; i++) buf[i] = 0x7FFF7FFFu;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE6000000u);
    static const int BS[6] = { 4, 8, 16, 32, 64, 128 };
    ExitCriticalSection();
    printf("block   blocks     down       up\n");
    EnterCriticalSection();
    for (int i = 0; i < 6; i++) {
        const uint32_t d = best(1, BS[i], 0), u = best(0, BS[i], 0);
        ExitCriticalSection();
        printf("%5d   %6d   %6u   %6u\n", BS[i], 4096 / BS[i], (unsigned)d, (unsigned)u);
        EnterCriticalSection();
    }
    const uint32_t ds = best(1, 16, 1), us = best(0, 16, 1);
    write32(GP1, 0x04000000u);
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\n16-word blocks, the poll slowed by 64 nops: down %u, up %u\n", (unsigned)ds, (unsigned)us);
    printf("\ngpu/gpu-dmabs: Done.\n");
    for (;;) VSync(0);
}
