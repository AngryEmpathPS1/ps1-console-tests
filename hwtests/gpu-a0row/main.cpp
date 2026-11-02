/*
 * gpu/gpu-a0row - what a row change costs the GPU's intake of GP0(A0h) transfer words, against the scanline (2026-11-02)
 *
 * WHY. hwtests/gpu-a0pace (the first SCPH-5501): a burst into GP0(A0h) gives ~0.95 words a cycle and loses words only now and then,
 * when the GPU stalls and the 16-word FIFO laps - and it loses more the narrower the rectangle (bursts of 384 words: 0 lost 1,024
 * wide, 64 lost 64 wide). experiments/_mk273.js models the stalls as the scan-out's fetch bursts and a 16-word flush, and gets the
 * losses within 47% but not that trend: a flat cost per row (CB_A0R) made it worse. This test puts the rows and the scan-out side by
 * side: the same burst into rows of 8 to 64 words, started at 16 places along one scanline.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks. Words whose two pixels are their own
 * index, 1,024 of them in one burst (sync 0) by DMA2 into GP0(A0h) at (0,256), into rectangles 16, 32, 48, 64, 96, 128 and 1,024
 * pixels wide (rows of 8, 16, 24, 32, 48, 64 and 512 words). Each transfer waits for vblank (I_STAT bit 0, polled), for 40 hblanks
 * after it, for the next hblank edge, then `phase` cycles more (0, 135, 270 ... 2,025: about one line) before CHCR2 is written. Twice
 * each. Then, as gpu-a0pace: 3,000 cycles, zeros until GPUSTAT bit 26 sets, and the rectangle read back through GP0(C0h).
 *
 * READ IT LIKE THIS. R w phase rep t left lost breaks p1 p2 p3: w the rectangle's width, phase the cycles after the hblank edge, t the
 * transfer's cycles (CHCR busy), left the zeros taken beyond the rectangle's own remainder, lost the indices sent and never found,
 * breaks the places the read-back steps back (words read twice), p1-p3 the first three indices lost. No assertions - compare against
 * your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0row`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
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
static uint16_t back[WORDS * 2 + 128];  // the widest remainder: 96 wide is 22 rows, 2,112 pixels
static uint8_t seen[WORDS];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* To `phase` cycles after the edge of the 40th hblank past vblank's start. */
static void toLine(uint32_t phase) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    if (phase) waitT2(phase);
}

static void one(int w, uint32_t phase, int rep) {
    const int n = WORDS, h = (2 * n + w - 1) / w, rest = (w * h - 2 * n) / 2;
    for (int i = 0; i < n; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)((w + 15) & ~15));
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (uint32_t)n);
    toLine(phase);
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, 0x11000001u);
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
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    const int words = w * h / 2;
    for (int k = 0; k < words; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    for (int i = 0; i < n; i++) seen[i] = 0;
    int breaks = 0;
    for (int k = 0; k < words; k++) { const int v = back[2 * k]; if (v < n) seen[v] = 1; if (k && v > 0 && v <= back[2 * k - 2]) breaks++; }
    int lost = 0, p[3] = { -1, -1, -1 }, np = 0;
    for (int i = 0; i < n; i++) if (!seen[i]) { lost++; if (np < 3 && (i == 0 || seen[i - 1])) p[np++] = i; }
    printf("R %d %u %d %u %d %d %d %d %d %d\n", w, (unsigned)phase, rep, (unsigned)t, (int)left - rest, lost, breaks, p[0], p[1], p[2]);
}

int main() {
    printf("\ngpu/gpu-a0row\n");
    printf("R w phase rep t left lost breaks p1 p2 p3 (one burst of 1,024 words into a w-wide rectangle, `phase` cycles past an "
           "hblank edge 40 lines below vblank). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const int W[7] = { 16, 32, 48, 64, 96, 128, 1024 };
    for (int a = 0; a < 7; a++) for (int ph = 0; ph < 16; ph++) for (int r = 0; r < 2; r++) one(W[a], (uint32_t)ph * 135u, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0row: Done.\n");
    for (;;) VSync(0);
}
