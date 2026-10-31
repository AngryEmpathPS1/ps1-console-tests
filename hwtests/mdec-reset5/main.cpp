/*
 * mdec/mdec-reset5 - is mdec-reset's N 0 lost because of N 0, or because it comes first? (2026-10-31, for the SCPH-5501)
 *
 * WHY. mdec-reset2 and 4: a write 7 cycles or more behind the reset lands, from any of five states. mdec-reset's N 0 writes
 * its enables 9 cycles behind and loses them, in all 24 runner logs, while its B 0, a command at 8 on the same code, is
 * taken. Time alone cannot say that. But N 0 is the very first case the program runs, its warm-up call the first thing
 * it does. So this is mdec-reset's one() word for word, called in a chosen order with every call printed.
 *
 * WHAT IT DOES. one(wait, cmdFirst, false) from mdec-reset, the same source, in this order: N 0 six times; N 1 twice; N 0
 * twice; N 4096 twice; N 0 twice. Then the same with one call to the B case first. Every call's status and result.
 *
 * READ IT LIKE THIS. Lost only on the first calls is a start-up matter: what is lost is the first reset's, or the cold
 * cache's. Lost on every N 0 is N 0, and the time it takes is not what the instructions say. No assertions.
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
static Row one(uint32_t wait, bool cmdFirst, bool twice) {
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
    r.done = false; r.at = 0;
    for (;;) {
        const uint32_t t = now();
        if (!(read32(CHCR0) & 0x01000000u)) { r.done = true; r.at = t - t0; break; }
        if (t - t0 > 67737u) break;
    }
    write32(CHCR0, 0);
    return r;
}

struct Call { uint16_t wait; bool cmdFirst; };
static const Call ORDER[] = {
    {0, false}, {0, false}, {0, false}, {0, false}, {0, false}, {0, false},
    {1, false}, {1, false}, {0, false}, {0, false}, {4096, false}, {4096, false}, {0, false}, {0, false},
    {0, true}, {0, true}, {0, false}, {0, false}, {0, true}, {0, false},
};
static const int CALLS = sizeof(ORDER) / sizeof(ORDER[0]);

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset5\n");
    printf("mdec-reset's one(), in a chosen order, every call printed. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);

    static Row rows[CALLS];
    for (int i = 0; i < CALLS; i++) rows[i] = one(ORDER[i].wait, ORDER[i].cmdFirst, false);
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    for (int i = 0; i < CALLS; i++) {
        printf("%2d: %c N %4u: status %08x  DMA0 ", i, ORDER[i].cmdFirst ? 'B' : 'A', (unsigned)ORDER[i].wait, (unsigned)rows[i].status);
        if (rows[i].done) printf("done after %u cycles\n", (unsigned)rows[i].at);
        else printf("STILL BUSY after 2 ms\n");
    }
    printf("\nmdec/mdec-reset5: Done.\n");
    for (;;) {}   /* park after Done. */
}
