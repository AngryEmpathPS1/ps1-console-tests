/*
 * cdrom/cd-irq-glitch - does a polled CD interrupt flag show a half-set value?
 *
 * WHY (2026-10-08, for the SCPH-1001). psx-spx, "Caution - Unstable IRQ Flag
 * polling" (reference/docs/psx-spx.txt 5476-5498): "IRQ flag changes aren't
 * synced with the MIPS CPU clock. If more than one bit gets set (and the CPU is
 * reading at the same time) then the CPU does occassionally see only one of the
 * newly bits": 0 -> 1 -> 3 about once in thousands of INT3s, 0 -> 4 -> 5 about
 * once in hundreds of INT5s - "The problem occurs only on older consoles (like
 * LATE-PU-8), not on newer consoles (like PSone)". Which boards count as older
 * is not said: the SCPH-5501 is a PU-18, the 7501 and 9001 one-chip boards, and
 * the 1001 a PU-7 or PU-8. This core sets the flags whole (the model and the
 * drive programs' CXD1815Q register alike), so a console that shows the half
 * values is a model trait to give that console's row (CONSOLE_MODELS).
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled in a tight loop with the
 * index register already at 1, so the loop is one byte read, a mask and a
 * branch. For each command: the first nonzero value read, and the read straight
 * after it. NG Getstats (INT3: bits 0 and 1) and NG invalid commands 30h (INT5:
 * bits 0 and 2 - psx-spx "1Fh..4Fh Unused/invalid INT5(11h,40h)"), each a
 * pseudo-random 0-1 ms after the last was acknowledged. Any disc, or none.
 *
 * READ IT LIKE THIS. A row "first 1, then 3" is psx-spx's glitch for INT3,
 * "first 4, then 5" for INT5; everything else should be "first 3, then 3" and
 * "first 5, then 5". Counts, not times: with ~3,000 of each, psx-spx's rates
 * would show a handful of INT3 glitches and some tens of INT5 ones.
 *
 * ON THIS CORE (2026-10-08, v0.377.46, `node hwtests/run.js cd-irq-glitch`):
 * every Getstat 3 then 3, every invalid command 5 then 5 - the flags are set
 * whole.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define NG 3000

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }
static uint32_t rng = 12345u;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

static uint32_t hist[8][8];
static int timeouts;

/* One command: the first nonzero flag value and the read right after it. */
static void one(uint8_t c) {
    ackAll(); drain();
    idle(rnd(33869));
    command(c);
    idx(1);
    uint8_t f = 0;
    for (uint32_t k = 0; k < 2000000 && !(f = *CD3 & 7); k++) { }
    const uint8_t g = *CD3 & 7;
    if (!f) { timeouts++; return; }
    hist[f][g]++;
    /* a second response (none for these two) or a late bit: wait for it to settle, then clear */
    idle(2000);
    drain(); ackAll();
}

static void report(const char *what) {
    printf("%s:\n", what);
    for (int f = 1; f < 8; f++) for (int g = 0; g < 8; g++)
        if (hist[f][g]) printf("  first %d, then %d: %u\n", f, g, (unsigned)hist[f][g]);
    if (timeouts) printf("  no interrupt: %d\n", timeouts);
    for (int f = 0; f < 8; f++) for (int g = 0; g < 8; g++) hist[f][g] = 0;
    timeouts = 0;
}

int main() {
    printf("\ncdrom/cd-irq-glitch\n");
    printf("The first nonzero CD interrupt flag a tight poll reads, and the read after it, %d Getstats and %d invalid commands.\n", NG, NG);
    printf("About ten seconds; nothing prints until each part is done.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    for (int i = 0; i < NG; i++) one(0x01);
    report("Getstat (INT3)");
    for (int i = 0; i < NG; i++) one(0x30);
    report("invalid command 30h (INT5)");
    write16(T2_MODE, m2);
    idx(1); *CD2 = 0x1F;
    ackAll(); drain();
    ExitCriticalSection();
    printf("\ncdrom/cd-irq-glitch: Done.\n");
    for (;;) VSync(0);
}
