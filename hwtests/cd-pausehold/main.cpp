/*
 * cdrom/cd-pausehold - where the head is after a Pause, over time: GetlocP polled every ~25k cycles for 1.5M cycles after a paused
 * read's INT2, at double speed and at single (2026-11-07).
 *
 * WHY. hwtests/cd-speedack2 (STATUS "After a Pause the drive program's answers wait out its hold jumps"): the drive's program holds
 * its answers through each hold jump, and the SCPH-7501, the second SCPH-5501 and this emulator place those jumps three ways - the
 * 7501 has this emulator's windows and one more a sector after the jump, the 5501 none at all. This emulator's head stands 13 tracks
 * past the target at the Pause's INT2 and jumps back once, about a sector later. GetlocP answers the last good subcode Q: its
 * absolute MSF traces the head - a jump shows as the position going back, by how far, and when - and a slow answer marks the
 * program busy, as before.
 *
 * WHAT IT DOES. Init, Mute, a SeekL to 20:57:19 (the drive up and settled first); four rows at each speed, interleaved (2X, 1X, ...): Setmode (80h / 00h), Setloc 20:57:19, ReadN, 8 sectors,
 * Pause to its INT2; then GetlocP repeatedly, each 25k cycles after the last one's answer, for 1.5M cycles: each answer's time (from
 * the INT2) and its absolute position less 20:57:19's, in sectors. Printed per row as "t:d" pairs where d changed (and the first),
 * with the slowest answer. Interrupts off; every wait bounded (rule 1). Any data disc reaching 21:00 (The X-Files Disc 1).
 * No assertions - compare against your emulator. Prints "Done.".
 *
 * Run here first: `node hwtests/run.js cd-pausehold --disc=xfiles --frames=6000 --quiet=5000` (PSX_MODEL and PSX_CD_FIRMWARE the
 * console's).
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
/* The next flag within `limit` cycles (0: none); its whole response into rb (nrb bytes); acknowledged. */
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
static uint8_t lastFlag;
static uint32_t timed(uint8_t c, const uint8_t *p, int np, bool second) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t t0 = now();
    command(c);
    uint8_t f; uint32_t at = 0;
    do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000));
    lastFlag = f;
    if (f == 3) at = now() - t0;
    if (second && f == 3) { do { f = take(MS(10000)); } while (f == 1 && now() - t0 < MS(10000)); }
    return at;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static const int TARGET = (20 * 60 + 57) * 75 + 19;

#define N_ROWS 8
#define N_PTS 64
struct Row { int n; uint32_t t[N_PTS]; int16_t d[N_PTS]; uint32_t slow, slowAt; int other; uint8_t otherFlag, otherByte; };
static Row rows[N_ROWS];

int main() {
    printf("\ncdrom/cd-pausehold\nWhere the head is after a Pause: GetlocP every ~25k cycles for 1.5M cycles after a paused read's INT2.\n");
    printf("Cycles at the system clock (1 ms = 33,869) from the Pause's INT2; positions in sectors from 20:57:19. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    timed(0x0A, 0, 0, true);                                /* Init: the motor up, the drive settled */
    timed(0x0B, 0, 0, false);                               /* Mute */
    { const uint8_t pl[3] = { bcd(20), bcd(57), bcd(19) }; timed(0x02, pl, 3, false); timed(0x15, 0, 0, true); }   /* SeekL there once */
    for (int r = 0; r < N_ROWS; r++) {
        Row &row = rows[r]; row.n = 0; row.slow = 0; row.slowAt = 0; row.other = 0; row.otherFlag = 0; row.otherByte = 0;
        const uint8_t pm[1] = { (uint8_t)((r & 1) ? 0x00 : 0x80) }; timed(0x0E, pm, 1, false);
        const uint8_t pl[3] = { bcd(20), bcd(57), bcd(19) }; timed(0x02, pl, 3, false);
        command(0x06);
        int n = 0; const uint32_t t0 = now();
        while (n < 8 && now() - t0 < MS(3000)) { const uint8_t f = take(MS(1000)); if (f == 1) n++; else if (f == 0) break; }
        timed(0x09, 0, 0, true);
        const uint32_t T = now();
        int last = 0x7FFF;
        while (now() - T < 1500000u) {
            const uint32_t a = timed(0x11, 0, 0, false);
            if (!a) { if (!row.other) { row.otherFlag = lastFlag; row.otherByte = nrb > 1 ? rb[1] : 0; } row.other++; }
            const uint32_t t = now() - T;
            if (a > row.slow) { row.slow = a; row.slowAt = t; }
            if (nrb >= 8) {
                const int lba = (unbcd(rb[5]) * 60 + unbcd(rb[6])) * 75 + unbcd(rb[7]);
                const int d = lba - TARGET;
                if (d != last && row.n < N_PTS) { row.t[row.n] = t; row.d[row.n] = (int16_t)d; row.n++; last = d; }
            }
            idle(25000);
        }
        timed(0x09, 0, 0, true);
    }
    timed(0x0C, 0, 0, false);                               /* Demute */
    ExitCriticalSection();
    for (int r = 0; r < N_ROWS; r++) {
        const Row &row = rows[r];
        printf("%s: slowest answer %u at %uk;", (r & 1) ? "1X" : "2X", (unsigned)row.slow, (unsigned)(row.slowAt / 1000));
        if (row.other) printf(" (%d answers not INT3, the first INT%u %02X)", row.other, row.otherFlag, row.otherByte);
        for (int i = 0; i < row.n; i++) printf(" %uk:%d", (unsigned)(row.t[i] / 1000), row.d[i]);
        printf("\n");
    }
    printf("cdrom/cd-pausehold: Done.\n");
    return 0;
}
