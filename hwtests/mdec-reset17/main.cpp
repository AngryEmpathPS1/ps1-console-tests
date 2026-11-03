/*
 * mdec/mdec-reset17 - mdec-reset's one() itself, its poll loop through now() included, timed from outside and by its own clock, for
 * a warm call after a rest and the call right behind it (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset14's first warm call of a pair is 415-425 cycles on the SCPH-5501 and ~10 fewer on v0.377.262, where mdec-reset16
 * (one()'s steps with a poll of Timer 2 alone) reads the console's minimum within a cycle at every segment. one() polls DMA0
 * through now(), which also stores two variables every turn; mdec-reset16's poll did not. This times one() as it is.
 *
 * WHAT IT DOES. one() unchanged (the reset, the enables, the status, the table command, DMA0 polled through now(), CHCR0 = 0, a
 * Row returned), with a count of its poll turns added after the loop; 512 pairs, interrupts off, cached code, varied refresh
 * phases, Timer 2 read before, between and after the two calls. Per call: the whole call from outside, one()'s own DMA time
 * (Row.at: now() before CHCR0's start to the turn that saw it done) and its poll turns, fewest and most.
 *
 * READ IT LIKE THIS. Beside an emulator's: a whole call that differs with the same DMA time and turns is outside the poll; a
 * turn count that differs is the loop's own timing. No assertions.
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
/* per call of the pair: whole, at, turns - fewest and most */
static uint16_t lo[2][3], hi[2][3];
static uint32_t lostN;
static void note(int k, uint16_t whole, const Row &r) {
    const uint16_t v[3] = { whole, (uint16_t)r.at, (uint16_t)r.turns };
    for (int j = 0; j < 3; j++) { if (!lo[k][j] || v[j] < lo[k][j]) lo[k][j] = v[j]; if (v[j] > hi[k][j]) hi[k][j] = v[j]; }
    if (!r.done) lostN++;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset17\n");
    printf("mdec-reset's one() as it is, a call after a rest and the call behind it: whole, its own DMA time, its poll turns. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    one(zeroWait, zeroFlag, zeroFlag); one(zeroWait, zeroFlag, zeroFlag);   /* cache it */
    for (int i = 0; i < 512; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        const uint16_t ta = read16(T2_COUNT);
        const Row r1 = one(zeroWait, zeroFlag, zeroFlag);
        const uint16_t tb = read16(T2_COUNT);
        const Row r2 = one(zeroWait, zeroFlag, zeroFlag);
        const uint16_t tc = read16(T2_COUNT);
        note(0, (uint16_t)(tb - ta), r1); note(1, (uint16_t)(tc - tb), r2);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("call            whole          DMA time (own)    poll turns\n");
    for (int k = 0; k < 2; k++)
        printf("%s   %5d-%-5d    %5d-%-5d       %3d-%-3d\n", k ? "behind it   " : "after a rest", (int)lo[k][0], (int)hi[k][0], (int)lo[k][1], (int)hi[k][1], (int)lo[k][2], (int)hi[k][2]);
    printf("lost: %u\n", (unsigned)lostN);
    printf("\nmdec/mdec-reset17: Done.\n");
    for (;;) {}   /* park after Done. */
}
