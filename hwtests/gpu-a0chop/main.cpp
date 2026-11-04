/*
 * gpu/gpu-a0chop - what the new GPU's transfer intake takes through the scan-out's refills, by how fast the words come: DMA2's
 * CHOPPED SyncMode 0 (no data request: words a cycle each in DMA windows of 2^n, the CPU's window of 2^m cycles between) beside a
 * plain burst and SyncMode 1 blocks, the same 1,024 words from the same places in the line (2026-11-04, for the SCPH-5501).
 *
 * WHY. Under hwtests/runner (Unirom's 512x240) a burst into GP0(A0h) loses a lap of 16 words at each of the three refills a line
 * and more at the line-start fetch (hwtests/gpu-a0row on the second SCPH-5501: losses 384 words apart, "R 1024 270 0 1117 0 32 2
 * 579 963"), while a 4-word-block upload never waits there (hwtests/gpu-dma-quiet) and 32-word blocks lose 16 now and then
 * (dma-loss). Blocks wait for the GPU's request; a chopped transfer does not, so this varies the words' rate and grouping alone: what
 * a refill lets through at each rate is the intake's room and the refill's length (STATUS "The bursts' fitting set ran in another
 * display mode"). And the emulator moved a chopped transfer's words at once, as one burst - this says what a chopped one loses.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks. The display set to what the runner
 * gives (512x240 NTSC 15-bit, the reset's ranges), so a run anywhere has the same refills. Word i's two pixels are i + 1 (0 is what
 * the zeros fed after it read as), 1,024 words by DMA2 into GP0(A0h) at (0,256), 1,024 wide (two rows of 512). Each transfer waits
 * for vblank (I_STAT bit 0, polled), 40 hblanks, the next hblank edge, then `phase` cycles (0 or 1,080) before CHCR2's store. Kinds:
 * the burst (CHCR 11000001h); chopped, DMA windows 1, 4, 16, 32, 64 words by CPU windows 1, 4, 16, 64 cycles (11000101h | n << 16
 * | m << 20); SyncMode 1 blocks of 4, 16 and 32 words with GP1(04h)=2 (data request when the FIFO is empty), 8 and 16 with
 * GP1(04h)=1 ("at least half empty", psx-spx). Twice each. Then, as gpu-a0row: 3,000 cycles, zeros until GPUSTAT bit 26 sets,
 * and the rectangle read back through GP0(C0h).
 *
 * READ IT LIKE THIS. C kind a b phase rep t left lost dup runs: kind B (burst), P (chopped: a the DMA window's words, b the CPU
 * window's cycles), S (SyncMode 1: a the block's words, b the request mode); t the transfer's cycles (CHCR busy), left the zeros
 * taken, lost the indices never read back, dup the readings of an index beyond its first (psx-spx's overrun reads the newest
 * words twice), then up to 8 lost runs as first:length. No assertions - compare against your emulator.
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
static uint16_t back[WORDS * 2];
static uint8_t seen[WORDS + 1];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* To `phase` cycles after the edge of the 40th hblank past vblank's start (gpu-a0row's). */
static void toLine(uint32_t phase) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    if (phase) waitT2(phase);
}

/* kind 'B', 'P' (a = log2 DMA window words, b = log2 CPU window cycles) or 'S' (a = block words, b = GP1(04h) mode) */
static void one(char kind, int a, int b, uint32_t phase, int rep) {
    const int w = 1024, h = 2;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000000u | (uint32_t)(kind == 'S' ? b : 2));
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? (((uint32_t)(WORDS / a) << 16) | (uint32_t)a) : (uint32_t)WORDS);
    const uint32_t chcr = kind == 'B' ? 0x11000001u : kind == 'P' ? (0x11000101u | ((uint32_t)a << 16) | ((uint32_t)b << 20)) : 0x01000201u;
    toLine(phase);
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    uint32_t polls = 0;
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) polls++;
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int k = 0; k < WORDS; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    for (int i = 0; i <= WORDS; i++) seen[i] = 0;
    int dup = 0;
    for (int k = 0; k < WORDS; k++) { const int v = back[2 * k]; if (v >= 1 && v <= WORDS) { if (seen[v]) dup++; if (seen[v] < 255) seen[v]++; } }
    int lost = 0;
    for (int i = 1; i <= WORDS; i++) if (!seen[i]) lost++;
    printf("C %c %d %d %u %d %u %u %d %d", kind, kind == 'P' ? 1 << a : a, kind == 'P' ? 1 << b : b, (unsigned)phase, rep, (unsigned)t, (unsigned)left, lost, dup);
    int runs = 0;
    for (int i = 1; i <= WORDS && runs < 8; ) {
        if (seen[i]) { i++; continue; }
        int j = i; while (j <= WORDS && !seen[j]) j++;
        printf(" %d:%d", i - 1, j - i); runs++; i = j;
    }
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-a0chop\n");
    printf("C kind a b phase rep t left lost dup runs (1,024 words into a 1,024-wide rectangle, `phase` cycles past an hblank edge 40 "
           "lines below vblank; B burst, P chopped a words / b cycles, S SyncMode 1 blocks of a, request mode b). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);   /* the reset's ranges, as the runner leaves them */
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
    write32(GP1, 0x08000001u);                              /* 512x240, NTSC, 15-bit: Unirom's, the runner's */
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const uint32_t PH[2] = { 0, 1080 };
    for (int p = 0; p < 2; p++) for (int r = 0; r < 2; r++) one('B', 0, 0, PH[p], r);
    static const int DW[5] = { 0, 2, 4, 5, 6 }, CW[4] = { 0, 2, 4, 6 };
    for (int a = 0; a < 5; a++) for (int b = 0; b < 4; b++) for (int p = 0; p < 2; p++) for (int r = 0; r < 2; r++) one('P', DW[a], CW[b], PH[p], r);
    static const int SB[5] = { 4, 16, 32, 8, 16 }, SM[5] = { 2, 2, 2, 1, 1 };
    for (int s = 0; s < 5; s++) for (int p = 0; p < 2; p++) for (int r = 0; r < 2; r++) one('S', SB[s], SM[s], PH[p], r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0chop: Done.\n");
    for (;;) VSync(0);
}
