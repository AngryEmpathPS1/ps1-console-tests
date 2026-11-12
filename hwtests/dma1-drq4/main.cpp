/*
 * dma/dma1-drq4 - what keeps DMA1 from wedging after its first MDEC transfer, and what frees it once wedged.
 *
 * WHY (2026-11-12). On the second SCPH-5501 and the PSone: dma1-drq moved 18 of 18 tries; dma1-drq2 moved 1 of 12 and
 * dma1-drq3 1 of 64 (x2 each console, byte-identical) - the first try moves, then every try hangs (MADR1 +0, no word written)
 * whatever its own recipe. So the channel WEDGES after a transfer, and nothing a later try does frees it. dma1-drq's every try
 * did four things dma1-drq3's first did not, so one of them, done with a transfer, keeps it from wedging:
 *   R  the parameters written from a RAM buffer
 *   0  MDEC1 read right after the parameters
 *   1  MDEC1 read after the 30,000-cycle wait, before the arm (dma1-start did only this, and wedged)
 *   2  MDEC1 read after the transfer, before the MDEC reset
 * Each run has one fresh channel, so the order is adaptive: dma1-drq's whole recipe (R012) four times - it must move - then
 * one factor dropped for two tries, two whole-recipe probes after; the next factor; and so on. The first drop after which the
 * probes hang names a factor the channel needs. Once wedged, four ways out, each followed by one whole-recipe probe:
 *   U1  DPCR's DMA1 enable off, then on
 *   U2  the MDEC's output drained by CPU reads of MDEC0 until its FIFO reads empty, then the MDEC reset
 *   U3  DICR's DMA1 flag written back (acknowledged)
 *   U4  CHCR1 started in SyncMode 0 with the manual trigger (11000000h), one word, then stopped
 *
 * WHAT IT DOES. As dma1-drq3: interrupts off, Timer 2 at the system clock, DPCR DMA0 and DMA1 on, outBuf filled with
 * A5A5A5A5h a try, the MDEC reset, MDEC1 20000000h, one 15-bit DC-only macroblock's six words by the CPU, 30,000 cycles, DMA1
 * 1 x 32, Busy polled up to 200,000 cycles (its start bit cleared if it stays), the MDEC reset. Each try printed in run order.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma1-drq4`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define DICR    0x1F8010F4u
#define CHCR0   0x1F801088u
#define MADR1   0x1F801090u
#define BCR1    0x1F801094u
#define CHCR1   0x1F801098u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define SENT    0xA5A5A5A5u
#define MAXT    48

enum { F_R = 1, F_0 = 2, F_1 = 4, F_2 = 8, FULL = 15 };

uint32_t outBuf[128] __attribute__((aligned(16)));
uint32_t inBuf[8] __attribute__((aligned(16)));
volatile uint32_t sink;

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { uint32_t s = 0; uint16_t a = t2(); while (s < cycles) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } }

struct Try { const char* what; uint8_t c; bool stuck; uint32_t moved, madr, dicr; };
static Try res[MAXT];
static int nres;

static bool one(const char* what, int c) {
    Try* r = &res[nres < MAXT ? nres++ : MAXT - 1];
    for (int i = 0; i < 128; i++) outBuf[i] = SENT;
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    write32(MDEC1, 0x20000000u);
    write32(MDEC0, (1u << 29) | (3u << 27) | 6u);
    if (c & F_R) for (int i = 0; i < 6; i++) write32(MDEC0, inBuf[i]);
    else for (int b = 0; b < 6; b++) write32(MDEC0, 0xFE000400u | (uint32_t)(b * 16));
    if (c & F_0) sink = read32(MDEC1);
    wait(30000);
    if (c & F_1) sink = read32(MDEC1);
    write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
    write32(BCR1, (1u << 16) | 32u);
    write32(CHCR1, 0x01000200u);
    uint32_t s = 0; uint16_t a = t2();
    while ((read32(CHCR1) & 0x01000000u) && s < 200000) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; }
    r->what = what; r->c = (uint8_t)c;
    r->stuck = (read32(CHCR1) & 0x01000000u) != 0;
    if (r->stuck) write32(CHCR1, read32(CHCR1) & ~0x01000000u);
    r->madr = read32(MADR1) - ((uint32_t)outBuf & 0x1FFFFFu);
    if (c & F_2) sink = read32(MDEC1);
    r->moved = 0;
    for (int i = 0; i < 128; i++) if (outBuf[i] != SENT) r->moved++;
    r->dicr = read32(DICR);
    write32(MDEC1, 0x80000000u);
    return !r->stuck;
}

static bool wedged;
/* a factor dropped twice, then two whole-recipe probes: true if the probes hung (the channel wedged) */
static bool drop(const char* what, int c) {
    one(what, c); one(what, c);
    const bool a = one("probe", FULL), b = one("probe", FULL);
    return !a && !b;
}

int main() {
    printf("\ndma/dma1-drq4\n");
    printf("DMA1 1 x 32 from an MDEC output: dma1-drq's whole recipe, then one factor dropped at a time, then ways out of a wedge.\n");
    printf("Factors: R params from RAM, 0 / 1 / 2 MDEC1 read after the params / before the arm / after the transfer. No assertions.\n\n");
    for (int b = 0; b < 6; b++) inBuf[b] = 0xFE000400u | (uint32_t)(b * 16);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), dp = dp0 | 0x88u, di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(DPCR, dp);
    for (int i = 0; i < 4; i++) one("whole", FULL);
    if (!wedged) wedged = drop("drop 2", FULL & ~F_2);
    if (!wedged) wedged = drop("drop 0", FULL & ~F_0);
    if (!wedged) wedged = drop("drop R", FULL & ~F_R);
    if (!wedged) wedged = drop("drop 1", FULL & ~F_1);
    if (!wedged) wedged = drop("none", 0);
    if (wedged) {
        write32(DPCR, dp & ~0x80u); wait(1000); write32(DPCR, dp);
        one("after U1 DPCR", FULL);
        write32(MDEC1, 0x80000000u); write32(MDEC1, 0x20000000u);
        write32(MDEC0, (1u << 29) | (3u << 27) | 6u);
        for (int i = 0; i < 6; i++) write32(MDEC0, inBuf[i]);
        wait(30000);
        for (int i = 0; i < 4096 && !(read32(MDEC1) & 0x80000000u); i++) sink = read32(MDEC0);
        write32(MDEC1, 0x80000000u);
        one("after U2 drain", FULL);
        write32(DICR, read32(DICR));
        one("after U3 DICR", FULL);
        write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu); write32(BCR1, 1u);
        write32(CHCR1, 0x11000000u); wait(2000); write32(CHCR1, 0);
        one("after U4 SM0", FULL);
    }
    write32(DPCR, dp0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("DPCR at entry %08x, DICR %08x\n", (unsigned)dp0, (unsigned)di0);
    int moved = 0;
    for (int t = 0; t < nres; t++) {
        const Try& r = res[t];
        const int c = r.c;
        printf("  #%2d  %-14s %c%c%c%c  %s  wrote %3u  MADR1 +%-3u  DICR %08x\n", t + 1, r.what, c & F_R ? 'R' : '-', c & F_0 ? '0' : '-', c & F_1 ? '1' : '-', c & F_2 ? '2' : '-',
               r.stuck ? "STUCK " : "moved ", (unsigned)r.moved, (unsigned)r.madr, (unsigned)r.dicr);
        if (!r.stuck) moved++;
    }
    printf("\n%d of %d tries moved; %s\n", moved, nres, wedged ? "the channel wedged (see the drop before the hung probes)" : "it never wedged");
    printf("\ndma/dma1-drq4: Done.\n");
    for (;;) VSync(0);
}
