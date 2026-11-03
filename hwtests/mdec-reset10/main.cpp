/*
 * mdec/mdec-reset10 - mdec-reset9's refresh sweep for a COLD call, and for the cold call followed by a warm one, the pair
 * mdec-reset's first row makes (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset's N 0 is lost in every runner log (30 of 30) and kept in the three from before the runner (Unirom); on the
 * SCPH-5501, 2026-11-03 (console-logs/2026-11-03-SCPH-5501-reset-sweep), the unmodified binary loses it at locked starts of
 * video 24000, 24256, 25000 and 34000 and keeps it at 24064, 24128 and 24192: offsets 0, 16 and 232 of the 256-cycle refresh
 * lose, 64, 128 and 192 keep. So it is the refresh. mdec-reset9 swept the refresh for a WARM call (4,096 calls, 0 lost); the
 * first row's two calls are the program's first, the first cold. This sweeps the refresh for a cold call, and for the pair.
 *
 * WHAT IT DOES. mdec-reset9's one() (N 0: the reset, the enables, the status, the command, a 32-word table DMA), interrupts
 * off. COLD: 2,048 times, the instruction cache evicted first (4 KB of nops), the phase of Timer 2 (16 bits, a whole number
 * of refreshes) read just before the call; calls, lost calls (DMA0 not done in 3,000 cycles) and calls whose status read
 * 60000000h (the bus: the read inside the reset's deaf cycles) by phase in bins of 8. PAIR: 2,048 times, evicted, the cold
 * call, then the same call warm; the warm call's lost and bus counts binned by the cold call's phase.
 *
 * READ IT LIKE THIS. Lost calls in some bins and none in others is the refresh meeting a cold call there. No assertions.
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

extern "C" void mr10Evict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mr10Evict\nmr10Evict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

static uint16_t cCalls[32], cLost[32], cBus[32], pLost[32], pBus[32], pCalls[32];
static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset10\n");
    printf("mdec-reset's one(), N 0, cold and cold-then-warm, by the refresh phase of the cold call. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    for (int i = 0; i < 2048; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        mr10Evict();
        const uint32_t phase = read16(T2_COUNT) & 0xFF;
        const Row r = one(zeroWait, zeroFlag, zeroFlag);
        cCalls[phase / 8]++;
        if (!r.done) cLost[phase / 8]++;
        if (r.status == 0x60000000u) cBus[phase / 8]++;
    }
    for (int i = 0; i < 2048; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        mr10Evict();
        const uint32_t phase = read16(T2_COUNT) & 0xFF;
        one(zeroWait, zeroFlag, zeroFlag);
        const Row r = one(zeroWait, zeroFlag, zeroFlag);
        pCalls[phase / 8]++;
        if (!r.done) pLost[phase / 8]++;
        if (r.status == 0x60000000u) pBus[phase / 8]++;
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("phase   cold: calls lost bus   pair (the warm call): calls lost bus\n");
    for (int i = 0; i < 32; i++) printf("%3d       %5d %4d %4d          %5d %4d %4d\n", i * 8, (int)cCalls[i], (int)cLost[i], (int)cBus[i], (int)pCalls[i], (int)pLost[i], (int)pBus[i]);
    printf("\nmdec/mdec-reset10: Done.\n");
    for (;;) {}   /* park after Done. */
}
