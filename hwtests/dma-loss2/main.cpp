/*
 * dma/dma-loss2 - dma-loss's read-back, every break in it (2026-11-02)
 *
 * WHY. hwtests/dma-len counted the words a transfer left the GPU still wanting (console-logs/2026-11-01-SCPH-5501-len2): blocks of 1..32
 * words never lose any, blocks of 64 and 128 lose 0..64, one burst (sync 0) of 256..2,048 words 32..160 - always a multiple of 32. psx-spx:
 * GP0's FIFO has 5-bit pointers, so a write 32 past what was read wraps it to empty. This core loses none (dma-len and dma-len00 run clean),
 * and the first SCPH-5501's slow boot came from such a loss (hwtests/dma-len00). To model it: where in the transfer the words go, and how
 * many, as the rectangle's width (the GPU's row) changes.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. 1,024 words, each word's two pixels its own index (word i: pixels i, i),
 * sent by DMA2 to GP0(A0h) at (0,256) into rectangles 16, 32, 64, 128 and 256 pixels wide, by blocks (sync 1) of 8, 16, 32, 64 and 128 words
 * and in one burst (sync 0), three times each. 3,000 cycles after the transfer, zeros are written to GP0 until GPUSTAT bit 26 sets (the words
 * it still wanted), then the rectangle is read back through GP0(C0h) and GPUREAD.
 *
 * dma-loss2: hwtests/dma-loss on the first SCPH-5501 lost words in chunks of 16, often with the GPU wanting none more (so the read-back
 * held 1,024 words with indices missing: words twice?). This one prints, for rectangles 64 wide only, every break in the read-back: B pos
 * previous index, the index found - backward breaks are words read again. Six repetitions of each block size.
 * READ IT LIKE THIS. X w bs rep t left lost first g1 n1 g2 n2 g3 n3: bs 0 is the burst; t the transfer's cycles (CHCR busy); left the zeros it
 * took; lost the indices never found in the read-back; first the first missing index; then up to three gaps: at read-back position g, n indices
 * skipped. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma-loss2`.
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
static uint16_t back[WORDS * 2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void one(int w, int bs, int rep) {
    const int h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)((w + 15) & ~15));
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, bs ? (((uint32_t)(WORDS / bs) << 16) | (uint32_t)bs) : (uint32_t)WORDS);
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, bs ? 0x01000201u : 0x11000001u);
    uint32_t polls = 0;
    while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    if (polls >= 200000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 4096) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 4096) { write32(GP1, 0x01000000u); }
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int k = 0; k < WORDS; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    /* every word's two pixels are its index: walk the read-back in word steps, note where the index jumps */
    int lost = 0, first = -1, ng = 0, g[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 }, expect = 0;
    for (int k = 0; k < WORDS && expect < WORDS; k++) {
        const int got = back[2 * k];
        if (got == expect) { expect++; continue; }
        if (got > expect && got < WORDS) {
            const int skip = got - expect;
            lost += skip; if (first < 0) first = expect;
            if (ng < 3) { g[ng] = k; n[ng] = skip; ng++; }
            expect = got + 1;
        }
    }
    if (expect < WORDS) { lost += WORDS - expect; if (first < 0) first = expect; }
    printf("X %d %d %d %u %u %d %d %d %d %d %d %d %d\n", w, bs, rep, (unsigned)t, (unsigned)left, lost, first, g[0], n[0], g[1], n[1], g[2], n[2]);
    int nb = 0;
    for (int k = 1; k < WORDS && nb < 12; k++) if (back[2 * k] != back[2 * k - 2] + 1) { printf("B %d %d %d\n", k, back[2 * k - 2], back[2 * k]); nb++; }
}

int main() {
    printf("\ndma/dma-loss2\n");
    printf("X w bs rep t left lost first g1 n1 g2 n2 g3 n3 (bs 0 = one burst). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const int BS[4] = { 32, 64, 128, 0 };
    for (int b = 0; b < 4; b++) for (int r = 0; r < 6; r++) one(64, BS[b], r);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-loss2: Done.\n");
    for (;;) VSync(0);
}
