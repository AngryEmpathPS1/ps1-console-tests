/*
 * mdec/mdec-reset - how long the MDEC's reset lasts: what a control write right after it does (2026-10-05, for the
 * SCPH-9001).
 *
 * WHY. dma-rates' MDEC IN cases write MDEC1 = 80000000h (reset), MDEC1 = 60000000h (data-in and data-out requests
 * on), the command (40000001h, the quantisation tables) and then start DMA0 with the 32 table words - and on BOTH
 * consoles of console day every one of those transfers never finished ("GAVE UP: still busy (channel stopped)"),
 * where this emulator finishes it in ~270 cycles. The MDEC tests that do work on the consoles (mdec-time,
 * mdec-leftover, mdec-mono-out) reset and then WAIT - for the command-busy bit to clear - before their next write.
 * So the reset probably takes time, and a control write inside it is lost: the request enables never set, DMA0 never
 * asked for data. psx-spx (MDEC1 bit 31): "Reset MDEC (0=No change, 1=Abort any command, and set
 * status=80040000h)" - how long it takes it does not say. This core's reset is instant.
 *
 * WHAT IT DOES. For each wait N (0, 1, 2, 4, 8 ... 4,096 cycles, Timer 2 at the system clock): reset, wait N, write
 * the request enables, read the status (MDEC1: bit 28 data-in request, 27 data-out request), write the command, start
 * DMA0 with the 32 table words in one block, and poll its CHCR for up to 2 ms: done, and when, or still busy. Then
 * the same with the command written BEFORE the enables (the order Sony's libpress uses is open here), and once with
 * the enables written twice (at 0 and after the wait). Every case starts from a channel stopped by hand.
 *
 * READ IT LIKE THIS. The smallest N at which the transfer finishes is the reset's length, as the CPU sees it.
 *
 * ANSWERED by the SCPH-5501 and 7501 (console day; v0.377.30, src/psx.js DMA_MDEC_IN_START): every case done after 75-85
 * cycles - no lost control write, and no 230-cycle start. AT v0.377.30 every case 70; AT v0.377.38 (DMA_MDEC_IN_START 5)
 * every case 75, the consoles' 200 of 252. NOT THE WHOLE ANSWER: row N 0 of the first table came back "DMA0 STILL BUSY",
 * status 60000000h, on later runs of the 5000, 5501, 7001 and 7502: a control write with no gap behind the reset IS lost
 * when the two land close enough, which a program's cache state decides (STATUS "MDEC1's enable is lost behind a reset").
 * THE RULE: run here first. ON THIS CORE AT v0.366 (`node hwtests/run.js mdec-reset`): every case done after 300
 * cycles (305 once) at every N - the instant reset. The status after the enables reads 80040000h (no request with no
 * command), B004001Fh with the command first (the data-in request up, 1Fh words to come).
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

struct Row { uint32_t status, at; bool done; };
/* One case: reset, the enables after `wait` (and at 0 too with `twice`), the command before or after them, DMA0. */
static Row one(uint32_t wait, bool cmdFirst, bool twice) {
    write32(CHCR0, 0);                                         /* the channel stopped */
    write32(MDEC1, 0x80000000u);                                /* reset */
    if (twice) write32(MDEC1, 0x60000000u);
    if (cmdFirst) write32(MDEC0, 0x40000001u);
    if (wait) waitCycles(wait);
    write32(MDEC1, 0x60000000u);                                /* data-in and data-out requests on */
    Row r;
    r.status = read32(MDEC1);
    if (!cmdFirst) write32(MDEC0, 0x40000001u);                 /* the quantisation tables, 32 words */
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    const uint32_t t0 = now();
    write32(CHCR0, 0x01000201u);
    r.done = false; r.at = 0;
    for (;;) {
        const uint32_t t = now();
        if (!(read32(CHCR0) & 0x01000000u)) { r.done = true; r.at = t - t0; break; }
        if (t - t0 > 67737u) break;                             /* 2 ms */
    }
    write32(CHCR0, 0);
    return r;
}

static const uint32_t WAITS[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset\n");
    printf("A control write right after the MDEC's reset: the request enables, then a table DMA. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);                        /* DMA0 on */
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);

    static Row rows[3][14];
    for (int order = 0; order < 2; order++)
        for (unsigned k = 0; k < sizeof(WAITS) / sizeof(WAITS[0]); k++) {
            one(WAITS[k], order == 1, false);                  /* once to warm the cache */
            rows[order][k] = one(WAITS[k], order == 1, false);
        }
    for (unsigned k = 0; k < sizeof(WAITS) / sizeof(WAITS[0]); k++) { one(WAITS[k], false, true); rows[2][k] = one(WAITS[k], false, true); }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    static const char* NAMES[3] = { "reset, wait N, enables, command, DMA0", "reset, command, wait N, enables, DMA0",
                                    "reset, enables, wait N, enables again, command, DMA0" };
    for (int order = 0; order < 3; order++) {
        printf("%s:\n", NAMES[order]);
        for (unsigned k = 0; k < sizeof(WAITS) / sizeof(WAITS[0]); k++) {
            const Row& r = rows[order][k];
            printf("  N %4u: status %08x (in %d, out %d)   DMA0 ", (unsigned)WAITS[k], (unsigned)r.status,
                   (int)((r.status >> 28) & 1), (int)((r.status >> 27) & 1));
            if (r.done) printf("done after %u cycles\n", (unsigned)r.at);
            else printf("STILL BUSY after 2 ms\n");
        }
    }
    printf("\nmdec/mdec-reset: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
