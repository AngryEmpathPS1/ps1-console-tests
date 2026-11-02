/*
 * gpu/gpu-a0pace - how fast the GPU takes a GP0(A0h) transfer's words, found from where a DMA starts to outrun it (2026-11-02)
 *
 * WHY. hwtests/dma-loss and dma-loss2 (the first SCPH-5501): a DMA into GP0(A0h) loses words when it outruns the GPU - blocks of 8 and
 * 16 never, 32-128 in about half the runs, one burst of 1,024 always (32-166) - and every loss on a 64-wide rectangle starts 2 words
 * past a 16-word boundary. This core takes transfer data the moment it comes (GPU.canTake: "pixel data streams straight in"). The
 * CPU cannot find the GPU's pace (a store loop gives a word every 6 cycles, which nothing loses); the DMA can: a burst gives ~0.91 a
 * cycle, and if the GPU takes r a cycle the 16-word FIFO laps after about 16 / (0.91 - r) words.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock. Words whose two pixels are their own index, by DMA2 into GP0(A0h)
 * at (0,256), three times each:
 *   B  one burst (sync 0) of N = 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1,024 and 2,048 words, into rectangles 1,024 wide
 *      (a single row while N <= 512) and 64 wide (a row each 32 words);
 *   K  blocks (sync 1) of 17, 20, 24, 28, 32, 48, 64, 96 and 128 words, 2,048 words or the most whole blocks under it, 1,024 wide.
 * The rectangle is as tall as the words need (rounded up to a row); 3,000 cycles after the transfer zeros are written until GPUSTAT
 * bit 26 sets, then the rectangle is read back through GP0(C0h) and GPUREAD.
 *
 * READ IT LIKE THIS. B|K w n rep t left lost breaks p1 p2 p3: n the burst's words (B) or the block size (K), t the transfer's cycles
 * (CHCR busy), left the zeros taken beyond the rectangle's own remainder, lost the indices sent and never found, breaks the places the
 * read-back steps back (words read twice), p1-p3 the first three indices lost. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0pace`.
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
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static uint16_t back[WORDS * 2];
static uint8_t seen[WORDS];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* n words to send, by blocks of bs (0 = one burst), into a rectangle w wide */
static void one(char tag, int w, int n, int bs, int rep) {
    const int width = w < 2 * n ? w : 2 * n, h = (2 * n + width - 1) / width, rest = (width * h - 2 * n) / 2;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)((width + 15) & ~15));
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)width);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, bs ? (((uint32_t)(n / bs) << 16) | (uint32_t)bs) : (uint32_t)n);
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, bs ? 0x01000201u : 0x11000001u);
    uint32_t polls = 0;
    while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    if (polls >= 200000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)width);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    const int words = width * h / 2;
    for (int k = 0; k < words; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    for (int i = 0; i < n; i++) seen[i] = 0;
    int breaks = 0;
    for (int k = 0; k < words; k++) { const int v = back[2 * k]; if (v < n) seen[v] = 1; if (k && v > 0 && v <= back[2 * k - 2]) breaks++; }
    int lost = 0, p[3] = { -1, -1, -1 }, np = 0;
    for (int i = 0; i < n; i++) if (!seen[i]) { lost++; if (np < 3 && (i == 0 || seen[i - 1])) p[np++] = i; }
    printf("%c %d %d %d %u %d %d %d %d %d %d\n", tag, w, bs ? bs : n, rep, (unsigned)t, (int)left - rest, lost, breaks, p[0], p[1], p[2]);
}

int main() {
    printf("\ngpu/gpu-a0pace\n");
    printf("B|K w n rep t left lost breaks p1 p2 p3 (B: one burst of n words; K: blocks of n). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const int NB[12] = { 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 2048 }, BS[9] = { 17, 20, 24, 28, 32, 48, 64, 96, 128 };
    for (int a = 0; a < 2; a++) for (int i = 0; i < 12; i++) for (int r = 0; r < 3; r++) one('B', a ? 64 : 1024, NB[i], 0, r);
    for (int i = 0; i < 9; i++) for (int r = 0; r < 3; r++) one('K', 1024, (WORDS / BS[i]) * BS[i], BS[i], r);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0pace: Done.\n");
    for (;;) VSync(0);
}
