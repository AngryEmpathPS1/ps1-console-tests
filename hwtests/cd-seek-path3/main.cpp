/*
 * cdrom/cd-seek-path3 - the head's path through a LONGER medium seek: where a 2N jump of N 33-64 lands (2026-10-21)
 *
 * WHY. v0.377.156 (STATUS): a 2N-track jump counted on MIRR falls short of 2N for N 16-31 (hwtests/cd-seek-path2 on
 * four consoles, measured to N 25); the fit carried on to the program's N 40h broke nugget's long seeks, so it stops
 * at 31. cd-game-steps' medium seeks (Omiai Commando, 07:xx, ~3,750 sectors: chained 2N jumps of N 64) read ~1.4M
 * late backward and ~1.4M early forward on the 7501's program against the 7501 - the shape a shortfall at large N
 * would give. This measures N 33-64.
 *
 * WHAT IT DOES. cd-seek-path's method unchanged (interrupts off, the flags polled psx-spx's way, every wait bounded,
 * Timer 2 at sysclock/8 unwrapped; per cell a control SeekL timed, then the same from the same start with GetlocP every
 * quarter sector, each change of sector kept). The cells: speed, origin, distance, wait in half sectors after the
 * origin's GetlocP - from LBA 19 +640 .. +1280 by 80 (N 33-64 on the 7501's program) at waits 0 and 8; from 33,000
 * (07:22) +1,500, +3,000, -1,500, -3,750; from 29:01 (130,462) +1,500 and -1,500; at double speed from 19 +640, +960,
 * +1280.
 *
 * READ IT LIKE cd-seek-path: per cell the control's and the polled seek's times (M cycles), then time(M):sector-less-
 * target where GetlocP's sector changed, | at the INT2. The first sector after the long gap is where the 2N jump
 * landed. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-path3 --disc=xfiles --frames=60000 --quiet=60000`.
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

#define NC 27
#define NP 120
static const int32_t CELL[NC][4] = { { 0, 19, 640, 0 }, { 0, 19, 720, 0 }, { 0, 19, 800, 0 }, { 0, 19, 880, 0 }, { 0, 19, 960, 0 }, { 0, 19, 1040, 0 }, { 0, 19, 1120, 0 }, { 0, 19, 1200, 0 }, { 0, 19, 1280, 0 }, { 0, 19, 640, 8 }, { 0, 19, 720, 8 }, { 0, 19, 800, 8 }, { 0, 19, 880, 8 }, { 0, 19, 960, 8 }, { 0, 19, 1040, 8 }, { 0, 19, 1120, 8 }, { 0, 19, 1200, 8 }, { 0, 19, 1280, 8 }, { 0, 33000, 1500, 0 }, { 0, 33000, 3000, 0 }, { 0, 33000, -1500, 0 }, { 0, 33000, -3750, 0 }, { 0, 130462, 1500, 0 }, { 0, 130462, -1500, 0 }, { 1, 19, 640, 0 }, { 1, 19, 960, 0 }, { 1, 19, 1280, 0 } };
static uint32_t pathT[NP]; static int32_t pathS[NP]; static int np; static uint32_t polledT, ctlT;

/* One GetlocP's answer: 3 and the sector, 5 (an error) and -1; 2 if the seek's INT2 came first (taken, *t2 set). */
static int32_t answer(int *kind, uint32_t t0, uint32_t *t2) {
    const uint32_t tw = now();
    for (;;) {
        if (now() - tw > LIMIT) { timeouts++; *kind = 0; return -1; }
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f != 2 && f != 3 && f != 5) { f = irqFlag() & 7; if (f != 2 && f != 3 && f != 5) { drain(); ackAll(); continue; } }
        if (f == 2) { if (!*t2) *t2 = now() - t0; drain(); ackAll(); continue; }
        if (f == 5) { drain(); ackAll(); *kind = 5; return -1; }
        uint8_t r[8]; int n = 0;
        while (hasResp() && n < 8) r[n++] = resp();
        drain(); ackAll();
        *kind = 3;
        if (n < 8) return -1;
        return (int32_t)((unbcd(r[5]) * 60 + unbcd(r[6])) * 75 + unbcd(r[7])) - 150;
    }
}
/* SeekL to lba with GetlocP every `gap` cycles from its INT3 to its INT2; the path kept where the sector changes. */
static uint32_t seekPath(uint32_t lba, uint32_t gap) {
    setloc(lba);
    const uint32_t t0 = now();
    command(0x15);
    if (waitInt(3) != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    uint32_t t2 = 0; int32_t last = -99999; np = 0;
    while (!t2 && now() - t0 < LIMIT) {
        const uint32_t tw = now();
        while (now() - tw < gap) {
            uint8_t f = irqFlag() & 7;
            if (!f) continue;
            if (f != 2 && f != 5) { f = irqFlag() & 7; }
            if (f == 2 || f == 5) { t2 = now() - t0; drain(); ackAll(); break; }
            drain(); ackAll();
        }
        if (t2) break;
        command(0x11);
        int kind = 0; const int32_t sec = answer(&kind, t0, &t2);
        if (!kind) break;
        if (sec != last && np < NP) { pathT[np] = now() - t0; pathS[np] = sec; np++; last = sec; }
    }
    return t2;
}

int main() {
    printf("\ncdrom/cd-seek-path3\n");
    printf("The head's path through a LONGER medium seek (a 2N jump of N 33-64): per cell (speed, origin, distance, wait in half sectors) the control SeekL's time\n");
    printf("and the polled one's (command write -> INT2, M cycles), then time(M):sector-less-target where GetlocP's sector\n");
    printf("changed (| marks the INT2). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    int speedNow = -1;
    for (int c = 0; c < NC; c++) {
        const int speed = CELL[c][0]; const uint32_t origin = (uint32_t)CELL[c][1];
        const uint32_t target = (uint32_t)((int32_t)origin + CELL[c][2]); const int w = CELL[c][3];
        if (speed != speedNow) { setmode(speed ? 0x80 : 0x00); speedNow = speed; }
        const uint32_t half = speed ? 112896u : 225792u;   /* half a sector at that speed */
        seekTo(origin); getlocP();
        { const uint32_t t0 = now(); while (now() - t0 < half * (uint32_t)w) {} }
        ctlT = seekTo(target);
        seekTo(origin); getlocP();
        { const uint32_t t0 = now(); while (now() - t0 < half * (uint32_t)w) {} }
        polledT = seekPath(target, half / 2);
        printf("%s from %6d %c%2d w%2d : %u.%02u %u.%02u :", speed ? "2x" : "1x", (int)origin, CELL[c][2] > 0 ? '+' : '-', (int)(CELL[c][2] > 0 ? CELL[c][2] : -CELL[c][2]), w,
               (unsigned)(ctlT / 1000000u), (unsigned)((ctlT % 1000000u) / 10000u), (unsigned)(polledT / 1000000u), (unsigned)((polledT % 1000000u) / 10000u));
        int marked = 0;
        for (int i = 0; i < np; i++) {
            if (!marked && polledT && pathT[i] > polledT) { printf(" |"); marked = 1; }
            if (pathS[i] < 0) printf(" %u.%02u:E", (unsigned)(pathT[i] / 1000000u), (unsigned)((pathT[i] % 1000000u) / 10000u));
            else printf(" %u.%02u:%d", (unsigned)(pathT[i] / 1000000u), (unsigned)((pathT[i] % 1000000u) / 10000u), (int)(pathS[i] - (int32_t)target));
        }
        if (!marked) printf(" |");
        printf("\n");
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-seek-path3: Done.\n");
    for (;;) VSync(0);
}
