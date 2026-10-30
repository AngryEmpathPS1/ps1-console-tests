/*
 * cdrom/cd-seek-long - the head's path through a LONG seek: where the sled lands, and what follows (2026-10-19)
 *
 * WHY. cd-sled on the SCPH-7001: long seeks out from the hub run 0.66-0.80 of this emulator's planned time and the 7001
 * is symmetric (2x 00:02 <-> 50:00: 26.3M out, 26.6M in) where the planner is not (38.7M out, 35.3M in). The planner's
 * path (src/psx.js planSeek, logged): the sled stops 128 tracks short by design and lands ~290 tracks short at 50:00,
 * then 2N jumps of 128, 141, 44, 10 tracks and singles - five decisions after the sled, ~15M of a 43M seek at 1x. The
 * console's fastest 1x 00:02 -> 50:00 (29.0M) is about this emulator's sled alone. GetlocP answers mid-seek from the
 * drive's latest Q (cd-seek-path, v0.377.129), so polled it draws the console's long path: the sled's landing, and every
 * jump after it.
 *
 * WHAT IT DOES. As cd-seek-path (whose framework this is): interrupts off, the CD polled psx-spx's way, every wait bounded,
 * Timer 2 at sysclock/8 unwrapped. Per cell (speed, from, to): SeekL to the origin, GetlocP, then the SeekL to the target
 * timed to its INT2 (the CONTROL, no polling); SeekL to the origin again, then the same SeekL with GetlocP every quarter
 * sector from its INT3 for two seconds, kept where the sector JUMPS (more than two sectors), with the first answer and the
 * last. A command taken mid-seek silences the
 * seek's INT2 on the console (v0.377.128), so the polled seek's time is the control's; its path is what it is for.
 * Cells at single then double speed: 00:02 -> 30:00, 00:02 -> 50:00, 50:00 -> 00:02, 30:00 -> 00:02, 10:00 -> 29:01.
 *
 * READ IT LIKE THIS. Per cell: the control's time and the polled seek's (M cycles; the polled one usually none), then
 * time(M):sector-less-target where GetlocP's sector changed - the first far from the target is the sled's landing. No
 * assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-long --disc=xfiles --frames=20000 --quiet=10000`.
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

#define NC 10
#define NP 160
static const int32_t CELL[NC][3] = { { 0, 0, 134850 }, { 0, 0, 224850 }, { 0, 224850, 0 }, { 0, 134850, 0 }, { 0, 44850, 130462 },
                                     { 1, 0, 134850 }, { 1, 0, 224850 }, { 1, 224850, 0 }, { 1, 134850, 0 }, { 1, 44850, 130462 } };
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

int main() {
    printf("\ncdrom/cd-seek-long\n");
    printf("The head's path through a long seek: per cell (speed, from, to) the control SeekL's time and the polled one's\n");
    printf("(command write -> INT2, M cycles), then time(M):sector-less-target where GetlocP's sector changed (| marks the\n");
    printf("INT2). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    int speedNow = -1;
    for (int rep = 0; rep < 2; rep++) {
        for (int c = 0; c < NC; c++) {
            const int speed = CELL[c][0]; const uint32_t origin = (uint32_t)CELL[c][1], target = (uint32_t)CELL[c][2];
            if (speed != speedNow) { setmode(speed ? 0x80 : 0x00); speedNow = speed; }
            const uint32_t half = speed ? 112896u : 225792u;   /* half a sector at that speed */
            seekTo(origin); getlocP();
            ctlT = seekTo(target);
            seekTo(origin); getlocP();
            polledT = seekPath(target, half / 2);
            const uint32_t a = origin + 150, b = target + 150;
            printf("%s %02d:%02d -> %02d:%02d : %u.%02u %u.%02u :", speed ? "2x" : "1x", (int)(a / 4500), (int)((a / 75) % 60), (int)(b / 4500), (int)((b / 75) % 60),
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
    printf("\ncdrom/cd-seek-long: Done.\n");
    for (;;) VSync(0);
}
