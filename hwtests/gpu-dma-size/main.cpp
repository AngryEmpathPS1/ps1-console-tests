/*
 * gpu/gpu-dma-size - one GPU DMA block of 1 to 64 words alone in a timed span: its floor and mean by size (2026-11-03, for the
 * SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-refresh on the SCPH-5501 (two runs byte-identical): a 16-word block from RAM, timed from a raw Timer 2 read
 * through CHCR2's store to the poll that reads it done, has a floor of 43 cycles there and 38 here; this core traces it as CHCR2's
 * store at 13, the block 18-38 (16 words and DMA_SM1_GPU_BURST), the poll's timer read held to the block's end. One size cannot
 * tell a start latency from a cost a word or a hold after the block. gpu-a0rate2 read whole transfers ~13 cycles slow here.
 *
 * WHAT IT DOES. gpu-dma-refresh's span (GP1(04h) = 2, GP0(00h) words, SyncMode 1, one block) at 1, 2, 4, 8, 16, 32 and 64 words,
 * 512 tries a size, each started 0-31 single cycles later than the last (a jump into a run of nops), interrupts off. Per size: the
 * fewest, the mean (tenths) and the most cycles, and lost tries.
 *
 * READ IT LIKE THIS. The floors against the size: the slope is the cost a word, the line's start the fixed cost; the mean over the
 * floor is the refresh's share. Each machine's against the other's. No assertions.
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

static uint32_t nops[64] __attribute__((aligned(16)));
struct Row { uint16_t at; bool done; };
static __attribute__((noinline, noclone)) Row one(uint32_t words) {
    write32(CHCR2, 0);
    write32(MADR2, (uint32_t)nops & 0x1FFFFFu);
    write32(BCR2, (1u << 16) | words);
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
    Row r; r.at = (uint16_t)(a1 - a0); r.done = turns <= 200;
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

static const uint32_t SIZES[7] = { 1, 2, 4, 8, 16, 32, 64 };

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-size\n");
    printf("One GPU DMA block of 1-64 words alone in the timed span: fewest, mean and most cycles by size. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 64; i++) nops[i] = 0;   /* GP0(00h) */
    write32(DPCR, read32(DPCR) | 0x800u);
    write32(GP1, 0x04000002u);
    one(1); one(1);   /* cache it */
    printf("words   tries  fewest   mean   most  lost\n");
    for (int s = 0; s < 7; s++) {
        uint32_t lo = 0xFFFF, hi = 0, sum = 0, n = 0, lost = 0;
        for (int i = 0; i < 512; i++) {
            waitCycles(300 + (uint32_t)(i * 37 % 256));
            sled((uint32_t)i * 11u);
            const Row r = one(SIZES[s]);
            if (!r.done) { lost++; continue; }
            n++; sum += r.at;
            if (r.at < lo) lo = r.at;
            if (r.at > hi) hi = r.at;
        }
        const unsigned m = n ? (unsigned)(sum * 10 / n) : 0;
        printf("%5u   %5u   %5u  %3u.%u  %5u  %4u\n", (unsigned)SIZES[s], (unsigned)n, (unsigned)(n ? lo : 0), m / 10, m % 10, (unsigned)hi, (unsigned)lost);
    }
    write32(GP1, 0x04000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-size: Done.\n");
    for (;;) {}   /* park after Done. */
}
