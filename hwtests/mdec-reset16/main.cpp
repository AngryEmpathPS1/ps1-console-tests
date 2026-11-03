/*
 * mdec/mdec-reset16 - mdec-reset15 with the call's own ends timed too: its entry (the call, the six stack saves) and its tail
 * (CHCR0 = 0, the return, the restores) (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset15 on the SCPH-5501: the inside of mdec-reset's one() - reset to status 56-59, status to DMA start 81-91 (the
 * 32-word table DMA, +10 when a refresh meets it), DMA start to done 15 - and experiments/_mkmdrf.js on v0.377.260 reads the same
 * to a cycle. But mdec-reset14's first warm call of a pair, timed from outside, is 415-425 on the console and 407-412 there:
 * ~10 short somewhere outside mdec-reset15's segments.
 *
 * WHAT IT DOES. one2() (one()'s own branches; Timer 2 read on entry, after the status read, after DMA0's start, when the poll
 * sees DMA0 done, and after the closing CHCR0 = 0) called in pairs from main with Timer 2 read before, between and after the
 * calls. 512 pairs, interrupts off, cached code, varied refresh phases. Per call: the fewest and most cycles of the entry
 * (main's read to one2's first), the three inside segments, the tail inside (done to after CHCR0 = 0) and the return (one2's
 * last read to main's next).
 *
 * READ IT LIKE THIS. Set each column beside an emulator's. No assertions.
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
struct Seg { uint16_t t0, t1, t2, t3, t4; };
static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;
static __attribute__((noinline, noclone)) Seg one2(uint32_t wait, bool cmdFirst, bool twice) {
    Seg s;
    s.t0 = read16(T2_COUNT);
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    if (twice) write32(MDEC1, 0x60000000u);
    if (cmdFirst) write32(MDEC0, 0x40000001u);
    if (wait) waitCycles(wait);
    write32(MDEC1, 0x60000000u);
    volatile uint32_t status = read32(MDEC1); (void)status;
    s.t1 = read16(T2_COUNT);
    if (!cmdFirst) write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    write32(CHCR0, 0x01000201u);
    s.t2 = read16(T2_COUNT);
    s.t3 = s.t2;
    for (int i = 0; i < 1000; i++) { s.t3 = read16(T2_COUNT); if (!(read32(CHCR0) & 0x01000000u)) break; }
    write32(CHCR0, 0);
    s.t4 = read16(T2_COUNT);
    return s;
}

/* columns: entry, reset..status, status..DMA start, DMA start..done, done..after CHCR0 = 0, return */
static uint16_t lo[2][6], hi[2][6];
static void note(int k, const Seg &s, uint16_t before, uint16_t after) {
    const uint16_t v[6] = { (uint16_t)(s.t0 - before), (uint16_t)(s.t1 - s.t0), (uint16_t)(s.t2 - s.t1), (uint16_t)(s.t3 - s.t2),
                            (uint16_t)(s.t4 - s.t3), (uint16_t)(after - s.t4) };
    for (int j = 0; j < 6; j++) { if (!lo[k][j] || v[j] < lo[k][j]) lo[k][j] = v[j]; if (v[j] > hi[k][j]) hi[k][j] = v[j]; }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset16\n");
    printf("mdec-reset's one() timed in segments, its entry and its tail included: a call after a rest, and the call behind it. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    one2(zeroWait, zeroFlag, zeroFlag); one2(zeroWait, zeroFlag, zeroFlag);   /* cache it */
    for (int i = 0; i < 512; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        const uint16_t ta = read16(T2_COUNT);
        const Seg s1 = one2(zeroWait, zeroFlag, zeroFlag);
        const uint16_t tb = read16(T2_COUNT);
        const Seg s2 = one2(zeroWait, zeroFlag, zeroFlag);
        const uint16_t tc = read16(T2_COUNT);
        note(0, s1, ta, tb); note(1, s2, tb, tc);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("call           entry      reset..status  status..DMA start  DMA start..done  done..CHCR0=0   return\n");
    for (int k = 0; k < 2; k++) {
        printf("%s", k ? "behind it   " : "after a rest");
        for (int j = 0; j < 6; j++) printf("  %5d-%-5d", (int)lo[k][j], (int)hi[k][j]);
        printf("\n");
    }
    printf("\nmdec/mdec-reset16: Done.\n");
    for (;;) {}   /* park after Done. */
}
