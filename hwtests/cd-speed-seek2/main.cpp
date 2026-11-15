/*
 * cdrom/cd-speed-seek2 - the head's path through a seek written right after a speed change (2026-11-15). Any pressed data disc
 * of 12 minutes or more (The X-Files, Doom (USA)).
 *
 * WHY. hwtests/cd-speed-seek on the second SCPH-5501 x3 (The X-Files): a ReadN 21,004 sectors back, written 0.3 ms after a
 * Setmode to double speed, reads its first sector 22.09M cycles on at the median and the same read with no change 9.12M - the
 * change costs 12.97M - where this emulator's drive program (SC430930 on cdservo.js) reads 24.05M and 9.50M: 14.55M, 1.6M
 * more, the same shift as Doom's first level read (hwtests/cd-doom-load). On this servo the program polls GFS for 13.6M
 * before it moves the sled; the console's own spindle (hwtests/cd-spindown) is back in about that. So either the console's sled
 * moves before the frames are back, or its seek after them is shorter than a plain one - and hwtests/cd-speed's short seek at
 * the disc's start goes the other way (the console 20.6-25.1M at D 50, this core 19.2M). GetlocP answers mid-seek from the
 * drive's latest Q (hwtests/cd-seek-long), so polled it draws the path: when the Q first moves off the origin, where the sled
 * lands, every jump after it.
 *
 * WHAT IT DOES. cd-seek-long's helpers (interrupts off, the flags polled, every wait bounded, Timer 2 at sysclock/8 unwrapped).
 * Per cell, five times: Setmode M0, SeekL to the origin, GetlocP, 1.2 s in the hold; then [Setmode M1 and] at once SeekL to the
 * target with GetlocP every quarter sector (of the new speed) from its INT3 to its INT2, kept where the sector jumps by more
 * than two, with the first answer and the last; a run of errors is kept once. The cells:
 *   C1  11:12:00 single -> double, to 06:31:71    Doom's first level read's seek
 *   N1  11:12:00 double,           to 06:31:71    the same, no change
 *   C2  00:02:16 single -> double, to 00:06:16    cd-speed's short seek
 *   N2  00:02:16 double,           to 00:06:16
 *   C3  00:50:33 double -> single, to 11:12:00    Doom's seek to the menu's music
 *   N3  00:50:33 single,           to 11:12:00
 *
 * READ IT LIKE THIS. A row per seek: the cell, the SeekL's write to its INT2 in M cycles, then time(M):sector-less-target at
 * each kept answer (E an error; | marks the INT2). The first answer far from the origin's side is the sled's landing; the time
 * of the last answer still at the origin is how long the drive stayed there. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-speed-seek2 --disc=xfiles --ntsc --frames=14000 --quiet=12000 --settle=600`.
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

#define NC 6
#define NP 160
#define NREP 5
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
        if ((last == -99999 || (sec < 0 && !(np && pathS[np - 1] < 0)) || (sec >= 0 && (sec - last > 2 || last - sec > 2))) && np < NP) { pathT[np] = now() - t0; pathS[np] = sec; np++; }   /* a run of errors once */
        if (sec >= 0) last = sec;
        endT = now() - t0; endS = sec;
    }
    if (np < NP) { pathT[np] = endT; pathS[np] = endS; np++; }   // where the head was at the last answer
    return t2;
}

struct Cell { const char *name; uint8_t m0; int flip; uint8_t m1; uint32_t o, t; };
static const Cell CELL[NC] = {
    { "C1", 0x00, 1, 0x80, 50250, 29246 }, { "N1", 0x80, 0, 0x80, 50250, 29246 },
    { "C2", 0x00, 1, 0x80, 16, 316 },      { "N2", 0x80, 0, 0x80, 16, 316 },
    { "C3", 0x80, 1, 0x00, 3633, 50250 },  { "N3", 0x00, 0, 0x00, 3633, 50250 },
};
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

int main() {
    printf("\ncdrom/cd-speed-seek2\n");
    printf("The head's path through a seek written right after a speed change: per seek the cell, the SeekL's write to its INT2\n");
    printf("(M cycles), then time(M):sector-less-target where GetlocP's sector changed (| marks the INT2). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    for (int c = 0; c < NC; c++) {
        const Cell &ce = CELL[c];
        for (int rep = 0; rep < NREP; rep++) {
            setmode(ce.m0);
            idle(2032128u);                                   /* 60 ms: Sony's three frames before a head move */
            seekTo(ce.o); const int32_t at = getlocP();
            idle(40642560u);                                  /* 1.2 s in the hold */
            if (ce.flip) setmode(ce.m1);
            const uint8_t m = ce.flip ? ce.m1 : ce.m0;
            polledT = seekPath(ce.t, (m & 0x80) ? 56448u : 112896u);
            printf("%s rep %d (hold at %d): %u.%02u :", ce.name, rep + 1, (int)(at - (int32_t)ce.o), (unsigned)(polledT / 1000000u), (unsigned)((polledT % 1000000u) / 10000u));
            int marked = 0;
            for (int i = 0; i < np; i++) {
                if (!marked && polledT && pathT[i] > polledT) { printf(" |"); marked = 1; }
                if (pathS[i] < 0) printf(" %u.%02u:E", (unsigned)(pathT[i] / 1000000u), (unsigned)((pathT[i] % 1000000u) / 10000u));
                else printf(" %u.%02u:%d", (unsigned)(pathT[i] / 1000000u), (unsigned)((pathT[i] % 1000000u) / 10000u), (int)(pathS[i] - (int32_t)ce.t));
            }
            if (!marked) printf(" |");
            printf("\n");
        }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-speed-seek2: Done.\n");
    for (;;) VSync(0);
}
