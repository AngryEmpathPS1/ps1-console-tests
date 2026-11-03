/*
 * mdec/mdec-reset14 - mdec-reset11's two losing pairs (cold then warm, warm then warm) with each call's cycles, so the console's
 * timing can be set beside an emulator's (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset11 on the SCPH-5501 (two runs, byte-identical) loses the second call of a back-to-back pair at cold-then-warm
 * phase 240-241 and warm-then-warm 124-125 and 136-137; v0.377.260 on the runner's lock (the virtual console) loses the same
 * kind at other phases (cold-then-warm 48-49, and with 200 cycles between at 56-57) and never warm-then-warm. The mechanism is
 * shared - a refresh in the call's write queue holding the reset 3 cycles and more, the enables 9 behind it lost (mdec-reset13);
 * where it lands is the calls' timing. This prints the timing.
 *
 * WHAT IT DOES. mdec-reset11's P (evict, one() cold, one() warm at once) and C (one() warm, one() warm at once), 2,048 tries
 * each, interrupts off, the phase of Timer 2 (16 bits, whole refreshes) read just before the first call, in bins of 2. Per bin:
 * calls, the second call lost (DMA0 not done in 3,000 cycles), and the fewest and most cycles of each call (Timer 2 read
 * before the first, between the two, and after the second).
 *
 * READ IT LIKE THIS. Per bin, P then C: calls, lost, call 1 cycles lo-hi, call 2 cycles lo-hi. No assertions.
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

extern "C" void mr14Evict(void);
__asm__(".text\n.set push\n.set noreorder\n.balign 4096\n.global mr14Evict\nmr14Evict:\n.rept 1024\nnop\n.endr\njr $ra\nnop\n.set pop\n");

static uint8_t calls[2][128], lost[2][128];
static uint16_t d1lo[2][128], d1hi[2][128], d2lo[2][128], d2hi[2][128];
static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset14\n");
    printf("mdec-reset11's cold-then-warm (P) and warm-then-warm (C) pairs with each call's cycles. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    for (int v = 0; v < 2; v++)
        for (int i = 0; i < 2048; i++) {
            waitCycles(300 + (uint32_t)(i * 37 % 256) + (uint32_t)((i >> 8) & 7));
            if (v == 0) mr14Evict();
            const uint16_t ta = read16(T2_COUNT);
            const uint32_t phase = ta & 0xFF;
            one(zeroWait, zeroFlag, zeroFlag);
            const uint16_t tb = read16(T2_COUNT);
            const Row r = one(zeroWait, zeroFlag, zeroFlag);
            const uint16_t tc = read16(T2_COUNT);
            const uint16_t d1 = (uint16_t)(tb - ta), d2 = (uint16_t)(tc - tb);
            const int b = (int)(phase / 2);
            if (calls[v][b] < 255) calls[v][b]++;
            if (!r.done && lost[v][b] < 255) lost[v][b]++;
            if (!d1lo[v][b] || d1 < d1lo[v][b]) d1lo[v][b] = d1;
            if (d1 > d1hi[v][b]) d1hi[v][b] = d1;
            if (!d2lo[v][b] || d2 < d2lo[v][b]) d2lo[v][b] = d2;
            if (d2 > d2hi[v][b]) d2hi[v][b] = d2;
        }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("phase   P calls lost  call1 lo-hi  call2 lo-hi     C calls lost  call1 lo-hi  call2 lo-hi\n");
    for (int b = 0; b < 128; b++) {
        if (!calls[0][b] && !calls[1][b]) continue;
        printf("%3d ", b * 2);
        for (int v = 0; v < 2; v++)
            printf("     %5d %4d  %5d-%-5d %5d-%-5d", (int)calls[v][b], (int)lost[v][b], (int)d1lo[v][b], (int)d1hi[v][b], (int)d2lo[v][b], (int)d2hi[v][b]);
        printf("\n");
    }
    for (int v = 0; v < 2; v++) {
        int c = 0, l = 0;
        for (int b = 0; b < 128; b++) { c += calls[v][b]; l += lost[v][b]; }
        printf("total %c: calls %d lost %d\n", "PC"[v], c, l);
    }
    printf("\nmdec/mdec-reset14: Done.\n");
    for (;;) {}   /* park after Done. */
}
