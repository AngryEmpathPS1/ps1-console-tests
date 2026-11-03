/*
 * gpu/gpu-dma-knee - where a GPU DMA block ends against the CPU: N single cycles between CHCR2's store and a raw timer read
 * (2026-11-03 night, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-size on the SCPH-5501 (two runs byte-identical): one SyncMode 1 block of 1-64 words, timed from a raw
 * Timer 2 read through CHCR2's store to the poll that reads it done, has a floor of 23 + a cycle a word there and 22 + a cycle a
 * word here. v0.377.263 traces its span as CHCR2's store at 13, the block 18 to 18 + w + 4 (DMA_SM1_GPU_LATENCY 5,
 * DMA_SM1_GPU_BURST 4), the timer read held to the block's end. A latency of 6 fits that test but makes fuzz-dma and the
 * gpu-a0rate tests worse (STATUS v0.377.263's follow-up), so the cycle is elsewhere: the block's end, or the read held after it.
 *
 * WHAT IT DOES. One 16-word block of GP0(00h), as gpu-dma-size; after CHCR2's store, N single cycles (a jump into a run of nops,
 * N = 0..31), then a raw Timer 2 read, then the poll as before. 64 tries a value of N at spread phases, interrupts off. Per N:
 * the fewest and the mean cycles from the first timer read to the read after the N cycles, and to the poll that reads it done.
 *
 * READ IT LIKE THIS. While the block holds the bus the read waits for its end, so the first column stays flat; once N carries the
 * read past the block it rises a cycle an N. The N at the knee is where the block ends; the flat value is the read's time after
 * it. Each machine's table against the other's. No assertions.
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

static uint32_t nops[16] __attribute__((aligned(16)));
struct Row { uint16_t first, done; bool ok; };
static __attribute__((noinline, noclone)) Row one(uint32_t n) {
    write32(CHCR2, 0);
    write32(MADR2, (uint32_t)nops & 0x1FFFFFu);
    write32(BCR2, (1u << 16) | 16u);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    sled(n);
    const uint16_t a1 = read16(T2_COUNT);
    uint16_t a2;
    unsigned turns = 0;
    for (;;) {
        a2 = read16(T2_COUNT);
        if (!(read32(CHCR2) & 0x01000000u)) break;
        if (++turns > 200) break;
    }
    write32(CHCR2, 0);
    Row r; r.first = (uint16_t)(a1 - a0); r.done = (uint16_t)(a2 - a0); r.ok = turns <= 200;
    return r;
}

static uint16_t lo1[32], lo2[32], lost[32];
static uint32_t sum1[32], sum2[32], cnt[32];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-knee\n");
    printf("A 16-word GPU DMA block, N single cycles after CHCR2's store a timer read: fewest and mean cycles by N. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 16; i++) nops[i] = 0;
    write32(DPCR, read32(DPCR) | 0x800u);
    write32(GP1, 0x04000002u);
    one(0); one(31);   /* cache it */
    for (int i = 0; i < 64 * 32; i++) {
        const uint32_t n = (uint32_t)(i & 31);
        waitCycles(300 + (uint32_t)((i >> 5) * 37 % 256));
        sled((uint32_t)(i >> 5) * 11u);
        const Row r = one(n);
        if (!r.ok) { lost[n]++; continue; }
        cnt[n]++; sum1[n] += r.first; sum2[n] += r.done;
        if (!lo1[n] || r.first < lo1[n]) lo1[n] = r.first;
        if (!lo2[n] || r.done < lo2[n]) lo2[n] = r.done;
    }
    write32(GP1, 0x04000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf(" N   tries   read after N: fewest  mean    done: fewest  mean   lost\n");
    for (int n = 0; n < 32; n++) {
        const unsigned m1 = cnt[n] ? (unsigned)(sum1[n] * 10 / cnt[n]) : 0, mm = cnt[n] ? (unsigned)(sum2[n] * 10 / cnt[n]) : 0;
        printf("%2d   %5u                %5u  %3u.%u          %5u  %3u.%u  %4u\n", n, (unsigned)cnt[n], (unsigned)lo1[n], m1 / 10, m1 % 10,
               (unsigned)lo2[n], mm / 10, mm % 10, (unsigned)lost[n]);
    }
    printf("\ngpu/gpu-dma-knee: Done.\n");
    for (;;) {}   /* park after Done. */
}
