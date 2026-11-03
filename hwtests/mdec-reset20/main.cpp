/*
 * mdec/mdec-reset20 - mdec-reset18's own DMA time by the phase of the refresh, 8 cycles at a time (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset18 on the SCPH-5501 (two runs byte-identical): the refresh lengthens one()'s own DMA time (now() before CHCR0's
 * start to the poll turn that sees DMA0 done) in 155 of 1,011 first calls, a window of ~39 cycles of the 256 - about the 32-word
 * block itself. v0.377.262 lengthens 300 of 1,024, ~75 cycles: the whole span it measures. So this core charges the refresh on
 * accesses around the block that the console does not. Which ones the histogram cannot say; the phase can.
 *
 * WHAT IT DOES. mdec-reset18's pairs (one() unchanged, warm, after 300+ cycles of rest and 0-31 single cycles more, then the call
 * behind it), 2,048 of them, interrupts off. Timer 2's phase (whole refreshes, low 8 bits) is read before the first call. Per 8-cycle phase bin: the first
 * calls' count, mean own DMA time (tenths) and how many read 80 or more, and the same for the call behind it.
 *
 * READ IT LIKE THIS. Where the phase puts the refresh inside the block both machines read 80 and up; a run of bins where only one
 * of them does is an access the other does not charge. Timer 2's phase sits at a different offset from the refresh on each
 * machine, so line the two curves up by shape, not by bin number. No assertions.
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

/* n & 31 more cycles, one at a time: a jump into a run of 32 nops, n from its end (cached code, 1 cycle each). waitCycles alone
 * leaves the start on its poll's grid: a first build had 13 of 32 phase bins empty on this core. */
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 32\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n & 31u) : "t0", "t1");
}

static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;
/* per phase bin of 8 and per call of the pair: calls done, the sum of their own DMA times, how many read 80 up, how many were lost */
static uint16_t cnt[2][32], hi[2][32], lost[2][32];
static uint32_t sum[2][32];
static void note(int k, const Row &r, uint32_t phase) {
    const int b = (int)(phase / 8);
    if (!r.done) { lost[k][b]++; return; }
    cnt[k][b]++; sum[k][b] += r.at;
    if (r.at >= 80) hi[k][b]++;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset20\n");
    printf("mdec-reset's one(), a call after a rest and the call behind it: own DMA time by Timer 2's phase /8. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    one(zeroWait, zeroFlag, zeroFlag); one(zeroWait, zeroFlag, zeroFlag);   /* cache it */
    for (int i = 0; i < 2048; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        sled((uint32_t)i * 11u);
        const uint32_t phase = read16(T2_COUNT) & 0xFF;
        const Row r1 = one(zeroWait, zeroFlag, zeroFlag);
        const Row r2 = one(zeroWait, zeroFlag, zeroFlag);
        note(0, r1, phase); note(1, r2, phase);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("phase    first: n  mean  80up lost    behind: n  mean  80up lost\n");
    for (int b = 0; b < 32; b++) {
        const unsigned m0 = cnt[0][b] ? (unsigned)(sum[0][b] * 10 / cnt[0][b]) : 0, m1 = cnt[1][b] ? (unsigned)(sum[1][b] * 10 / cnt[1][b]) : 0;
        printf("%3d-%3d     %4d %3u.%u %4d %4d        %4d %3u.%u %4d %4d\n", b * 8, b * 8 + 7,
               (int)cnt[0][b], m0 / 10, m0 % 10, (int)hi[0][b], (int)lost[0][b], (int)cnt[1][b], m1 / 10, m1 % 10, (int)hi[1][b], (int)lost[1][b]);
    }
    printf("\nmdec/mdec-reset20: Done.\n");
    for (;;) {}   /* park after Done. */
}
