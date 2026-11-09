/*
 * cdrom/cd-pausewin - the drive program's busy windows after a Pause, seen from inside: GetlocP back to back for 1.2M cycles after
 * a paused read's INT2, each answer's time and wait and the subcode position it reports (2026-11-08).
 *
 * WHY. hwtests/cd-speedack2 (STATUS "After a Pause the drive program's answers wait out its hold jumps"): the SCPH-7501's program
 * is busy a sector after its hold jump - ~425k cycles after a double-speed Pause, ~925k after a single-speed one - where this
 * emulator's (SC430940) and the second SCPH-5501's are not; cd-pausehold's GetlocP every 25k showed no jump there. So what is the
 * 7501 doing? Back-to-back GetlocPs put an answer every ~25-60k: a slow one marks the busy window to a few thousand cycles, and the
 * position beside it says whether the subcode went stale (a lock blip: the same Q repeated, then a step) or the head moved (a jump).
 *
 * WHAT IT DOES. Init, Mute, SeekL 05:00:00 (inside the data of any data disc that reaches 05:10 - The X-Files, Doom's track 1); then
 * six rows, 2X and 1X interleaved: Setmode (80h / 00h), Setloc 05:00:00, ReadN, 8 sectors, Pause to its INT2; GetlocP back to back
 * for 1.2M cycles - each answer's time from the INT2, its wait (write to INT3) and its absolute position less 05:00:00 (sectors).
 * Printed per row: every answer that waited over 70,000 cycles (a GetlocP answers in ~25-45k) ("t/wait:d"), and every change of position ("t:d").
 * Interrupts off; every wait bounded (rule 1). No assertions - compare against your emulator. Prints "Done.". About 30 s.
 *
 * Run here first: `node hwtests/run.js cd-pausewin --disc=xfiles --frames=4000 --quiet=3500` (PSX_MODEL and PSX_CD_FIRMWARE
 * the console's; --disc=doom for Doom).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t p) { idx(0); *CD2 = p; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }
static void bfrd(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint8_t rb[16]; static int nrb;
static uint8_t take(uint32_t limit) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    nrb = 0; while (nrb < 16 && hasResp()) rb[nrb++] = resp();
    drain();
    if (f == 1) bfrd(false);
    ackAll();
    return f;
}
static uint32_t timed(uint8_t c, const uint8_t *p, int np, bool second) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t t0 = now();
    command(c);
    uint8_t f; uint32_t at = 0;
    do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000));
    if (f == 3) at = now() - t0;
    if (second && f == 3) { do { f = take(MS(10000)); } while (f == 1 && now() - t0 < MS(10000)); }
    return at;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static const int TARGET = (5 * 60 + 0) * 75 + 0;

#define N_ROWS 6
#define N_SLOW 24
#define N_MOVE 40
struct Row { int nSlow, nMove, polls; uint32_t slowT[N_SLOW], slowW[N_SLOW]; int16_t slowD[N_SLOW]; uint32_t moveT[N_MOVE]; int16_t moveD[N_MOVE]; };
static Row rows[N_ROWS];

int main() {
    printf("\ncdrom/cd-pausewin\nThe busy windows after a Pause from inside: GetlocP back to back for 1.2M cycles after a paused read's INT2.\n");
    printf("Cycles at the system clock (1 ms = 33,869) from the Pause's INT2; t/wait:d for answers over 70,000; t:d for changes of position (sectors from 05:00:00). No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    timed(0x0A, 0, 0, true);                                /* Init */
    timed(0x0B, 0, 0, false);                               /* Mute */
    const uint8_t pl[3] = { bcd(5), bcd(0), bcd(0) };
    timed(0x02, pl, 3, false); timed(0x15, 0, 0, true);     /* SeekL there once */
    for (int r = 0; r < N_ROWS; r++) {
        Row &row = rows[r]; row.nSlow = 0; row.nMove = 0; row.polls = 0;
        const uint8_t pm[1] = { (uint8_t)((r & 1) ? 0x00 : 0x80) }; timed(0x0E, pm, 1, false);
        timed(0x02, pl, 3, false);
        command(0x06);
        int n = 0; const uint32_t t0 = now();
        while (n < 8 && now() - t0 < MS(3000)) { const uint8_t f = take(MS(1000)); if (f == 1) n++; else if (f == 0) break; }
        timed(0x09, 0, 0, true);
        const uint32_t T = now();
        int last = 0x7FFF;
        while (now() - T < 1200000u) {
            const uint32_t w = timed(0x11, 0, 0, false);
            const uint32_t t = now() - T;
            row.polls++;
            int d = 0x7FFF;
            if (w && nrb >= 8) d = (unbcd(rb[5]) * 60 + unbcd(rb[6])) * 75 + unbcd(rb[7]) - TARGET;
            if ((!w || w > 70000u) && row.nSlow < N_SLOW) { row.slowT[row.nSlow] = t; row.slowW[row.nSlow] = w; row.slowD[row.nSlow] = (int16_t)d; row.nSlow++; }
            if (d != 0x7FFF && d != last && row.nMove < N_MOVE) { row.moveT[row.nMove] = t; row.moveD[row.nMove] = (int16_t)d; row.nMove++; last = d; }
        }
        timed(0x09, 0, 0, true);
    }
    timed(0x0C, 0, 0, false);                               /* Demute */
    ExitCriticalSection();
    for (int r = 0; r < N_ROWS; r++) {
        const Row &row = rows[r];
        printf("%s: %d polls; slow", (r & 1) ? "1X" : "2X", row.polls);
        for (int i = 0; i < row.nSlow; i++) printf(" %uk/%uk:%d", (unsigned)(row.slowT[i] / 1000), (unsigned)(row.slowW[i] / 1000), row.slowD[i]);
        printf("; moves");
        for (int i = 0; i < row.nMove; i++) printf(" %uk:%d", (unsigned)(row.moveT[i] / 1000), row.moveD[i]);
        printf("\n");
    }
    printf("cdrom/cd-pausewin: Done.\n");
    return 0;
}
