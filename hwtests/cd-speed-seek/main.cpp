/*
 * cdrom/cd-speed-seek - a long seek right after a speed change, started at 17 places across the hold's turn (2026-11-15).
 * Any pressed data disc of 12 minutes or more (The X-Files, Doom (USA)): every head move is a SeekP or a ReadN inside 00:50-11:12.
 *
 * WHY. Doom (USA)'s level load begins with the two moves this drive model is longest on (hwtests/cd-doom-load, the second
 * SCPH-5501 x6 against its drive program SC430930 on the disc's own spiral, STATUS 2026-11-15): the menu's SeekP to the music
 * (Setmode 07h at double speed, then 00:50:13 -> 11:12:00: 28.7M cycles at the median where this core reads 30.8M) and the
 * level's first ReadN (the music paused at single speed, Setmode 80h, then 11:12:00 -> 06:31:71: 21.4-21.9M in 20 repeats of
 * 30 and 24.0-24.4M in 8, where this core reads 23.5M or 26.1M). Every other long seek of the load is the console's to 0.2M.
 * hwtests/cd-speed puts the change alone, at the disc's start, at or under the console's time on this core - so what is long
 * is the seek AFTER the change. The game writes each at one place in the drive's hold; this walks the start across the hold's
 * turn, with and without the speed change, so the floor (when the head arrives) and the phase (when the target comes round)
 * separate, and the change's share is the difference from the control.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, every wait bounded, Timer 2 at the system clock unwrapped in software
 * (cd-doom-phase's helpers). Per cell, per step k = 0..16, twice: Setmode M0; 60 ms; Setloc O, SeekP, its INT2; the drive holds
 * there 1.2 s + k x STEP; then, as the game does it, [Setmode M1, 3,800 cycles after its INT3] Pause, its INT2; and
 *   a READ cell: Setloc T 4,900 after that INT2, Setloc T again 4,200 after its INT3, ReadN 2,700 after that: the ReadN's write
 *     to its first INT1; Pause 20,000 after it;
 *   a SEEK cell: Setloc T 4,100 after that INT2, SeekP 2,700 after its INT3: the SeekP's write to its INT2.
 * The cells (O, M0 [-> M1], T; STEP is a seventeenth of the hold's turn at O and M0):
 *   RC  11:12:00 single -> double, ReadN 06:31:71   Doom's first level read (21,004 sectors back)      STEP 345,000
 *   R0  11:12:00 double,           ReadN 06:31:71   the same with no change                            STEP 172,000
 *   SC  00:50:33 double -> single, SeekP 11:12:00   Doom's seek to the menu's music (46,617 on)        STEP 130,000
 *   S0  00:50:33 single,           SeekP 11:12:00   the same with no change                            STEP 260,000
 * About nine minutes; nothing prints until the end. A step that timed out or met an INT5 prints 0; past 24 of them it stops
 * (the second SCPH-5501 loses the disc after about one speed change in 20: STATUS 2026-11-09).
 *
 * READ IT LIKE THIS. A row per cell and pass: the seventeen times in k cycles, k = 0 first. Within a row the times step by
 * whole turns at the target; the smallest is the head's arrival, and RC less R0 (SC less S0) is what the change costs a seek.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-speed-seek --disc=xfiles --ntsc --frames=40000 --quiet=38000 --settle=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)
#define NCELL 4
#define NSTEP 17
#define NPASS 2

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
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* The next interrupt: which and when; drained and acknowledged. A first read of 1 or 4 is read again (an old board's flag can
 * show 1 before 3, psx-spx "Unstable IRQ Flag polling"); every wait is bounded. */
struct Got { uint8_t flag; uint32_t t; };
static Got take(uint32_t limit) {
    Got g; g.flag = 0; g.t = 0;
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    drain(); ackAll();
    return g;
}
static Got takeKind(uint8_t k, uint32_t limit) {
    for (;;) { const Got g = take(limit); if (!g.flag || g.flag == k || g.flag == 5) return g; }
}
static void idleUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) {} }
static uint32_t setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    const uint32_t t = now(); command(0x02); return t;
}

struct Cell { const char *name; uint32_t o; uint8_t m0; int flip; uint8_t m1; int seek; uint32_t t; uint32_t step; };
static const Cell CELL[NCELL] = {
    { "RC", 50250, 0x00, 1, 0x80, 0, 29246, 345000 },
    { "R0", 50250, 0x80, 0, 0x80, 0, 29246, 172000 },
    { "SC", 3633, 0x80, 1, 0x00, 1, 50250, 130000 },
    { "S0", 3633, 0x00, 0, 0x00, 1, 50250, 260000 },
};
static uint32_t T[NCELL][NPASS][NSTEP], pMin[NCELL], pMax[NCELL];
static int bad;

int main() {
    printf("\ncdrom/cd-speed-seek\n");
    printf("A long seek right after a speed change, started at 17 places across the hold's turn. Needs a pressed data disc.\n");
    printf("About nine minutes; nothing prints until the end.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    ackAll(); command(0x0A); take(MS(3000)); takeKind(2, MS(8000));       /* Init */
    idleUntil(now() + MS(1000));

    for (int c = 0; c < NCELL; c++) { pMin[c] = 0xFFFFFFFFu; pMax[c] = 0; }
    for (int c = 0; c < NCELL && bad <= 24; c++) for (int pass = 0; pass < NPASS; pass++) for (int k = 0; k < NSTEP && bad <= 24; k++) {
        const Cell &ce = CELL[c];
        bool ok = true;
        ackAll(); param(ce.m0); command(0x0E); Got g = takeKind(3, MS(3000));
        idleUntil(now() + MS(60));
        setloc(ce.o); g = takeKind(3, MS(3000));
        idleUntil(g.t + 2700); ackAll(); command(0x16); takeKind(3, MS(3000)); g = takeKind(2, MS(10000));
        if (g.flag != 2) ok = false;
        idleUntil(g.t + MS(1200) + (uint32_t)k * ce.step);
        if (ce.flip) { ackAll(); param(ce.m1); command(0x0E); g = takeKind(3, MS(3000)); idleUntil(g.t + 3800); }
        ackAll(); uint32_t tw = now(); command(0x09); takeKind(3, MS(3000)); g = takeKind(2, MS(8000));
        if (g.flag == 2) { const uint32_t d = g.t - tw; if (d < pMin[c]) pMin[c] = d; if (d > pMax[c]) pMax[c] = d; } else ok = false;
        if (!ce.seek) {
            idleUntil(g.t + 4900); setloc(ce.t); g = takeKind(3, MS(3000));
            idleUntil(g.t + 4200); setloc(ce.t); g = takeKind(3, MS(3000));
            idleUntil(g.t + 2700); ackAll(); tw = now(); command(0x06); takeKind(3, MS(3000));
            g = takeKind(1, MS(10000));
            T[c][pass][k] = (ok && g.flag == 1) ? g.t - tw : 0;
            if (!(ok && g.flag == 1)) bad++;
            idleUntil(g.t + 20000);
            ackAll(); command(0x09); takeKind(3, MS(3000)); takeKind(2, MS(8000));
        } else {
            idleUntil(g.t + 4100); setloc(ce.t); g = takeKind(3, MS(3000));
            idleUntil(g.t + 2700); ackAll(); tw = now(); command(0x16); takeKind(3, MS(3000));
            g = takeKind(2, MS(10000));
            T[c][pass][k] = (ok && g.flag == 2) ? g.t - tw : 0;
            if (!(ok && g.flag == 2)) bad++;
        }
        idleUntil(now() + 200000);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("Per cell and pass: the ReadN's write to its first INT1 (RC, R0) or the SeekP's write to its INT2 (SC, S0), k cycles,\n");
    printf("the hold before it 1.2 s + 0, 1, ... 16 steps:\n");
    for (int c = 0; c < NCELL; c++) for (int pass = 0; pass < NPASS; pass++) {
        printf("%s %5u -> %5u step %6u:", CELL[c].name, (unsigned)CELL[c].o, (unsigned)CELL[c].t, (unsigned)CELL[c].step);
        for (int k = 0; k < NSTEP; k++) printf(" %5u", (unsigned)((T[c][pass][k] + 500) / 1000));
        printf("\n");
    }
    printf("The Pause before each, its write to its INT2, min-max k cycles:");
    for (int c = 0; c < NCELL; c++) printf(" %s %u-%u", CELL[c].name, (unsigned)((pMin[c] + 500) / 1000), (unsigned)((pMax[c] + 500) / 1000));
    printf("\nsteps that failed (a timeout or an INT5): %d\n", bad);
    printf("\ncdrom/cd-speed-seek: Done.\n");
    for (;;) VSync(0);
}
