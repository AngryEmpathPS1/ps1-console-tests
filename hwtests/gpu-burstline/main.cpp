/*
 * gpu/gpu-burstline - where in the video line a RAM-to-VRAM burst starts losing words, beside a 4-word-block upload started at the
 * same place (2026-11-05)
 *
 * WHY. CURRENT.md's open item on the GPU's transfer intake: a burst (sync 0: gpu-a0row, dma-loss) loses words in 32s as if the GPU took
 * nothing past its FIFO while the scan-out holds VRAM, while a 4-word-block upload (sync 1) never waits there; no intake room or window
 * share fits both. Its named next step: time a burst against the line - where in the line its losses start - beside a 4-word upload
 * at the same place.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, the display as the runner leaves it (240 lines: GPUSTAT bit 31
 * changes every line). For d = 0, 32 .. 2,144 cycles after a change of bit 31 (a line edge): 1,024 words, each word's two pixels its
 * own index, sent by DMA2 to GP0(A0h) at (0,256) into a rectangle 256 pixels wide (8 rows of 128 words), once as one burst and once
 * in 4-word blocks. As hwtests/dma-loss: 3,000 cycles on, zeros to GP0 until GPUSTAT bit 26 sets (the words it still wanted), then
 * the rectangle read back through GP0(C0h) and GPUREAD.
 *
 * READ IT LIKE THIS. L bs d t left lost first g1 n1 g2 n2 g3 n3: bs 0 the burst, 4 the blocks; d the start after the line edge; t the
 * transfer's cycles (CHCR busy); left the zeros it took; lost the indices never found; first the first missing index; then up to three
 * gaps (read-back position, indices skipped). A loss's place against d says where in the line the intake stops taking words.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-burstline --frames=4000 --quiet=2000`.
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

static void one(int bs, uint32_t d) {
    const int w = 256, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, bs ? (((uint32_t)(WORDS / bs) << 16) | (uint32_t)bs) : (uint32_t)WORDS);
    /* a line edge: GPUSTAT bit 31 changes (bounded), then d cycles */
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
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
    printf("L %d %u %u %u %d %d %d %d %d %d %d %d\n", bs, (unsigned)d, (unsigned)t, (unsigned)left, lost, first, g[0], n[0], g[1], n[1], g[2], n[2]);
}

int main() {
    printf("\ngpu/gpu-burstline\n");
    printf("L bs d t left lost first g1 n1 g2 n2 g3 n3 (bs 0 = one burst, d = cycles after a line edge). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (uint32_t d = 0; d < 2150; d += 32) { one(0, d); one(4, d); }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-burstline: Done.\n");
    for (;;) VSync(0);
}
