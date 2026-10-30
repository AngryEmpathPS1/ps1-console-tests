/*
 * cdrom/cd-seek-phase3 - cd-seek-phase at two more radii: where a seek decides to read forward or go round, against the head's place (2026-10-18, the seek piece)
 *
 * WHY. cd-seek-hist on the SCPH-7001: a seek plans from where the head really is - one sector further on, the same target
 * reads forward a sector less or goes round a turn less or more (from LBA 1,000: +10 8.72M with the head one short of the
 * origin, 3.96M with it on), and cd-seek-map's rows split by run on the head's place inside its sector. This core's planner
 * puts that boundary a sector off (from 1,000 +9 goes round here, reads forward on the console). Where exactly the console's
 * decision falls, as a function of the head's position, is what the planner's lead / arrive / decision constants are; this
 * maps it the way gpu-shape-time maps a GPU hold: the same seek started at every place across the head's idle circle.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (2 s; CONSOLE RULE 1), Timer 2 at
 * sysclock/8 unwrapped in software. At each speed, from 20:00 and 40:00 (LBA 89,850 and 179,850: radii no fit has seen - v0.377.132 CD_LANDING was raced on cd-seek-phase and phase2), for distances -14, -10, -6, -2, +6,
 * +8, +9, +10, +11, +12, +14, +18 and 20 waits W = 0, 0.5 ... 9.5 sectors (at that speed): SeekL to the origin, GetlocP,
 * wait W from the GetlocP's answer, SeekL to the target timed (command write -> INT2), GetlocP.
 *
 * READ IT LIKE THIS. Per row (speed, origin, distance) the 20 waits' times in M cycles and, under them, GetlocP's sector at
 * the origin and after the target seek (less the origin, less the target): as W walks the head round its idle circle a
 * distance's time steps between "read forward" and "a turn more" - the step's place is the decision's boundary.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-phase3 --disc --frames=60000 --quiet=6000`.
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

#define ND 12
#define NW 20
static const int32_t DIST[ND] = { -14, -10, -6, -2, 6, 8, 9, 10, 11, 12, 14, 18 };
static uint32_t tT[ND][NW]; static int32_t pO[ND][NW], pT[ND][NW];

int main() {
    printf("\ncdrom/cd-seek-phase3\n");
    printf("A seek's time against the head's place: per row (speed, origin, distance) the target SeekL's time (command write ->\n");
    printf("INT2, M cycles at 33,868,800 a second) after waits of 0, 0.5 ... 9.5 sectors from the origin's GetlocP; then the\n");
    printf("origin's and the target's GetlocP sectors (less the origin / the target). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    static const uint32_t origins[2] = { 89850, 179850 };
    for (int speed = 0; speed < 2; speed++) {
        setmode(speed ? 0x80 : 0x00);
        const uint32_t half = speed ? 112896u : 225792u;   /* half a sector: 33,868,800 / 75 (or 150) / 2 */
        for (int o = 0; o < 2; o++) {
            for (int d = 0; d < ND; d++)
                for (int w = 0; w < NW; w++) {
                    seekTo(origins[o]);
                    pO[d][w] = getlocP();
                    { const uint32_t t0 = now(); while (now() - t0 < half * (uint32_t)w) {} }
                    tT[d][w] = seekTo((uint32_t)((int32_t)origins[o] + DIST[d]));
                    pT[d][w] = getlocP();
                }
            for (int d = 0; d < ND; d++) {
                printf("%s from %6d %c%2d :", speed ? "2x" : "1x", (int)origins[o], DIST[d] > 0 ? '+' : '-', (int)(DIST[d] > 0 ? DIST[d] : -DIST[d]));
                for (int w = 0; w < NW; w++) { if (tT[d][w]) printf(" %u.%02u", (unsigned)(tT[d][w] / 1000000u), (unsigned)((tT[d][w] % 1000000u) / 10000u)); else printf(" T/O"); }
                printf("\n%22s", "heads");
                for (int w = 0; w < NW; w++) printf(" %d/%d", (int)(pO[d][w] - (int32_t)origins[o]), (int)(pT[d][w] - (int32_t)origins[o] - DIST[d]));
                printf("\n");
            }
        }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-seek-phase3: Done.\n");
    for (;;) VSync(0);
}
