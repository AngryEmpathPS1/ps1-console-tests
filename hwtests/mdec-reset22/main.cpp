/*
 * mdec/mdec-reset22 - mdec-reset's table DMA alone in the timed span: where a DRAM refresh costs it, 4 cycles at a time (2026-11-03,
 * for the SCPH-5501).
 *
 * WHY. mdec-reset20 and 21 on the SCPH-5501 (two runs each byte-identical) against v0.377.262: the refresh's cost around one()'s
 * 32-word table DMA has the same span on both (~48 cycles) but not the same shape - the console +6, +8, then +10 for 24 and nothing
 * on the stores behind it, with a ~40-cycle short mode just ahead; this core +7, +10 for 24, then +4.5 and +2.7 on the stores. Their
 * timed span holds the clock's RAM loads and stores and the result's stores too, so which part is the block's own cannot be told.
 *
 * WHAT IT DOES. one() as mdec-reset's up to the DMA's start; the span is then a raw Timer 2 read, the CHCR0 store, and a poll of raw
 * Timer 2 and CHCR0 reads, all in registers: the only RAM access inside it is the DMA's own. 2,048 pairs (a warm call after a rest,
 * the call behind it), interrupts off. Per 4-cycle bin of the span's first timer value: count, mean (tenths), fewest, most, lost.
 *
 * READ IT LIKE THIS. A bigger start value is a refresh that falls earlier in the span. The bins where the time is over its minimum are
 * the block's refresh window, and their values its cost there. Each machine's table against the other's by one shift. No assertions.
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
struct Row { uint32_t status; uint16_t at, a0; bool done; };
static __attribute__((noinline, noclone)) Row one(uint32_t wait, bool cmdFirst, bool twice) {
    /* mdec-reset's front unchanged: its untaken branches keep the enable 7 cycles and more behind the reset (a first build that
     * wrote them back to back lost every call on both machines - the reset's deaf cycles, v0.377.227) */
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
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR0, 0x01000201u);
    uint16_t a1;
    unsigned turns = 0;
    for (;;) {
        a1 = read16(T2_COUNT);
        if (!(read32(CHCR0) & 0x01000000u)) break;
        if (++turns > 200) break;
    }
    write32(CHCR0, 0);
    r.at = (uint16_t)(a1 - a0); r.a0 = a0; r.done = turns <= 200;
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
/* per bin of the span's first timer value's low 8 bits /4 and per call of the pair: done, sum, fewest, most, lost */
static uint16_t cnt[2][64], lo[2][64], hi[2][64], lost[2][64];
static uint32_t sum[2][64];
static void note(int k, const Row &r) {
    const int b = (r.a0 & 0xFF) / 4;
    if (!r.done) { lost[k][b]++; return; }
    cnt[k][b]++; sum[k][b] += r.at;
    if (!lo[k][b] || r.at < lo[k][b]) lo[k][b] = r.at;
    if (r.at > hi[k][b]) hi[k][b] = r.at;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset22\n");
    printf("mdec-reset's table DMA alone in the timed span: its time by the span's first timer value /4. No assertions.\n\n");
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
        const Row r2 = one(zeroWait, zeroFlag, zeroFlag);
        note(0, r1); note(1, r2);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("a0 /4     first: n  mean  lo- hi lost    behind: n  mean  lo- hi lost\n");
    for (int b = 0; b < 64; b++) {
        const unsigned m0 = cnt[0][b] ? (unsigned)(sum[0][b] * 10 / cnt[0][b]) : 0, m1 = cnt[1][b] ? (unsigned)(sum[1][b] * 10 / cnt[1][b]) : 0;
        printf("%3d-%3d     %4d %3u.%u %3d-%3d %4d        %4d %3u.%u %3d-%3d %4d\n", b * 4, b * 4 + 3,
               (int)cnt[0][b], m0 / 10, m0 % 10, (int)lo[0][b], (int)hi[0][b], (int)lost[0][b],
               (int)cnt[1][b], m1 / 10, m1 % 10, (int)lo[1][b], (int)hi[1][b], (int)lost[1][b]);
    }
    printf("\nmdec/mdec-reset22: Done.\n");
    for (;;) {}   /* park after Done. */
}
