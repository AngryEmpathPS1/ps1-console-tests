/*
 * cdrom/cd-race - a command written just before a sector: which interrupt
 * comes first, the command's INT3 or the sector's INT1, and where does it flip?
 *
 * WHY (STATUS M192, "ESPN Extreme Games"; M231, cd-queue case G). The drive's
 * sub-CPU runs one main loop that both picks up commands and delivers sectors,
 * and psx-spx says which goes first depends "on which portion of the mainloop
 * it is currently executing". DuckStation delivers the command first,
 * mednafen the sector (M231). This core holds a sector's INT1 behind ANY
 * pending command's first response (the Kojin Kyouju rule, M192): its only
 * measurement is Kojin Kyouju's 333 cycles, where the sector comes after.
 * ESPN's sector landed about 23,000 cycles before its pending command ran, and
 * whether that one jumps the command decides whether the game stalls for 480
 * frames. nocash's case G is one point of the same race and reads the sector
 * first. This maps it: a threshold is what the model needs, and one point
 * cannot give one.
 *
 * WHAT IT DOES. ReadN at 2x (then 1x) with no data taken, and the INT1 period
 * P measured from 16 sectors. Then per trial: take an INT1 at t1, wait until
 * t1 + P - D, write GetStat, and report which interrupt comes first and when
 * each arrives after the write. D runs from -20,000 (the command after the
 * sector's INT1 time) to +150,000 cycles (well before it) in steps of 5,000,
 * three times each.
 *
 * THE CLOCK. Timer2 at sysclock/8, unwrapped in software, as cd-pause.
 *
 * READ IT LIKE THIS. A row is "D: order  first  second" - order 31 means the
 * command's INT3 came first, 13 the sector's INT1. For D below 0 the INT1 is
 * already raised when the command is written, so 13 is the control. Where
 * 31 starts, and whether the INT3 time grows when the sector wins, is the
 * mainloop's shape.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js cd-race --disc
 * --frames=30000`): the INT1 period reads 226,303 at 2x and 450,204 at 1x
 * (the CD's seeded sector jitter, M174/M188, moves each INT1 by tens of
 * thousands of cycles, so the edge is fuzzy). 13 at D <= -15,000; mixed from
 * -10,000 to +15,000; 31 on every trial from D = 20,000 to 150,000 at both
 * speeds. The INT3 comes 24,000-84,000 cycles after the write. Where the
 * sector's own time falls inside that (D ~ 25,000-80,000) its INT1 WAITS for
 * the command: at 2x D = 50,000 it arrives 49,624-85,760 after the write,
 * behind an INT3 at 29,616-64,760 - the M192 hold. Mednafen's sector-first
 * rule would read 13 there; a console that does has found ESPN's threshold.
 *
 * THE HELD SECTOR, ON THE CONSOLES (CD rebuild, piece 5): where the INT3 comes first with the sector already waiting,
 * the INT1 follows it by 15,864-16,504 cycles on all three (the SCPH-5501 28 trials at double speed and 26 at single,
 * the 7501 and 9001 62 and 54), in two clusters a 464-cycle step apart - the drive's program announcing it in the pass
 * that took the command, which this test's at-once acknowledge catches. v0.377.7 read 20,900 / 33,700 / 50,900
 * (p10 / median / p90 at double speed), v0.377.8 16,200 / 16,200 / 50,900; the consoles 15,900 / 16,400 / 68,300 (the
 * tail is the sector's own time when the command came well before it).
 * CONSOLE DAY (2026-10-05): every wait now gives up after ~4 s of other interrupts (tooLong). The SCPH-5501 came
 * to this program from cd-cmds with its drive still reading (see cd-cmds, S2), and waitInt(2) for Init's INT2 took
 * an INT1 every 13 ms for 13 minutes with interrupts off; after a reset it ran through. Rebuilt, the D < 0 control
 * rows read the first interrupt 80 cycles after the write where they read 96: the polling loop's own latency,
 * moved with the code (cd-cmds then cd-race back to back on the drive's own program, both builds).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }

/* The next interrupt of any kind, with the time it was first seen. Gives up
 * after ~2 s (returns 0). */
static uint8_t nextInt(uint32_t* at) {
    uint32_t t0 = now();
    for (;;) {
        uint32_t t = now();
        uint8_t f = irqFlag();
        if (f) { *at = t; return f; }
        if (t - t0 > 70000000) { *at = t; return 0; }
    }
}
/* Every wait for one KIND of interrupt gives up after ~4 s of other kinds too (console day, 2026-10-05): the SCPH-5501
 * came to this program from cd-cmds with its drive still reading, and waitInt(2) for Init's INT2 took an INT1 every
 * 13 ms for 13 minutes, interrupts off, the console deaf to the cable. */
static bool tooLong(uint32_t t0) { return now() - t0 > 140000000; }
static uint8_t waitInt(uint8_t want) {
    const uint32_t t0 = now();
    for (;;) {
        uint32_t t;
        uint8_t f = nextInt(&t);
        if (f == want || f == 5 || f == 0) return f;
        drain(); ackAll();
        if (tooLong(t0)) return 0;
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setmode(uint8_t m) { param(m); simple(0x0E); }

/* An INT1, acknowledged; its time. */
static uint32_t takeInt1() {
    uint32_t t;
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = nextInt(&t);
        drain(); ackAll();
        if (f == 1 || f == 0 || tooLong(t0)) return t;
    }
}

static void run(int speed) {
    setmode(speed ? 0x80 : 0x00);
    const uint32_t a = 1000 + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);                                   /* Setloc */
    command(0x06); waitInt(3); drain(); ackAll();   /* ReadN */
    for (int i = 0; i < 4; i++) takeInt1();
    uint32_t first = takeInt1(), last = first;
    for (int i = 0; i < 16; i++) last = takeInt1();
    const uint32_t P = (last - first) / 16;
    printf("%s: INT1 period %d cycles\n", speed ? "2x" : "1x", P);
    for (int32_t D = -20000; D <= 150000; D += 5000) {
        printf("%s D=%7d :", speed ? "2x" : "1x", D);
        for (int rep = 0; rep < 3; rep++) {
            const uint32_t t1 = takeInt1();
            while ((int32_t)(now() - (t1 + P - D)) < 0) { }
            const uint32_t tw = now();
            command(0x01);                           /* GetStat */
            uint32_t ta, tb;
            const uint8_t fa = nextInt(&ta);
            drain(); ackAll();
            const uint8_t want = fa == 3 ? 1 : 3;
            uint8_t fb = 0;
            for (;;) {                               /* the other one; skip a second INT1 */
                fb = nextInt(&tb);
                drain(); ackAll();
                if (fb == want || fb == 0 || fb == 5) break;
                if (tooLong(tw)) { fb = 0; break; }
            }
            printf("  %d%d %7d %7d", fa, fb, ta - tw, tb - tw);
        }
        printf("\n");
    }
    simple(0x09); waitInt(2); drain(); ackAll();     /* Pause */
}

int main() {
    printf("\ncdrom/cd-race\n");
    printf("GetStat written D cycles before the next sector's INT1: which comes first?\n");
    printf("Each entry: order (31 = INT3 first, 13 = INT1 first), then each one's cycles after the write.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    run(1);
    run(0);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
