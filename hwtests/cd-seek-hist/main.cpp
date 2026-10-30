/*
 * cdrom/cd-seek-hist - does a seek's time depend on how the head got to where it starts? (2026-10-18, the seek piece)
 *
 * WHY. cd-seek-map on five consoles (STATUS "the seek map on five boards", 2026-10-17): 184 of 304 rows agree on every
 * board, and the planner refit (scratch/seekfit.js) lands 164 of them; every remaining miss ends with a jump back from
 * just past the target and a read up, and the same last two steps go both ways by row - -88 a turn more on every board,
 * +40 / -152 / +152 none. In cd-seek-map each row's origin is reached from the row before's target (and its second run
 * from its own first target), so the history before the timed seek is never the same twice and never controlled: if
 * the servo carries something from the jump before (the lens's offset on the sled, a direction), that is where it
 * would show, and the planner cannot model it from those rows.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded in time (CONSOLE RULE 1), Timer 2
 * at sysclock/8 unwrapped in software, as cd-seek-map. At each speed, from LBA 1,000 and from 130,462 (29:01), for each
 * distance -152, -88, -24, +10, +20, +40, +88, +152 and each of five HISTORIES - the origin reached by a long jump forward
 * (from 3,000 sectors before; 16 from LBA 1,000), a long jump back (from 3,000 after), a short jump forward (from 40
 * before), a short jump back (from 40 after), or a second SeekL to the origin itself - twice: SeekL to the history's
 * place, SeekL to the origin (timed), GetlocP, SeekL to the target (timed from the command write to its INT2), GetlocP.
 * Printed per row: per run the origin seek's time, the target seek's time and the two GetlocP sectors.
 *
 * READ IT LIKE THIS. Down a distance's five rows, a target time that moves by about a turn (4.3M at 1x near the start,
 * 6.5M at 29:01; half at 2x) with the history is history the planner must carry; five rows alike (within the run-to-run
 * spread of the two runs) say the misses come from something else. T/O marks a wait that ran out (2 seconds).
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-hist --disc --frames=40000 --quiet=6000`.
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

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }

static const uint32_t LIMIT = 67737600u;   /* 2 seconds */
static int timeouts;
/* The interrupt wanted (or 5), 0 if none came within LIMIT. A first read that is neither is read again before it is
 * taken for another interrupt (psx-spx "Unstable IRQ Flag polling"; CONSOLE RULE 1). */
static uint8_t waitInt(uint8_t want) {
    const uint32_t t0 = now();
    for (;;) {
        if (now() - t0 > LIMIT) { timeouts++; return 0; }
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f == want || f == 5) return f;
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setmode(uint8_t m) { param(m); simple(0x0E); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}
/* SeekL to lba and wait for it to finish; command write -> INT2 in cycles, 0 on an error or a wait that ran out. */
static uint32_t seekTo(uint32_t lba) {
    setloc(lba);
    const uint32_t t0 = now();
    command(0x15);
    const uint8_t a = waitInt(3);
    if (a != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    const uint8_t r = waitInt(2); const uint32_t t = now() - t0; drain(); ackAll();
    return r == 2 ? t : 0;
}
/* GetlocP's absolute sector, or -1. */
static int32_t getlocP() {
    command(0x11);
    if (waitInt(3) != 3) { drain(); ackAll(); return -1; }
    uint8_t b[8]; int n = 0;
    while (hasResp() && n < 8) b[n++] = resp();
    drain(); ackAll();
    if (n < 8) return -1;
    return (int32_t)((unbcd(b[5]) * 60 + unbcd(b[6])) * 75 + unbcd(b[7])) - 150;
}

#define ND 8
#define NH 5
static const int32_t DIST[ND] = { -152, -88, -24, 10, 20, 40, 88, 152 };
static const char* HNAME[NH] = { "far-fwd", "far-back", "near-fwd", "near-back", "again" };
static uint32_t tO[ND][NH][2], tT[ND][NH][2];
static int32_t pO[ND][NH][2], pT[ND][NH][2];

static uint32_t histPlace(uint32_t origin, int h) {
    switch (h) {
        case 0: return origin > 3016 ? origin - 3000 : 16;
        case 1: return origin + 3000;
        case 2: return origin - 40;
        case 3: return origin + 40;
        default: return origin;
    }
}
static void tval(uint32_t t) { if (t) printf(" %9u", (unsigned)t); else printf("       T/O"); }

int main() {
    printf("\ncdrom/cd-seek-hist\n");
    printf("A seek's time against how the head got to its origin: per row (speed, origin, distance, history) and per run\n");
    printf("the origin SeekL's time, the target SeekL's time (command write -> INT2, cycles at 33,868,800 a second) and GetlocP's\n");
    printf("sector after each. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    static const uint32_t origins[2] = { 1000, 130462 };
    for (int speed = 0; speed < 2; speed++) {
        setmode(speed ? 0x80 : 0x00);
        for (int o = 0; o < 2; o++) {
            for (int d = 0; d < ND; d++)
                for (int h = 0; h < NH; h++)
                    for (int run = 0; run < 2; run++) {
                        seekTo(histPlace(origins[o], h));
                        tO[d][h][run] = seekTo(origins[o]);
                        pO[d][h][run] = getlocP();
                        tT[d][h][run] = seekTo((uint32_t)((int32_t)origins[o] + DIST[d]));
                        pT[d][h][run] = getlocP();
                    }
            for (int d = 0; d < ND; d++)
                for (int h = 0; h < NH; h++) {
                    printf("%s from %6d %c%3d %-9s:", speed ? "2x" : "1x", (int)origins[o], DIST[d] > 0 ? '+' : '-',
                           (int)(DIST[d] > 0 ? DIST[d] : -DIST[d]), HNAME[h]);
                    for (int run = 0; run < 2; run++) {
                        tval(tO[d][h][run]); tval(tT[d][h][run]);
                        printf("  %6d %6d %s", (int)pO[d][h][run], (int)pT[d][h][run], run ? "" : "|");
                    }
                    printf("\n");
                }
        }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-seek-hist: Done.\n");
    for (;;) VSync(0);
}
