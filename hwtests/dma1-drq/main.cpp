/*
 * dma/dma1-drq - when does DMA1 (MDEC.OUT) take an MDEC output that is already waiting? The question dma1-start could not
 * answer on a console.
 *
 * WHY (2026-11-12). hwtests/dma1-start on the second SCPH-5501 and the PSone (x6 each, byte-identical per console): with one
 * 15-bit macroblock decoded from CPU-written parameters and left 30,000 cycles in the output FIFO (status bit 31 clear), a
 * SyncMode 1 start (CHCR1 01000200h) moved NOTHING in ~3,850 tries - CHCR1 stayed Busy for 32,767 polls, the store cost the CPU
 * nothing (t - N 9, the control's), and outBuf kept RAM garbage (4a314a31 / 7db27db2; this core writes 42104210, DC 0's grey).
 * This core moves the block (t - N 239). Which rule the console keeps is the question; each case below changes one thing.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, DPCR DMA0 and DMA1 on. outBuf is filled with A5A5A5A5h before each
 * try, so a word DMA1 wrote is told from one it did not. One macroblock: six DC-only 15-bit blocks (q 1, DC 0, 16 .. 80, end
 * code); "6 words" is dma1-start's exact payload, "32 words" the same padded with FE00h as psx-spx asks for DMA0. Cases:
 *   A   reset, MDEC1 20000000h, 6 words by the CPU, wait 30,000, DMA1 1 x 32       (dma1-start, exactly)
 *   B   as A with 2,000 cycles between the reset and MDEC1 60000000h (both requests, as Sony's library sets them)
 *   C   reset, MDEC1 60000000h, DMA1 1 x 32 ARMED FIRST, then 6 words by the CPU     (the games' order: DMA1 before the data)
 *   D   reset, 60000000h, DMA1 4 x 32 armed, 32 words by DMA0 (CHCR0 01000201h)        (a movie's path: the positive control)
 *   E   reset, 60000000h, 32 words by DMA0, DMA0 done, wait 30,000, then DMA1 1 x 32  (armed after, the data by DMA)
 *   F   as A with 32 words by the CPU                                                 (the padding alone)
 * Each case three times. Per try: MDEC1 status after the parameters (s0) and right before DMA1 is armed (s1), the cycles until
 * CHCR1's Busy cleared (or "stuck" after 200,000: the start bit is then cleared), MDEC1 after (s2), DMA1's MADR after (how far
 * it got), the words of outBuf DMA1 wrote and the first. Status bits (psx-spx): 31 out FIFO empty, 29 busy, 27 data-out
 * request, 18-16 current block, 15-0 words left - 1.
 *
 * READ IT LIKE THIS. D moving 128 words is the console working as movies need. A stuck and C moving: DMA1 must be armed before
 * the output appears (an edge, or a request the MDEC raises once). B moving where A does not: the write right after a reset
 * is lost. F or E moving where A does not: the padding, or the parameters' path. s1 bit 27 says whether the MDEC asks.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma1-drq`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define MADR0   0x1F801080u
#define BCR0    0x1F801084u
#define CHCR0   0x1F801088u
#define MADR1   0x1F801090u
#define BCR1    0x1F801094u
#define CHCR1   0x1F801098u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define SENT    0xA5A5A5A5u

uint32_t outBuf[128] __attribute__((aligned(16)));
uint32_t inBuf[32] __attribute__((aligned(16)));

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { uint32_t s = 0; uint16_t a = t2(); while (s < cycles) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } }

struct Try { uint32_t s0, s1, s2, madr, took, moved, first; bool stuck; };

static void fill(int words) {
    for (int b = 0; b < 6; b++) inBuf[b] = 0xFE000400u | (uint32_t)(b * 16);   /* q 1 + a DC, then the end code */
    for (int i = 6; i < 32; i++) inBuf[i] = 0xFE00FE00u;
    (void)words;
}
static void armOut(uint32_t blocks) {
    write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
    write32(BCR1, (blocks << 16) | 32u);
    write32(CHCR1, 0x01000200u);
}
static void cmdCpu(int words) {
    write32(MDEC0, (1u << 29) | (3u << 27) | (uint32_t)words);
    for (int i = 0; i < words; i++) write32(MDEC0, inBuf[i]);
}
static void cmdDma() {
    write32(MDEC0, (1u << 29) | (3u << 27) | 32u);
    write32(MADR0, (uint32_t)inBuf & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    write32(CHCR0, 0x01000201u);
}
/* Busy cleared within `limit` cycles: the cycles it took; else stuck, and the start bit cleared. */
static void pollOut(Try* r, uint32_t limit) {
    uint32_t s = 0; uint16_t a = t2();
    while ((read32(CHCR1) & 0x01000000u) && s < limit) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; }
    r->stuck = (read32(CHCR1) & 0x01000000u) != 0;
    r->took = s;
    if (r->stuck) write32(CHCR1, read32(CHCR1) & ~0x01000000u);
}
static void waitIn() { uint32_t s = 0; uint16_t a = t2(); while ((read32(CHCR0) & 0x01000000u) && s < 200000) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } write32(CHCR0, read32(CHCR0) & ~0x01000000u); }

static void one(char c, Try* r) {
    for (int i = 0; i < 128; i++) outBuf[i] = SENT;
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    switch (c) {
    case 'A': write32(MDEC1, 0x20000000u); cmdCpu(6); r->s0 = read32(MDEC1); wait(30000); r->s1 = read32(MDEC1); armOut(1); break;
    case 'B': wait(2000); write32(MDEC1, 0x60000000u); cmdCpu(6); r->s0 = read32(MDEC1); wait(30000); r->s1 = read32(MDEC1); armOut(1); break;
    case 'C': write32(MDEC1, 0x60000000u); r->s1 = read32(MDEC1); armOut(1); cmdCpu(6); r->s0 = read32(MDEC1); break;
    case 'D': write32(MDEC1, 0x60000000u); r->s1 = read32(MDEC1); armOut(4); cmdDma(); waitIn(); r->s0 = read32(MDEC1); break;
    case 'E': write32(MDEC1, 0x60000000u); cmdDma(); waitIn(); r->s0 = read32(MDEC1); wait(30000); r->s1 = read32(MDEC1); armOut(1); break;
    case 'F': write32(MDEC1, 0x20000000u); cmdCpu(32); r->s0 = read32(MDEC1); wait(30000); r->s1 = read32(MDEC1); armOut(1); break;
    }
    pollOut(r, 200000);
    r->s2 = read32(MDEC1);
    r->madr = read32(MADR1) - ((uint32_t)outBuf & 0x1FFFFFu);
    r->moved = 0;
    for (int i = 0; i < 128; i++) if (outBuf[i] != SENT) r->moved++;
    r->first = outBuf[0];
    write32(MDEC1, 0x80000000u);
}

static const char CASES[] = "ABCDEF";
static Try res[6][3];

int main() {
    printf("\ndma/dma1-drq\n");
    printf("DMA1 (MDEC.OUT, CHCR 01000200h) and an MDEC output: which order and path lets DMA1 take it. No assertions.\n");
    printf("s0 = MDEC1 after the parameters, s1 = before DMA1 armed (C, D: after the reset, before the data), s2 = after.\n\n");
    fill(32);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000088u);          /* DMA0 and DMA1 on */
    for (int k = 0; k < 6; k++) for (int t = 0; t < 3; t++) one(CASES[k], &res[k][t]);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int k = 0; k < 6; k++)
        for (int t = 0; t < 3; t++) {
            const Try& r = res[k][t];
            printf("  %c%d  s0 %08x  s1 %08x  ", CASES[k], t + 1, (unsigned)r.s0, (unsigned)r.s1);
            if (r.stuck) printf("Busy STUCK      "); else printf("Busy cleared %6u", (unsigned)r.took);
            printf("  s2 %08x  MADR1 +%4u  wrote %3u words, first %08x\n", (unsigned)r.s2, (unsigned)r.madr, (unsigned)r.moved, (unsigned)r.first);
        }
    printf("\ndma/dma1-drq: Done.\n");
    for (;;) VSync(0);
}
