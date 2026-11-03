/*
 * mdec/mdec-reset15 - where mdec-reset's one() spends its cycles, segment by segment, for a warm call after a rest and for a warm
 * call right behind another (2026-11-03, for the SCPH-5501).
 *
 * WHY. mdec-reset14 on the SCPH-5501 (two runs byte-identical) beside v0.377.260 on the runner's lock: the first warm call of a
 * pair takes 415-425 cycles on the console and 402-411 here, ~13 short; the call right behind it 316-335 there and 315-328 here;
 * a cold call 557-563 there, 550-556 here. one() is what every movie player does to the MDEC (the reset, the enables, the
 * status, a table command, a 32-word DMA0 and its poll). This times its parts.
 *
 * WHAT IT DOES. one2(), mdec-reset's one() with Timer 2 read at four points: on entry (t0), after the status read (t1: CHCR0 = 0,
 * the reset, the enables, the read), after DMA0's start (t2: the command, MADR0, BCR0, CHCR0) and when the poll sees DMA0 done
 * (t3). Interrupts off, cached code; 512 tries of a pair (one2() after 300+ cycles of rest, one2() again at once), from varied
 * refresh phases. Per call of the pair: the fewest and most cycles of each segment (t1-t0, t2-t1, t3-t2) and of the whole call.
 *
 * READ IT LIKE THIS. Set each segment beside an emulator's: where they differ is where its timing is wrong. No assertions.
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
struct Seg { uint16_t a, b, c, all; };
/* one()'s own branches between the reset and the enables (twice, cmdFirst, wait - all off), so the enables land 9 cycles behind
   the reset as in one(); a first build without them put the enables 6 behind, inside the reset's deaf cycles, on both machines. */
static volatile uint32_t zeroWait = 0;
static volatile bool zeroFlag = false;
static __attribute__((noinline, noclone)) Seg one2(uint32_t wait, bool cmdFirst, bool twice) {
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR0, 0);
    write32(MDEC1, 0x80000000u);
    if (twice) write32(MDEC1, 0x60000000u);
    if (cmdFirst) write32(MDEC0, 0x40000001u);
    if (wait) waitCycles(wait);
    write32(MDEC1, 0x60000000u);
    volatile uint32_t status = read32(MDEC1); (void)status;
    const uint16_t t1 = read16(T2_COUNT);
    if (!cmdFirst) write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)table & 0x1FFFFFu);
    write32(BCR0, (1u << 16) | 32u);
    write32(CHCR0, 0x01000201u);
    const uint16_t t2 = read16(T2_COUNT);
    uint16_t t3 = t2;
    for (int i = 0; i < 1000; i++) { t3 = read16(T2_COUNT); if (!(read32(CHCR0) & 0x01000000u)) break; }
    write32(CHCR0, 0);
    Seg s; s.a = (uint16_t)(t1 - t0); s.b = (uint16_t)(t2 - t1); s.c = (uint16_t)(t3 - t2); s.all = (uint16_t)(t3 - t0);
    return s;
}

static uint16_t lo[2][4], hi[2][4];
static void note(int k, const Seg &s) {
    const uint16_t v[4] = { s.a, s.b, s.c, s.all };
    for (int j = 0; j < 4; j++) { if (!lo[k][j] || v[j] < lo[k][j]) lo[k][j] = v[j]; if (v[j] > hi[k][j]) hi[k][j] = v[j]; }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-reset15\n");
    printf("mdec-reset's one() timed in segments: a call after a rest, and the call right behind it. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x8u);
    for (int i = 0; i < 32; i++) table[i] = 0x01010101u * (uint32_t)(i + 1);
    one2(zeroWait, zeroFlag, zeroFlag); one2(zeroWait, zeroFlag, zeroFlag);   /* cache it */
    for (int i = 0; i < 512; i++) {
        waitCycles(300 + (uint32_t)(i * 37 % 256));
        const Seg s1 = one2(zeroWait, zeroFlag, zeroFlag);
        const Seg s2 = one2(zeroWait, zeroFlag, zeroFlag);
        note(0, s1); note(1, s2);
    }
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("call          reset..status   status..DMA start   DMA start..done   whole\n");
    for (int k = 0; k < 2; k++)
        printf("%s   %5d-%-5d     %5d-%-5d         %5d-%-5d       %5d-%-5d\n", k ? "behind it   " : "after a rest",
               (int)lo[k][0], (int)hi[k][0], (int)lo[k][1], (int)hi[k][1], (int)lo[k][2], (int)hi[k][2], (int)lo[k][3], (int)hi[k][3]);
    printf("\nmdec/mdec-reset15: Done.\n");
    for (;;) {}   /* park after Done. */
}
