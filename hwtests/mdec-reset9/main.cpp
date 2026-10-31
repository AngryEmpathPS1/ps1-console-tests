/*
 * mdec/mdec-reset9 - is mdec-reset's lost row the DRAM refresh? (2026-10-31, for the SCPH-5501)
 *
 * WHY. Under the runner mdec-reset's N 0 is lost every time, 24 logs of 24 and 5 more today, and mdec-reset6 and 8, the same
 * instructions with a name a character longer, never lose it. The reset's own length is not the cause: mdec-reset7 gave it
 * the same 7 cycles from every state. So the reading: the runner starts every test locked to the refresh, one more
 * character printed moves the first call, and the original's first call has a refresh land on the stack saves at the top of
 * one(). The stores wait, the queue backs up behind them, and the reset leaves the queue late, with the enable right
 * behind it. Then a program whose stores are in that place loses the write at one phase of 256 and no other.
 *
 * WHAT IT DOES. mdec-reset's one(), N 0, 4,096 times, each from a different phase of Timer 2, whose 16 bits are a whole
 * number of 256-cycle refreshes. Calls and lost calls by phase in bins of 4 cycles.
 *
 * READ IT LIKE THIS. Lost calls in a few bins and none in the rest is the refresh. Lost calls spread evenly is not. No
 * assertions.
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
static __attribute__((noinline, noclone)) Row one(uint32_t wait, bool cmdFirst, bool twice) {
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
        if (t - t0 > 3000u) break;
    }
    write32(CHCR0, 0);
    return r;
}

static uint16_t calls[64], lost[64], bus[64];
static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset9\n");
    printf("mdec-reset's one(), N 0, by the phase of the refresh it starts from. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);

    for (int i = 0; i < 4096; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        const uint32_t phase = read16(T2_COUNT) & 0xFF;
        const Row r = one(zeroWait, zeroFlag, zeroFlag);
        calls[phase / 4]++;
        if (!r.done) lost[phase / 4]++;
        if (r.status == 0x60000000u) bus[phase / 4]++;
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("phase  calls lost bus\n");
    for (int i = 0; i < 64; i++) printf("%3d  %5d %4d %4d\n", i * 4, (int)calls[i], (int)lost[i], (int)bus[i]);
    printf("\nmdec/mdec-reset9: Done.\n");
    for (;;) {}   /* park after Done. */
}
