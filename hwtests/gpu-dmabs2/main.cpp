/*
 * gpu/gpu-dmabs2 - does an upload in blocks over 16 words hang the new GPU's DMA, from a clean start; and a download's
 * cost by its shape (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-dmabs on the SCPH-7501: a 128x64 upload by DMA2 SyncMode 1 in 32-, 64- and 128-word blocks never
 * finished (CHCR bit 24 still set after 40,000,000 cycles), where psx-spx says only the OLD GPU fails over 10h ("new gpu
 * works with blksiz=8C0h!") - but that test did not stop the DMA after a timeout, so the first hang may have poisoned
 * the rest. And its downloads were not linear in the block count (4 / 8 / 16 / 32 / 64 / 128 words: 15,153 / 10,258 /
 * 8,956 / 6,195 / 5,393 / 4,935 cycles; this core 10.6 a block + 1.5 a word), DMA_GPU_READ's 1.5 a word having been fitted
 * to gpu/bandwidth's 320x240 download in 16-word blocks - a per-row cost folded into the word's would read a 128-wide
 * download slow, as gpu-scan6's does.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; every transfer in the vertical blanking (20,000 cycles after
 * the VBlank IRQ). A, uploads of 128x64 (4,096 words) at (512,300) by DMA2 SyncMode 1, waited at most 200,000 cycles,
 * each followed by a recovery (CHCR 0, GP1(01h), GP1(04h) 0) - FIRST 32-word blocks from a clean GPU, then 16, 8, 64,
 * 16: the cycles to CHCR bit 24 clear, or "hung" and the words the DMA's MADR had passed. B, downloads of 4,096 words as
 * 512x16, 256x32, 128x64 and 64x128 at (512,256) in 16- and 128-word blocks, fastest of four.
 *
 * READ IT LIKE THIS. A: which block sizes complete; whether a clean start changes it. B: the cycles against the rows
 * (16 / 32 / 64 / 128) at each block size - a row's cost and a word's. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dmabs2 --frames=3000 --quiet=3000`.
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
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 400000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t buf[4096];

static void toBlank() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
}
static void recover() {
    write32(D2_CHCR, 0);
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    { const uint32_t t0 = now(); while (now() - t0 < 2000u) {} }
}
/* an upload: returns cycles, or 0xFFFFFFFF if it did not finish in 200,000; *words = MADR's progress */
static uint32_t up(int bs, uint32_t* words) {
    write32(GP1, 0x04000002u);
    toBlank();
    const uint32_t base = (uint32_t)buf & 0x1FFFFF;
    const uint32_t t0 = now();
    write32(GP0, 0xA0000000u); write32(GP0, xy(512, 300)); write32(GP0, xy(128, 64));
    write32(D2_MADR, base);
    write32(D2_BCR, ((uint32_t)(4096 / bs) << 16) | (uint32_t)bs);
    write32(D2_CHCR, 0x01000201u);
    uint32_t t;
    for (;;) { t = now() - t0; if (!(read32(D2_CHCR) & 0x01000000u)) break; if (t > 200000u) { t = 0xFFFFFFFFu; break; } }
    *words = (read32(D2_MADR) - base) / 4;
    recover();
    return t;
}
static uint32_t down(int w, int h, int bs) {
    write32(GP1, 0x04000003u);
    toBlank();
    const uint32_t t0 = now();
    write32(GP0, 0xC0000000u); write32(GP0, xy(512, 256)); write32(GP0, xy(w, h));
    while (!(read32(GP1) & 0x08000000u) && now() - t0 < 400000u) {}
    write32(D2_MADR, (uint32_t)buf & 0x1FFFFF);
    write32(D2_BCR, ((uint32_t)(4096 / bs) << 16) | (uint32_t)bs);
    write32(D2_CHCR, 0x01000200u);
    uint32_t t;
    for (;;) { t = now() - t0; if (!(read32(D2_CHCR) & 0x01000000u)) break; if (t > 400000u) { t = 0xFFFFFFFFu; break; } }
    recover();
    return t;
}

int main() {
    printf("\ngpu/gpu-dmabs2\n");
    printf("A: 128x64 uploads by DMA2 block size from a clean GPU, recovered after each. B: 4,096-word downloads by shape.\n");
    printf("In the blanking. No assertions.\n\n");
    for (int i = 0; i < 4096; i++) buf[i] = 0x7FFF7FFFu;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE6000000u);
    static const int UB[5] = { 32, 16, 8, 64, 16 };
    for (int i = 0; i < 5; i++) {
        uint32_t words;
        const uint32_t t = up(UB[i], &words);
        ExitCriticalSection();
        if (t == 0xFFFFFFFFu) printf("A  upload %3d-word blocks: hung (MADR past %u words)\n", UB[i], (unsigned)words);
        else printf("A  upload %3d-word blocks: %u cycles\n", UB[i], (unsigned)t);
        EnterCriticalSection();
    }
    static const int W[4] = { 512, 256, 128, 64 }, H[4] = { 16, 32, 64, 128 }, B2[2] = { 16, 128 };
    ExitCriticalSection();
    printf("\nB  shape     rows   16-word   128-word\n");
    EnterCriticalSection();
    for (int i = 0; i < 4; i++) {
        uint32_t r[2];
        for (int b = 0; b < 2; b++) { uint32_t m = 0xFFFFFFFFu; for (int k = 0; k < 4; k++) { const uint32_t t = down(W[i], H[i], B2[b]); m = t < m ? t : m; } r[b] = m; }
        ExitCriticalSection();
        printf("   %3dx%-4d %5d   %7u    %7u\n", W[i], H[i], H[i], (unsigned)r[0], (unsigned)r[1]);
        EnterCriticalSection();
    }
    write32(GP1, 0x04000000u);
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dmabs2: Done.\n");
    for (;;) VSync(0);
}
