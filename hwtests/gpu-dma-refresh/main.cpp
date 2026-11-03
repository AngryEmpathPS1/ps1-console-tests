/*
 * gpu/gpu-dma-refresh - a GPU DMA block alone in a timed span: where a DRAM refresh costs it, 4 cycles at a time (2026-11-03, for
 * the SCPH-5501).
 *
 * WHY. mdec-reset22 on the SCPH-5501 (two runs byte-identical): a refresh due before an MDEC-in (DMA0) block costs it nothing but its
 * own remainder right ahead of it, where v0.377.262 held it behind the DMA registers' stores and cost +2 for 44 cycles more
 * (experiments/_mkmdio.js takes that away). Applied to GPU blocks too, the same rule moves six of Sony's CPE samples' frame counts
 * both ways (the cpe gate: 4 better, 2 worse than their pins). Does a GPU block meet the refresh as the MDEC's does?
 *
 * WHAT IT DOES. GP1(04h) = 2 (DMA to GP0); per try MADR2 = 16 words of GP0(00h), BCR2 = one block of 16, then the span: a raw Timer 2
 * read, CHCR2 = 01000201h (SyncMode 1, from RAM, start), and a poll of raw Timer 2 and CHCR2 reads, all in registers - the only RAM
 * access inside it is the DMA's own. 4,096 tries, each started 0-31 single cycles later than the last (a jump into a run of nops),
 * interrupts off. Per 4-cycle bin of the span's first timer value: count, mean (tenths), fewest, most, lost.
 *
 * READ IT LIKE THIS. As mdec-reset22: a bigger start value is a refresh earlier in the span; the bins over the floor are the block's
 * refresh window. Each machine's table against the other's by one shift (experiments/mdec-r22cmp.js reads this layout). No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint32_t nops[16] __attribute__((aligned(16)));
struct Row { uint16_t at, a0; bool done; };
static __attribute__((noinline, noclone)) Row one() {
    write32(CHCR2, 0);
    write32(MADR2, (uint32_t)nops & 0x1FFFFFu);
    write32(BCR2, (1u << 16) | 16u);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    uint16_t a1;
    unsigned turns = 0;
    for (;;) {
        a1 = read16(T2_COUNT);
        if (!(read32(CHCR2) & 0x01000000u)) break;
        if (++turns > 200) break;
    }
    write32(CHCR2, 0);
    Row r; r.at = (uint16_t)(a1 - a0); r.a0 = a0; r.done = turns <= 200;
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

static uint16_t cnt[64], lo[64], hi[64], lost[64];
static uint32_t sum[64];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-refresh\n");
    printf("A 16-word GPU DMA block alone in the timed span: its time by the span's first timer value /4. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 16; i++) nops[i] = 0;   /* GP0(00h) */
    write32(DPCR, read32(DPCR) | 0x800u);       /* DMA2 enabled */
    write32(GP1, 0x04000002u);                  /* DMA direction: CPU to GP0 */
    one(); one();   /* cache it */
    for (int i = 0; i < 4096; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        sled((uint32_t)i * 11u);
        const Row r = one();
        const int b = (r.a0 & 0xFF) / 4;
        if (!r.done) { lost[b]++; continue; }
        cnt[b]++; sum[b] += r.at;
        if (!lo[b] || r.at < lo[b]) lo[b] = r.at;
        if (r.at > hi[b]) hi[b] = r.at;
    }
    write32(GP1, 0x04000000u);                  /* DMA direction off */
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("a0 /4         n  mean  lo- hi lost\n");
    for (int b = 0; b < 64; b++) {
        const unsigned m = cnt[b] ? (unsigned)(sum[b] * 10 / cnt[b]) : 0;
        printf("%3d-%3d     %4d %3u.%u %3d-%3d %4d\n", b * 4, b * 4 + 3, (int)cnt[b], m / 10, m % 10, (int)lo[b], (int)hi[b], (int)lost[b]);
    }
    printf("\ngpu/gpu-dma-refresh: Done.\n");
    for (;;) {}   /* park after Done. */
}
