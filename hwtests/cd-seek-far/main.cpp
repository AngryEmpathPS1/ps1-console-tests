/*
 * cdrom/cd-seek-far - cd-seek-long's cells taken to near the disc's end (the Fable CD session, 2026-11-25)
 *
 * WHY. src/cdservo.js SLED_DRIVE_TPS, KNOWN-OPEN: nugget's cdlSeekP2to71 (an SCPH-9001, SeekP 00:02 -> 71:00 on a disc ending
 * 71:28: at least 1 s) reads 0.91 s on this servo - its sled 450 ms, its landing 14,330 sectors short, a 150 ms brake, five long
 * jumps in 250 ms - and the sled was fitted on seeks no farther than 50:00 (cd-seek-long, cd-sled, cd-seek-path; the 9001's
 * then on The X-Files, which ends at 57:59). Our 9001 now has Final Fantasy VII (USA) Disc 1 in it (lead-out 70:39).
 *
 * WHAT IT DOES. cd-seek-long's framework (interrupts off, the CD polled psx-spx's way, every wait bounded, Timer 2 at sysclock/8
 * unwrapped); its cells from GetTD's lead-out: FAR is 40 s short of it. Per cell (speed, from, to, SeekL or SeekP): SeekL to
 * the origin, GetlocP, the seek to the target timed to its INT2 (the CONTROL; SeekP where marked, as nugget's); SeekL to the
 * origin again, then the same as a SeekL with GetlocP every quarter sector from its INT3 for two seconds, kept where the sector
 * jumps. Cells at 1x then 2x: 00:02 -> FAR (SeekL), FAR -> 00:02, 00:02 -> FAR (SeekP), 00:02 -> 60:00, 60:00 -> 00:02,
 * 60:00 -> FAR (60:00 only where the disc reaches past it). Twice. Needs a disc of 51 minutes or more.
 *
 * READ IT LIKE THIS. As cd-seek-long: per cell the control's time and the polled seek's (M cycles), then time(M):sector-less-
 * target where GetlocP's sector changed - the first far from the target is the sled's landing. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-far --disc=<a long disc> --frames=20000 --quiet=10000`.
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

#define NC 12
#define NP 160
/* speed, origin, target, SeekP (1) or SeekL (0); FAR and SIXTY filled from GetTD in main */
#define FAR (-1)
#define SIXTY (-2)
static int32_t CELL[NC][4] = { { 0, 0, FAR, 0 }, { 0, FAR, 0, 0 }, { 0, 0, FAR, 1 }, { 0, 0, SIXTY, 0 }, { 0, SIXTY, 0, 0 }, { 0, SIXTY, FAR, 0 },
                               { 1, 0, FAR, 0 }, { 1, FAR, 0, 0 }, { 1, 0, FAR, 1 }, { 1, 0, SIXTY, 0 }, { 1, SIXTY, 0, 0 }, { 1, SIXTY, FAR, 0 } };
/* SeekP to lba and wait for it to finish (as seekTo) */
static uint32_t seekToP(uint32_t lba) {
    setloc(lba);
    const uint32_t t0 = now();
    command(0x16);
    const uint8_t a = waitInt(3);
    if (a != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    const uint8_t r = waitInt(2); const uint32_t t = now() - t0; drain(); ackAll();
    return r == 2 ? t : 0;
}
static uint32_t pathT[NP]; static int32_t pathS[NP]; static int np; static uint32_t polledT, ctlT;
static uint32_t endT; static int32_t endS;   /* the last GetlocP answer of a polled seek */

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
        // only the jumps (a step of more than two sectors), errors, and the first answer: the head's steady advance between
        // them would fill the path before the sled has landed
        if ((last == -99999 || sec < 0 || sec - last > 2 || last - sec > 2) && np < NP) { pathT[np] = now() - t0; pathS[np] = sec; np++; }
        if (sec >= 0) last = sec;
        endT = now() - t0; endS = sec;
    }
    if (np < NP) { pathT[np] = endT; pathS[np] = endS; np++; }   // where the head was at the last answer
    return t2;
}

#ifndef CDSEEKFAR_NO_MAIN   /* hwtests/cd-sled-edges includes this file for its framework */
int main() {
    printf("\ncdrom/cd-seek-far\n");
    printf("The head's path through a seek to near the disc's end: per cell (speed, from, to, SeekL or SeekP) the control seek's time and\n");
    printf("the polled SeekL's (command write -> INT2, M cycles), then time(M):sector-less-target where GetlocP's sector changed (| marks\n");
    printf("the INT2). FAR is 40 s short of GetTD's lead-out. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    /* GetTD(0): the lead-out, MM SS */
    param(0); command(0x14);
    int32_t leadout = 0;
    if (waitInt(3) == 3) { uint8_t r[4]; int n = 0; while (hasResp() && n < 4) r[n++] = resp(); if (n >= 3) leadout = (unbcd(r[1]) * 60 + unbcd(r[2])) * 75 - 150; }
    drain(); ackAll();
    const int32_t far = leadout - 40 * 75, sixty = 60 * 60 * 75 - 150;
    printf("lead-out %d (GetTD), FAR %d\n", leadout, far);
    if (far < 50 * 60 * 75) { printf("disc too short for FAR (needs 51 minutes)\n"); restoreTimer(2, prev); ExitCriticalSection(); printf("\ncdrom/cd-seek-far: Done.\n"); for (;;) VSync(0); }
    for (int c = 0; c < NC; c++) for (int k = 1; k <= 2; k++) { if (CELL[c][k] == FAR) CELL[c][k] = far; else if (CELL[c][k] == SIXTY) CELL[c][k] = sixty < far ? sixty : -3; }
    int speedNow = -1;
    for (int rep = 0; rep < 2; rep++) {
        for (int c = 0; c < NC; c++) {
            if (CELL[c][1] < 0 || CELL[c][2] < 0) continue;   /* 60:00 past FAR: not this disc */
            const int speed = CELL[c][0]; const uint32_t origin = (uint32_t)CELL[c][1], target = (uint32_t)CELL[c][2];
            const int useP = CELL[c][3];
            if (speed != speedNow) { setmode(speed ? 0x80 : 0x00); speedNow = speed; }
            const uint32_t half = speed ? 112896u : 225792u;   /* half a sector at that speed */
            seekTo(origin); getlocP();
            ctlT = useP ? seekToP(target) : seekTo(target);
            seekTo(origin); getlocP();
            polledT = seekPath(target, half / 2);
            const uint32_t a = origin + 150, b = target + 150;
            printf("%s%s %02d:%02d -> %02d:%02d : %u.%02u %u.%02u :", speed ? "2x" : "1x", useP ? "P" : "L", (int)(a / 4500), (int)((a / 75) % 60), (int)(b / 4500), (int)((b / 75) % 60),
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
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-seek-far: Done.\n");
    for (;;) VSync(0);
}
#endif
