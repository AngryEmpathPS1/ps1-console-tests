/*
 * cdrom/cd-spinramp - the disc's speed over the 3.5 s after a speed change, from the sectors' own clock (the Fable CD session,
 * 2026-11-23). WHY: hwtests/cd-pause-settle on the second SCPH-5501 (console-logs/2026-11-23-pausesettle-README.txt): right after
 * Setmode 80h a Pause after a read's first INT1 takes +5-6% on the model, then +2%, then as the model 3 s later - the spindle is
 * still coming up to double speed, a turn taking longer. The model's servo (src/cdservo.js) sets the disc to the CLV speed the
 * moment its PLL locks (omega = target), so it has no such tail. This measures the tail's shape, to fit the approach law.
 * WHAT IT DOES: cd-pause's helpers (Init, Demute; flags polled, every wait bounded; Timer 2 at sysclock/8 unwrapped). Five rows,
 * each from a head parked at 00:02:16 (LBA 16) and the drive settled 5 s at the row's first speed, the Pause done:
 *   UP1, UP2  Setmode 80h at T0, at once ReadN 00:02:16; every INT1 timed from T0 for 3.5 s.
 *   DOWN      the same from double speed with Setmode 00h.
 *   CTL2      no speed change, at double speed (the sector clock once settled).
 *   CTL1      no speed change, at single speed.
 * Each INT1 acknowledged at once, its data not read (as cd-pause). Kept in RAM, printed with the drive paused.
 * READ IT LIKE THIS: per row the first INT1's time from T0 (ms), then per 100 ms bin of time from T0 "t:n:mean" - the bin's start
 * in ms, how many intervals ended in it, and their mean in CPU cycles (a sector is 225,792 at 2x, 451,584 at 1x: 225,792 / mean
 * is the disc's speed as a share of 2x). The interval's own jitter is ~3-5% (cd-int1-spread), so a bin of ~15 averages it out.
 * No unlock. SAFE FOR THE DRIVE: reads end before 00:06:00. About 45 s.
 */
#define CDPAUSE_NO_MAIN
#include "../cd-pause/main.cpp"

static uint32_t tI[700]; static int nI;
static void waitc(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }
static void park(uint8_t mode) {
    setmode(mode);
    param(0x00); param(0x02); param(0x16); simple(0x02);                /* Setloc 00:02:16 */
    command(0x15); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* SeekL */
    waitc(5u * 33868800u);                                             /* 5 s settled at this speed */
}
static void row(const char* label, uint8_t from, int change, uint8_t to) {
    park(from);
    const uint32_t S = 33868800;
    uint32_t T0 = now();
    if (change) { T0 = now(); setmode(to); }
    param(0x00); param(0x02); param(0x16); simple(0x02);                /* Setloc 00:02:16 */
    command(0x06); if (waitInt(3) != 3) { drain(); ackAll(); printf("%s ReadN refused\n", label); return; }
    drain(); ackAll();
    nI = 0;
    while (now() - T0 < 7 * S / 2 && nI < 700) {
        tick();
        const uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f == 1) { tI[nI++] = now() - T0; drain(); ackAll(); continue; }
        { const uint8_t g = irqFlag() & 7; if (g == 1) { tI[nI++] = now() - T0; drain(); ackAll(); continue; } }
        drain(); ackAll();
    }
    command(0x09); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Pause */
    printf("%s n %d first %d ms:", label, nI, nI ? (int)(tI[0] / 33869) : -1);
    const uint32_t BIN = S / 10;
    int i = 1;
    for (uint32_t b = 0; b < 35; b++) {
        uint32_t sum = 0, n = 0;
        while (i < nI && tI[i] < (b + 1) * BIN) { if (tI[i] >= b * BIN) { sum += tI[i] - tI[i - 1]; n++; } i++; }
        if (n) printf(" %d:%d:%d", (int)(b * 100), (int)n, (int)(sum / n));
    }
    printf("\n");
}

int main() {
    printf("\ncdrom/cd-spinramp\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                  /* Demute */
    if (discSectors() == 0) { printf("disc: none (GetTD gave 0) - nothing to time\n"); restoreTimer(2, prev); ExitCriticalSection(); printf("\nDone.\n"); for (;;) {} }
    row("UP1 ", 0x00, 1, 0x80);
    row("UP2 ", 0x00, 1, 0x80);
    row("DOWN", 0x80, 1, 0x00);
    row("CTL2", 0x80, 0, 0x80);
    row("CTL1", 0x00, 0, 0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
