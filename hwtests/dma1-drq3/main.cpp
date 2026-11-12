/*
 * dma/dma1-drq3 - which of five small things lets DMA1 take a second MDEC output: dma1-drq's tries all moved, dma1-drq2's only
 * its first.
 *
 * WHY (2026-11-12). On the second SCPH-5501 and the PSone, dma1-drq moved every try (18 of 18), and dma1-drq2 moved its FIRST
 * try only: the next 11 hung (MADR1 +0, no word written) in every case, its DPCR and its sled store alike. dma1-start too (its
 * first transfer left the console's MDEC output in outBuf, the rest hung). The two tests differ only in:
 *   R  the parameters written from a RAM buffer (dma1-drq) or as constants (dma1-drq2)
 *   0  MDEC1 read right after the parameters (dma1-drq's s0)
 *   1  MDEC1 read after the 30,000-cycle wait, before DMA1 is armed (dma1-drq's s1; dma1-start's one read)
 *   2  MDEC1 read after the transfer, before the MDEC reset (dma1-drq's s2)
 *   D  DPCR written again before each try (dma1-drq2)
 * This tries all 32 combinations, round-robin, twice (64 tries), and prints each try in the order it ran: a cause that is
 * per try shows by its case; a channel that wedges once and stays wedged shows as every try after one hanging.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, DPCR DMA0 and DMA1 on. Each try: outBuf filled with A5A5A5A5h,
 * CHCR0 / CHCR1 0, the MDEC reset, MDEC1 20000000h, one 15-bit DC-only macroblock's six words by the CPU, 30,000 cycles,
 * DMA1 1 x 32 (MADR1, BCR1, CHCR1 01000200h from C), Busy polled up to 200,000 cycles (the start bit cleared if it stays),
 * the MDEC reset. Per try: the case, Busy's cycles or STUCK, the words written, MADR1's end.
 *
 * READ IT LIKE THIS. A column of the case (R, 0, 1, 2, D) that every moving try has and every hung one lacks is the rule.
 * Everything hung after some try whatever its case: the channel wedged there (look at what that try did).
 *
 * THE RULE: run it here first: `node hwtests/run.js dma1-drq3`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define CHCR0   0x1F801088u
#define MADR1   0x1F801090u
#define BCR1    0x1F801094u
#define CHCR1   0x1F801098u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define SENT    0xA5A5A5A5u
#define TRIES   64

uint32_t outBuf[128] __attribute__((aligned(16)));
uint32_t inBuf[8] __attribute__((aligned(16)));
volatile uint32_t sink;

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { uint32_t s = 0; uint16_t a = t2(); while (s < cycles) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } }

struct Try { uint8_t c; bool stuck; uint32_t took, moved, madr; };
static Try res[TRIES];

static void one(uint32_t dpcr, int c, Try* r) {
    const bool fromRam = c & 1, s0 = c & 2, s1 = c & 4, s2 = c & 8, d = c & 16;
    for (int i = 0; i < 128; i++) outBuf[i] = SENT;
    if (d) write32(DPCR, dpcr);
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    write32(MDEC1, 0x20000000u);
    write32(MDEC0, (1u << 29) | (3u << 27) | 6u);
    if (fromRam) for (int i = 0; i < 6; i++) write32(MDEC0, inBuf[i]);
    else for (int b = 0; b < 6; b++) write32(MDEC0, 0xFE000400u | (uint32_t)(b * 16));
    if (s0) sink = read32(MDEC1);
    wait(30000);
    if (s1) sink = read32(MDEC1);
    write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
    write32(BCR1, (1u << 16) | 32u);
    write32(CHCR1, 0x01000200u);
    uint32_t s = 0; uint16_t a = t2();
    while ((read32(CHCR1) & 0x01000000u) && s < 200000) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; }
    r->c = (uint8_t)c;
    r->stuck = (read32(CHCR1) & 0x01000000u) != 0;
    r->took = s;
    if (r->stuck) write32(CHCR1, read32(CHCR1) & ~0x01000000u);
    r->madr = read32(MADR1) - ((uint32_t)outBuf & 0x1FFFFFu);
    if (s2) sink = read32(MDEC1);
    r->moved = 0;
    for (int i = 0; i < 128; i++) if (outBuf[i] != SENT) r->moved++;
    write32(MDEC1, 0x80000000u);
}

int main() {
    printf("\ndma/dma1-drq3\n");
    printf("DMA1 1 x 32 from an MDEC output, 64 tries in the order run: R params from RAM, 0 / 1 / 2 MDEC1 read after the params /\n");
    printf("before the arm / after the transfer, D DPCR written again. No assertions.\n\n");
    for (int b = 0; b < 6; b++) inBuf[b] = 0xFE000400u | (uint32_t)(b * 16);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), dp = dp0 | 0x88u;
    write16(T2_MODE, 0);
    write32(DPCR, dp);
    for (int t = 0; t < TRIES; t++) one(dp, t & 31, &res[t]);
    write32(DPCR, dp0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("DPCR at entry %08x\n", (unsigned)dp0);
    int moved = 0;
    for (int t = 0; t < TRIES; t++) {
        const Try& r = res[t];
        const int c = r.c;
        printf("  #%2d  %c%c%c%c%c  ", t + 1, c & 1 ? 'R' : '-', c & 2 ? '0' : '-', c & 4 ? '1' : '-', c & 8 ? '2' : '-', c & 16 ? 'D' : '-');
        if (r.stuck) printf("STUCK          "); else printf("cleared %6u ", (unsigned)r.took);
        printf(" wrote %3u  MADR1 +%u\n", (unsigned)r.moved, (unsigned)r.madr);
        if (!r.stuck) moved++;
    }
    printf("\n%d of %d tries moved\n", moved, TRIES);
    printf("\ndma/dma1-drq3: Done.\n");
    for (;;) VSync(0);
}
