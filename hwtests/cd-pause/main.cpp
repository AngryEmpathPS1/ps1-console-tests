/*
 * cdrom/cd-pause - how long does a Pause's second response (INT2) take, and
 * does it depend on WHERE on the disc the head is and WHEN in the sector the
 * Pause arrives?
 *
 * WHY (STATUS, "Gundam Battle Assault 2 (Europe)"). That game's loader races
 * the Pause's INT2 against its once-per-frame state machine and plays only if
 * the pause takes more than ~1,075,000 cycles at double speed. The two console
 * measurements disagree: ps1-tests cdrom/timing's console read 1,028,832 to
 * 1,046,336 (pausing near LBA 117, straight after an INT1), psx-spx's PAL PSone
 * 1,066,874 to 1,159,938 ("time equal to about 5 sectors"). Both are the span
 * from the first response to the second - psx-spx's "when paused" row, 1DF2h,
 * is exactly that span in this core. DuckStation models the pause as the head
 * jumping back one track and waiting for the target to come round: (sectors a
 * turn - 2) x one sector - time since the last sector, so it would depend on
 * the RADIUS and the PHASE. This core uses one constant. This test asks.
 *
 * THE DRIVE'S OWN PROGRAM (tools/cddrive.js runs the SCPH-9001's, 2026-09-30)
 * has a rule of its own. Pause seeks to a target and answers when the head is
 * 2 frames short of it, jumping a track back from anywhere closer. Right after
 * a read's FIRST INT1 the target is still the Setloc position, 3 frames behind
 * the head: a turn less ~4.4 sectors. After that each frame sets it to the
 * subcode + 1: a turn less ~1.4 (a frame more from phase 1/2, where the next
 * frame lands first). So the "first INT1" rows and the phase=0/4 rows should
 * read 3.0 sectors apart ON ANY DISC, and both grow with the radius. A turn is
 * 2 x pi x r / v, and no disc says its v (1.2-1.4 m/s): nocash's PSone fits
 * 1.28 m/s, the SCPH-9001's burned CD-R 1.22 (tools/cddrivetest.js grades
 * both). `node tools/cdpausepredict.js <disc> --from-first=<the 1x lba=16
 * first INT1 avg>` prints every row the program predicts for that disc.
 *
 * WHAT IT DOES. Reads the disc's length (GetTD 0), then for each speed, each of
 * three positions (near the start, the middle, 80%) and each phase (the Pause
 * written 0, 1/4, 1/2, 3/4 of a sector after an INT1), four times: Setloc +
 * ReadN, take 12 INT1s, wait the phase, write Pause, wait for its INT3 and
 * acknowledge it, and time the wait for INT2. Before the phases at each
 * position, four with the Pause right after the FIRST INT1, as nocash's and
 * nugget's consoles were timed (added 2026-09-30). On a disc with music tracks
 * the three positions are taken inside its data track (GetTD(2)).
 *
 * THE CLOCK. Timer2 at sysclock/8, read continuously and unwrapped in software.
 * ps1-tests' measure() adds 0FFFFh once however many times the counter wrapped
 * and so cannot time a million-cycle pause (the M18 trap).
 *
 * THE RULE: run it here first. This core charges a constant after the INT3, so
 * every row must read the same (~1,041,000 double, ~2,065,000 single, plus the
 * loop's own granularity).
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js cd-pause --disc`):
 *   2x: 0.87-1.08M cycles by phase at every LBA tried (16, 167,062, 267,300); 1x: 1.85-2.21M.
 * AT v0.280 (2026-09-30, `node hwtests/run.js cd-pause --disc --frames=6000`: a console with
 *   the program's longer pauses needs more than the default budget), the first-INT1 rows added: 2x 1.04-1.08M, 1x
 *   2.15-2.21M at every LBA; the phases 0.89-1.10M and 1.82-2.21M.
 * THE DRIVE'S PROGRAM on Final Fantasy VII Disc 1 at 1.3 m/s (cdpausepredict):
 *   1x first INT1 2.10M / 5.37M / 6.77M at LBA 16 / 158,962 / 254,340, phase=0/4
 *   3.46M / 6.74M / 8.14M; 2x 1.07M / 2.72M / 3.41M and 1.74M / 3.39M / 4.07M.
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
static uint8_t irqFlag() { idx(1); return *CD3 & 0x1F; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
static uint8_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

/* The unwrapped clock: Timer2 counts sysclock/8; call tick() often. */
static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }

/* Wait for a given INT, acknowledging others; returns it (5 = error). */
static uint8_t waitInt(uint8_t want) {
    const uint32_t c0 = clk8;                                   /* rule 1: 0 after 10 s, never a deaf console */
    for (;;) {
        tick();
        uint8_t f = irqFlag() & 7;
        if (!f) { if (clk8 - c0 > 42336000u) return 0; continue; }
        if (f == want || f == 5) return f;
        /* An old board's flag can read 1 before 3 (psx-spx "Unstable IRQ Flag polling"; the SCPH-1001's cd-irq-glitch: once
         * in 3,000 Getstats): read it again before taking it for another interrupt, or the INT3 is acknowledged here and
         * waited for for ever - cd-seek-map hung the 1001 twice this way (2026-10-09). The normal path is unchanged. */
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setmode(uint8_t m) { param(m); simple(0x0E); }

static uint32_t discSectors() {
    param(0); command(0x14); waitInt(3);
    resp(); uint8_t mm = unbcd(resp()), ss = unbcd(resp()); drain(); ackAll();
    return (mm * 60 + ss) * 75;
}

/* One measurement: Setloc + ReadN, `int1s` INT1s taken, the phase waited, Pause.
 * Returns INT3-ack -> INT2 in cycles; *ack gets Pause -> INT3. */
static uint32_t onePause(uint32_t lba, uint32_t phaseCycles, uint32_t* ack, int int1s) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);                                  /* Setloc */
    command(0x06); waitInt(3); drain(); ackAll();   /* ReadN */
    for (int i = 0; i < int1s; i++) { waitInt(1); drain(); ackAll(); }
    uint32_t t0 = now();
    while (now() - t0 < phaseCycles) { }
    uint32_t tp = now();
    command(0x09);                                  /* Pause */
    uint8_t r = waitInt(3); drain(); ackAll();
    uint32_t tack = now();
    *ack = tack - tp;
    if (r == 5) return 0;
    r = waitInt(2); drain(); ackAll();
    return now() - tack;
}

int main() {
    printf("\ncdrom/cd-pause\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    /* The drive is set up by hand, not through libcd: Init (INT3, then INT2 once
     * the spindle settles) and Demute. libcd's CdInit never returned on this
     * core's runner; nothing here depends on a library's state. */
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
    simple(0x0C);

    const uint32_t total = discSectors();
    /* A disc with music tracks after its data is timed inside the data track: a
     * ReadN onto audio fails, and every row there would read a refused Pause
     * (Moto Racer World Tour's data ends at 9:33 of 62:56). */
    command(0x13); waitInt(3); resp(); resp();      /* GetTN */
    const uint8_t lastTrack = unbcd(resp()); drain(); ackAll();
    uint32_t end = total;
    if (lastTrack >= 2) {
        param(bcd(2)); command(0x14); waitInt(3);   /* GetTD(2): where the music starts */
        resp(); uint8_t mm = unbcd(resp()), ss = unbcd(resp()); drain(); ackAll();
        end = (mm * 60 + ss) * 75;
    }
    printf("disc: %d sectors (%d:%02d)", total, total / 4500, (total / 75) % 60);
    if (end != total) printf(", its data to %d:%02d", end / 4500, (end / 75) % 60);
    printf("\n");
    const uint32_t pos[3] = { 16, end / 2, end * 8 / 10 };
    for (int speed = 1; speed >= 0; speed--) {
        setmode(speed ? 0x80 : 0x00);
        const uint32_t sector = speed ? 225792 : 451584;
        for (int p = 0; p < 3; p++) {
            /* Right after the read's FIRST INT1, as nocash's and nugget's
             * consoles were timed. */
            {
                uint32_t mn = 0xFFFFFFFF, mx = 0, sum = 0, n = 0;
                printf("%s lba=%6d first INT1:", speed ? "2x" : "1x", pos[p]);
                for (int run = 0; run < 4; run++) {
                    uint32_t ack = 0;
                    uint32_t v = onePause(pos[p], 0, &ack, 1);
                    printf(" %d", v);
                    if (v) { mn = v < mn ? v : mn; mx = v > mx ? v : mx; sum += v; n++; }
                }
                if (n) printf("   min %d avg %d max %d\n", mn, sum / n, mx);
                else printf("   (Pause refused every time)\n");
            }
            for (int ph = 0; ph < 4; ph++) {
                uint32_t mn = 0xFFFFFFFF, mx = 0, sum = 0, n = 0;
                printf("%s lba=%6d phase=%d/4 :", speed ? "2x" : "1x", pos[p], ph);
                for (int run = 0; run < 4; run++) {
                    uint32_t ack = 0;
                    uint32_t v = onePause(pos[p], sector * ph / 4, &ack, 12);
                    printf(" %d", v);
                    if (v) { mn = v < mn ? v : mn; mx = v > mx ? v : mx; sum += v; n++; }
                }
                if (n) printf("   min %d avg %d max %d\n", mn, sum / n, mx);
                else printf("   (Pause refused every time)\n");
            }
        }
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nOne constant everywhere: this core's model. Rising with position (more\n");
    printf("sectors a turn) and falling with phase: DuckStation's back-a-track model.\n");
    printf("The drive's own program: first INT1 a turn less ~4.4 sectors, phase=0/4\n");
    printf("a turn less ~1.4 - 3.0 sectors apart on any disc.\n");
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
