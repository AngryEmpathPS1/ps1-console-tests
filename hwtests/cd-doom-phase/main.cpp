/*
 * cdrom/cd-doom-phase - Doom (USA)'s level-load seeks, each started at 17 places across the disc's turn (2026-11-15).
 * Needs Doom (USA) in the drive (lead-out 27:30; hwtests/cd-discid).
 *
 * WHY. hwtests/cd-doom-load on the second SCPH-5501 x6 (STATUS, 2026-11-15): the game's ReadN from 29,300 to 30,232 (its
 * fourth of the level load, after 30 sectors read and a Pause) takes 8.0-8.3M cycles in 30 repeats of 36 and 5.5-5.8M in six;
 * this emulator's drive program (SC430930) reads the target at once every time, 3.7M - by 0.2M: its last single jump lands
 * 3.5 sectors before the target, and a sector later the decoder's first header is the target itself, which the program does
 * not take (it jumps back two tracks and reads up: 8.76M). The game writes that ReadN at ONE place in the drive's hold, so
 * cd-doom-load shows one point of the curve. This walks the start across a whole turn: where the console's seek goes from
 * one way to the other, against where this core's does, is the lateness (or the longer blind) to model.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, every wait bounded, Timer 2 at the system clock unwrapped in software
 * (cd-doom-load's helpers). Double speed. Per cell, per step k = 0..16, twice: Setloc A, ReadN, n sectors taken, Pause P cycles
 * after the last INT1, its INT2; then W + k x 150,000 cycles after that INT2, Setloc B; Setloc B again 3,300 after its INT3 (as
 * the game does); ReadN 2,700 after that INT3; the ReadN's write to its first INT1; Pause 20,000 after it. The cells are the
 * game's own (hwtests/cd-doom-load's table): A, n, P, W, B =
 *   L4  29270 x30, 22,400,     5,700 -> 30232   (the level's 4th read: 932 sectors on)
 *   L5  30232 x71, 41,800,    73,900 -> 4757    (its 5th: 25,480 back)
 *   L7   4757 x14, 43,400, 10,591,000 -> 7157    (its 7th: 2,386 on)
 *   L9   7157 x45, 29,200,  5,303,700 -> 4942    (its 9th: 2,260 back)
 *   M2   2408 x4,  19,700,     5,900 -> 3613    (the menu's 2nd: 1,201 on)
 *   M3   3613 x19, 21,100, 1,278,800 -> 2408    (its 3rd: 1,224 back)
 * 150,000 cycles is 0.66 sector at double speed; 17 steps are 2.4M, a turn at 30,000 (2.44M) and more than one further in.
 *
 * READ IT LIKE THIS. A row per cell and pass: the seventeen first-INT1 times in k cycles (0: a timeout or an INT5), k = 0 first -
 * k = 0 is the game's own start. Then per cell the Pause's write to its INT2 (min-max over its 34 seeks).
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-doom-phase --disc=doom --ntsc --frames=20000 --quiet=6000 --settle=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)
#define NCELL 6
#define NSTEP 17
#define NPASS 2
#define STEP 150000u

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

struct Cell { const char *name; uint32_t a; int n; uint32_t p, w, b; };
static const Cell CELL[NCELL] = {
    { "L4", 29270, 30, 22400, 5700, 30232 },
    { "L5", 30232, 71, 41800, 73900, 4757 },
    { "L7", 4757, 14, 43400, 10591000, 7157 },
    { "L9", 7157, 45, 29200, 5303700, 4942 },
    { "M2", 2408, 4, 19700, 5900, 3613 },
    { "M3", 3613, 19, 21100, 1278800, 2408 },
};
static uint32_t T[NCELL][NPASS][NSTEP], pMin[NCELL], pMax[NCELL];
static int bad;

int main() {
    printf("\ncdrom/cd-doom-phase\n");
    printf("Doom (USA)'s level-load seeks, each started at 17 places across the disc's turn. Needs Doom (USA) in the drive.\n");
    printf("About three minutes; nothing prints until the end.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    ackAll(); command(0x0A); take(MS(3000)); takeKind(2, MS(8000));       /* Init */
    ackAll(); param(0x80); command(0x0E); takeKind(3, MS(3000));
    idleUntil(now() + MS(1000));

    for (int c = 0; c < NCELL; c++) { pMin[c] = 0xFFFFFFFFu; pMax[c] = 0; }
    for (int c = 0; c < NCELL && bad <= 12; c++) for (int pass = 0; pass < NPASS; pass++) for (int k = 0; k < NSTEP && bad <= 12; k++) {
        const Cell &ce = CELL[c];
        setloc(ce.a); Got g = takeKind(3, MS(3000));
        idleUntil(g.t + 3300); setloc(ce.a); g = takeKind(3, MS(3000));
        idleUntil(g.t + 2700); ackAll(); command(0x06); takeKind(3, MS(3000));
        bool ok = true;
        for (int i = 0; ok && i < ce.n; i++) { g = takeKind(1, MS(10000)); ok = g.flag == 1; }
        idleUntil(g.t + ce.p);
        ackAll(); uint32_t tw = now(); command(0x09); takeKind(3, MS(3000)); g = takeKind(2, MS(8000));
        if (g.flag == 2) { const uint32_t d = g.t - tw; if (d < pMin[c]) pMin[c] = d; if (d > pMax[c]) pMax[c] = d; } else ok = false;
        idleUntil(g.t + ce.w + (uint32_t)k * STEP);
        setloc(ce.b); g = takeKind(3, MS(3000));
        idleUntil(g.t + 3300); setloc(ce.b); g = takeKind(3, MS(3000));
        idleUntil(g.t + 2700); ackAll(); tw = now(); command(0x06); takeKind(3, MS(3000));
        g = takeKind(1, MS(10000));
        T[c][pass][k] = (ok && g.flag == 1) ? g.t - tw : 0;
        if (!(ok && g.flag == 1)) bad++;
        idleUntil(g.t + 20000);
        ackAll(); command(0x09); takeKind(3, MS(3000)); takeKind(2, MS(8000));
        idleUntil(now() + 200000);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("Per cell and pass: the ReadN's write to its first INT1, k cycles, at start delays +0, +150,000 ... +2,400,000:\n");
    for (int c = 0; c < NCELL; c++) for (int pass = 0; pass < NPASS; pass++) {
        printf("%s %5u x%-2d -> %5u:", CELL[c].name, (unsigned)CELL[c].a, CELL[c].n, (unsigned)CELL[c].b);
        for (int k = 0; k < NSTEP; k++) printf(" %5u", (unsigned)((T[c][pass][k] + 500) / 1000));
        printf("\n");
    }
    printf("The Pause before each seek, its write to its INT2, min-max k cycles:");
    for (int c = 0; c < NCELL; c++) printf(" %s %u-%u", CELL[c].name, (unsigned)((pMin[c] + 500) / 1000), (unsigned)((pMax[c] + 500) / 1000));
    printf("\nseeks that failed (a timeout or an INT5): %d\n", bad);
    printf("\ncdrom/cd-doom-phase: Done.\n");
    for (;;) VSync(0);
}
