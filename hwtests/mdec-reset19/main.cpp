/*
 * mdec/mdec-reset19 - which part of mdec-reset's one() is short when its own DMA time reads ~40 (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset18 on the SCPH-5501 (two runs byte-identical): one()'s own DMA time - now() before CHCR0's start to the poll turn
 * that sees DMA0 done - reads 76-87 cycles, and 40-43 in 13 of 1,024 first calls, all at phase 32-47; v0.377.262 never reads under
 * 76. This core moves the 32 words in 37 cycles, so ~40 of the usual 76 are what is around the DMA. Either the call really is
 * ~36 shorter in those (the poll did not wait), or the clock read before the DMA was held ~36 (the call the same length): the two
 * say different whole-call times.
 *
 * WHAT IT DOES. one() unchanged; around it only reads: Timer 2 before and after each call, and the clock now() kept (clk, lastT) as
 * it stood before the call, so one()'s own t0 can be placed against the caller's read. 2,048 pairs (a warm call after 300+ cycles
 * of rest, the call behind it), interrupts off. For the first calls: in two groups, own DMA time under 64 and 64 or more - count,
 * fewest and most of the whole call, of caller-to-t0, and of t0-to-done.
 *
 * READ IT LIKE THIS. If the short group's whole call is ~36 shorter, the DMA wait went; if its caller-to-t0 is ~36 longer, the
 * clock read was held. No assertions.
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
struct Row { uint32_t status, at, t0; bool done; };
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
    r.done = false; r.at = 0; r.t0 = t0;
    for (;;) {
        const uint32_t t = now();
        if (!(read32(CHCR0) & 0x01000000u)) { r.done = true; r.at = t - t0; break; }
        if (t - t0 > 3000u) break;
    }
    write32(CHCR0, 0);
    return r;
}

static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;
/* groups: 0 own DMA time < 64, 1 >= 64; columns: whole, caller-to-t0, t0-to-done */
static uint32_t cnt[2];
static uint16_t lo[2][3], hi[2][3];

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset19\n");
    printf("mdec-reset's one(), first calls with a short own DMA time against the rest: whole call, caller to t0, t0 to done. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    one(zeroWait, zeroFlag, zeroFlag); one(zeroWait, zeroFlag, zeroFlag);   /* cache it */
    for (int i = 0; i < 2048; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256) + (uint32_t)((i >> 8) & 7));
        const uint16_t ta = read16(T2_COUNT);
        const uint32_t clkA = clk + (uint16_t)(ta - lastT);   /* the clock now() would have read at ta: loads only, no store */
        const Row r1 = one(zeroWait, zeroFlag, zeroFlag);
        const uint16_t tb = read16(T2_COUNT);
        one(zeroWait, zeroFlag, zeroFlag);
        if (!r1.done) continue;
        const int g = r1.at < 64 ? 0 : 1;
        const uint16_t v[3] = { (uint16_t)(tb - ta), (uint16_t)(r1.t0 - clkA), (uint16_t)r1.at };
        cnt[g]++;
        for (int j = 0; j < 3; j++) { if (!lo[g][j] || v[j] < lo[g][j]) lo[g][j] = v[j]; if (v[j] > hi[g][j]) hi[g][j] = v[j]; }
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("group            count     whole        caller..t0     t0..done\n");
    for (int g = 0; g < 2; g++)
        printf("%s   %5u   %5d-%-5d   %5d-%-5d   %5d-%-5d\n", g ? "own DMA >= 64  " : "own DMA < 64   ", (unsigned)cnt[g],
               (int)lo[g][0], (int)hi[g][0], (int)lo[g][1], (int)hi[g][1], (int)lo[g][2], (int)hi[g][2]);
    printf("\nmdec/mdec-reset19: Done.\n");
    for (;;) {}   /* park after Done. */
}
