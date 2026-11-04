/*
 * gpu/gpu-a0lsf - the scan-out's line-start fetch as GPU transfer words meet it: which words a transfer loses there and which the
 * GPU reads twice, at five rates, from 14 places before it (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-a0chop on the second SCPH-5501: at the line-start fetch (~160-240 cycles past an hblank edge in the runner's
 * 512x240) chopped transfers lose by their rate - 16-word DMA windows nothing with 4-cycle CPU windows and 13-16 read twice with
 * one, 32-word windows one wrap of the ring, a burst two - which fits an intake that goes on at ~0.47 words a cycle through ~130
 * cycles, and this emulator loses 32-40 at 16- and 32-word windows (STATUS v0.377.268). Totals say how much; the lost and repeated
 * words' places say when: where the intake stops, how long, and what it takes inside.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks; the display the runner's (512x240
 * NTSC 15-bit, the reset's ranges). Word i's pixels are i + 1. 384 words by DMA2 into GP0(A0h) at (0,256), 768 wide (one row: no
 * row change), each started 1,760 + 30 k cycles (k = 0..13) past the 40th hblank edge below vblank - just after the line's last
 * refill, so the next line's start fetch is the first thing it meets. Kinds: a burst; chopped SyncMode 0 at 16 words / 1 cycle,
 * 16 / 2, 32 / 4, 32 / 16, 64 / 16; SyncMode 1 blocks of 32. Twice each. Then 3,000 cycles, zeros until GPUSTAT bit 26, the
 * rectangle read back through GP0(C0h).
 *
 * READ IT LIKE THIS. L kind a b phase rep t left lost dup | lost runs as first:length | read-twice runs as position:length (where in the
 * read-back an index came again, how many in a row). t is the transfer's cycles, 32 bits (the poll loop adds Timer 2 up). kind B
 * burst, P chopped (a words, b cycles), S SyncMode 1 blocks of a. No assertions - compare against your emulator.
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
#define WORDS 384

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static uint16_t back[1024 * 2];
static uint8_t seen[WORDS + 1];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void toLine(uint32_t phase) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    if (phase) waitT2(phase);
}

/* kind 'B', 'P' (a = log2 DMA window words, b = log2 CPU window cycles) or 'S' (a = block words) */
static void one(char kind, int a, int b, uint32_t phase, int rep) {
    const int w = 1024, h = 1;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)WORDS * 2);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? (((uint32_t)(WORDS / a) << 16) | (uint32_t)a) : (uint32_t)WORDS);
    const uint32_t chcr = kind == 'B' ? 0x11000001u : kind == 'P' ? (0x11000101u | ((uint32_t)a << 16) | ((uint32_t)b << 20)) : 0x01000201u;
    toLine(phase);
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)WORDS * 2);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int k = 0; k < WORDS; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    for (int i = 0; i <= WORDS; i++) seen[i] = 0;
    int dup = 0;
    for (int k = 0; k < WORDS; k++) { const int v = back[2 * k]; if (v >= 1 && v <= WORDS) { if (seen[v]) dup++; if (seen[v] < 255) seen[v]++; } }
    int lost = 0;
    for (int i = 1; i <= WORDS; i++) if (!seen[i]) lost++;
    printf("L %c %d %d %u %d %u %u %d %d |", kind, kind == 'P' ? 1 << a : a, kind == 'P' ? 1 << b : b, (unsigned)phase, rep, (unsigned)t, (unsigned)left, lost, dup);
    int runs = 0;
    for (int i = 1; i <= WORDS && runs < 6; ) {
        if (seen[i]) { i++; continue; }
        int j = i; while (j <= WORDS && !seen[j]) j++;
        printf(" %d:%d", i - 1, j - i); runs++; i = j;
    }
    printf(" |");
    /* where an index comes again: a read-back position whose value is not above every value before it */
    int hi = 0; runs = 0;
    for (int k = 0; k < WORDS && runs < 6; ) {
        const int v = back[2 * k];
        if (v >= 1 && v <= hi) { int j = k; while (j < WORDS && back[2 * j] >= 1 && back[2 * j] <= hi) j++; printf(" %d:%d", k, j - k); runs++; k = j; continue; }
        if (v > hi) hi = v;
        k++;
    }
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-a0lsf\n");
    printf("L kind a b phase rep t left lost dup | lost runs first:length | read-twice runs position:length (384 words, one row, "
           "`phase` cycles past the 40th hblank edge; B burst, P chopped a words / b cycles, S SyncMode 1 blocks of a). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
    write32(GP1, 0x08000001u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    struct K { char k; int a, b; };
    static const K KS[7] = { { 'B', 0, 0 }, { 'P', 4, 0 }, { 'P', 4, 1 }, { 'P', 5, 2 }, { 'P', 5, 4 }, { 'P', 6, 4 }, { 'S', 32, 2 } };
    for (int s = 0; s < 7; s++) for (int k = 0; k < 14; k++) for (int r = 0; r < 2; r++) one(KS[s].k, KS[s].a, KS[s].b, 1760u + 30u * (uint32_t)k, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0lsf: Done.\n");
    for (;;) VSync(0);
}
