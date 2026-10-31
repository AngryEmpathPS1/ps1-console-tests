/*
 * mdec/mdec-reset8 - mdec-reset with N 0 all through its table and nothing else changed (2026-10-31, for the SCPH-5501).
 *
 * WHY. mdec-reset's N 0, the first row, is lost; with N 1 first (mdec-reset6) nothing is, on the same code. mdec-reset7 showed
 * the reset's length does not depend on what the MDEC was doing. So is it every N 0 that follows an N 0, or only the first?
 * The rows print the N they ran. Every instruction stays at its address, only the table moves. No assertions.
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

static const uint32_t WAITS[] = { 0, 0, 0, 0, 1, 0, 0, 2, 0, 0, 4, 0, 0, 8 };   /* mdec-reset8 */

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset8\n");
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
    printf("\nmdec/mdec-reset8: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
