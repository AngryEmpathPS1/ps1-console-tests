/*
 * mdec/mdec-reset21 - mdec-reset20 binned by the timer read that starts the timed span (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset20 on the SCPH-5501 (two runs byte-identical) against v0.377.262, lined up by the calls both lose (console phase
 * 72-79, here 136-143): the partial costs of a refresh on the CPU's loads match in place and size, and the table DMA's window does
 * not - the console pays from 8 cycles earlier (+10 for 24 cycles, then ~+6 for 16), this core 8 later (+10 for 24, +8 for 8),
 * both ending at the same place. mdec-reset20 bins by Timer 2 read ~130 cycles before one()'s t0, which smears the edges.
 *
 * WHAT IT DOES. mdec-reset20's pairs unchanged in one() but for one store after its poll loop (outside the timed span): t0, the
 * clock at the span's first timer read. Per 8-cycle bin of t0's low 8 bits: count, mean own DMA time (tenths), the fewest and
 * most, how many read 80 or more, lost.
 *
 * READ IT LIKE THIS. Each machine's table against the other's, shifted by one constant (Timer 2 sits at a different offset from the
 * refresh on each): where the +10 starts and ends against where the loads' small costs are. No assertions.
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
static uint32_t s_t0;
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
    s_t0 = t0;
    write32(CHCR0, 0);
    return r;
}

/* n & 31 more cycles, one at a time: a jump into a run of 32 nops, n from its end (cached code, 1 cycle each). */
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
/* per bin of t0's low 8 bits /8 and per call of the pair: done, sum, fewest, most, 80 up, lost */
static uint16_t cnt[2][32], lo[2][32], hi[2][32], up[2][32], lost[2][32];
static uint32_t sum[2][32];
static void note(int k, const Row &r) {
    const int b = (int)((s_t0 & 0xFF) / 8);
    if (!r.done) { lost[k][b]++; return; }
    cnt[k][b]++; sum[k][b] += r.at;
    if (!lo[k][b] || r.at < lo[k][b]) lo[k][b] = (uint16_t)r.at;
    if (r.at > hi[k][b]) hi[k][b] = (uint16_t)r.at;
    if (r.at >= 80) up[k][b]++;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset21\n");
    printf("mdec-reset's one(), a call after a rest and the call behind it: own DMA time by t0's own clock /8. No assertions.\n\n");
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
        const Row r1 = one(zeroWait, zeroFlag, zeroFlag);
        note(0, r1);
        const Row r2 = one(zeroWait, zeroFlag, zeroFlag);
        note(1, r2);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("t0 /8     first: n  mean  lo- hi 80up lost    behind: n  mean  lo- hi 80up lost\n");
    for (int b = 0; b < 32; b++) {
        const unsigned m0 = cnt[0][b] ? (unsigned)(sum[0][b] * 10 / cnt[0][b]) : 0, m1 = cnt[1][b] ? (unsigned)(sum[1][b] * 10 / cnt[1][b]) : 0;
        printf("%3d-%3d     %4d %3u.%u %3d-%3d %4d %4d        %4d %3u.%u %3d-%3d %4d %4d\n", b * 8, b * 8 + 7,
               (int)cnt[0][b], m0 / 10, m0 % 10, (int)lo[0][b], (int)hi[0][b], (int)up[0][b], (int)lost[0][b],
               (int)cnt[1][b], m1 / 10, m1 % 10, (int)lo[1][b], (int)hi[1][b], (int)up[1][b], (int)lost[1][b]);
    }
    printf("\nmdec/mdec-reset21: Done.\n");
    for (;;) {}   /* park after Done. */
}
