/*
 * mdec/mdec-reset18 - mdec-reset17's pair with each call's own DMA time as a histogram, by the refresh phase too (2026-11-03, for
 * the SCPH-5501).
 *
 * WHY. mdec-reset17 on the SCPH-5501: one()'s own DMA time (now() before CHCR0's start to the poll turn that saw DMA0 done) reads
 * 42-86 cycles for a warm call after a rest and 76-86 for the call behind it; v0.377.262 reads 78-88 for both. mdec-reset8's first
 * row read 39 where every other read 75. A DMA of 32 words to the MDEC in ~40 cycles is another mode, or the clock read at
 * now() held: which, and when, a min / max cannot say.
 *
 * WHAT IT DOES. mdec-reset17's pairs (one() unchanged, warm, after 300+ cycles of rest, then at once), 1,024 of them, interrupts off;
 * each call's own DMA time binned by 4 cycles (0-127), and the phase of Timer 2 (whole refreshes) before the first call for the
 * first call's times under 64.
 *
 * READ IT LIKE THIS. Per bin, how many first and second calls; then the phases of the short first calls. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR0    0x1F801080u
#define BCR0     0x1F801084u
#define CHCR0    0x1F801088u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint32_t table[32] __attribute__((aligned(16)));
struct Row { uint32_t status, at, turns; bool done; };
static __attribute__((noinline, noclone)) Row one(uint32_t wait, bool cmdFirst, bool twice) {
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    if (twice) write32(MDEC1, 0x60000000u);
    if (cmdFirst) write32(MDEC0, 0x40000001u);
    if (wait) waitCycles(wait);
    write32(MDEC1, 0x60000000u);
    Row r;
    r.status = read32(MDEC1);
    if (!cmdFirst) write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    const uint32_t t0 = now();
    write32(CHCR0, 0x01000201u);
    r.done = false; r.at = 0; r.turns = 0;
    for (;;) {
        const uint32_t t = now();
        r.turns++;
        if (!(read32(CHCR0) & 0x01000000u)) { r.done = true; r.at = t - t0; break; }
        if (t - t0 > 3000u) break;
    }
    write32(CHCR0, 0);
    return r;
}

static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;
/* per call of the pair: the own DMA time in bins of 4 (0-127; 128 and up, and lost, in the last two); the short first calls' phases */
static uint16_t hist[2][34];
static uint16_t shortPh[16];
static void note(int k, const Row &r, uint32_t phase) {
    const int b = !r.done ? 33 : r.at >= 128 ? 32 : (int)(r.at / 4);
    hist[k][b]++;
    if (k == 0 && r.done && r.at < 64) shortPh[phase / 16]++;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset18\n");
    printf("mdec-reset's one(), a call after a rest and the call behind it: its own DMA time as a histogram. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    one(zeroWait, zeroFlag, zeroFlag); one(zeroWait, zeroFlag, zeroFlag);   /* cache it */
    for (int i = 0; i < 1024; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256) + (uint32_t)((i >> 8) & 3));
        const uint32_t phase = read16(T2_COUNT) & 0xFF;
        const Row r1 = one(zeroWait, zeroFlag, zeroFlag);
        const Row r2 = one(zeroWait, zeroFlag, zeroFlag);
        note(0, r1, phase); note(1, r2, phase);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("own DMA time   after a rest   behind it\n");
    for (int b = 0; b < 34; b++) {
        if (!hist[0][b] && !hist[1][b]) continue;
        if (b < 32) printf("  %3d-%3d       %5d        %5d\n", b * 4, b * 4 + 3, (int)hist[0][b], (int)hist[1][b]);
        else printf("  %s       %5d        %5d\n", b == 32 ? "128 up " : "lost   ", (int)hist[0][b], (int)hist[1][b]);
    }
    printf("first calls under 64, by phase /16:");
    for (int k = 0; k < 16; k++) printf(" %d", (int)shortPh[k]);
    printf("\n\nmdec/mdec-reset18: Done.\n");
    for (;;) {}   /* park after Done. */
}
