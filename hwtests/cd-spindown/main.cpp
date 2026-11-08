/*
 * cdrom/cd-spindown - how long the drive is blind after a speed change with the head held, and how far the disc carries it: at
 * five places across the disc, double speed to single and back, GetlocP polled back to back for 1.5 s after each Setmode
 * (2026-11-08).
 *
 * WHY. STATUS "The relay stamps line edges by interrupt and loses no byte ..." (2026-11-08): after a double-to-single change under a
 * read this emulator skips 30 sectors (the drive program's slip rule) in 1-4 of 4 rows where the consoles never do (cd-pausehold,
 * 0 of 40), and cd-speed's 2X->1X SEEK / READ rows end a disc turn after every console's earliest mode, its STAT rows' first busy
 * answer ~3M cycles late (13.5M on the 5501, 16.4M here). The spindle's drive down (cdservo.js CLV_BRAKE_SHARE, 0.73 of each
 * console's brake, fitted at 00:02 only) or the PLL's capture (LOCK_TOL, 3.47% from the CXD2510Q's "+-150kHz ... or more") - the
 * two move these numbers alike at one place. Across the disc they need not: this measures the blind time and the carry at five
 * radii, both directions, on each console.
 *
 * WHAT IT DOES. Init, Mute; for each place P (00:02:16, 07:00:00, 14:00:00, 21:00:00, 26:30:00 - those inside the disc's lead-out,
 * from GetTD), twice: Setmode 80h (double), SeekP P to its INT2, 1.5 s; Setmode 00h (single), then GetlocP back to back for 1.5 s:
 * each answer's time from the Setmode's INT3 and its absolute position less P, in sectors, printed as "t:d" where d changed (and
 * the first), with the slowest answer; then 1.0 s; Setmode 80h, the same for 1.5 s. GetlocP answers the last good subcode Q, so
 * a held d marks the blind time and the first new d after it how far the disc carried the head: each row prints its longest
 * stretch with no change ("blind a-b", b 0 if it lasts to the end) and the positions either side, then the changes (80 at most). SeekP seeks by subcode: an audio
 * track is as good a place as data. Interrupts off; every wait bounded (rule 1); a half-set flag read again. Any pressed disc
 * reaching 26:30 (The X-Files Disc 1, Doom); a shorter one runs the places it has. No assertions - compare against your
 * emulator. Prints "Done.". About 70 s.
 *
 * Run here first: `node hwtests/run.js cd-spindown --disc=xfiles --frames=7000 --quiet=6000` (PSX_MODEL and PSX_CD_FIRMWARE the
 * console's; --disc=doom for Doom).
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
/* A command to its INT3 (the cycles from the write; 0 if none came), then with `second` its second response. */
static uint32_t timed(uint8_t c, const uint8_t *p, int np, bool second) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t t0 = now();
    command(c);
    uint8_t f; uint32_t at = 0;
    do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000));
    lastFlag = f;
    if (f == 3) at = now() - t0;
    if (second && f == 3) { do { f = take(MS(10000)); } while (f == 1 && now() - t0 < MS(10000)); lastFlag = f; }
    return at;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

#define N_PLACES 5
static const uint8_t PLACE[N_PLACES][3] = { { 0, 2, 16 }, { 7, 0, 0 }, { 14, 0, 0 }, { 21, 0, 0 }, { 26, 30, 0 } };
#define N_PTS 80
/* gapFrom..gapTo: the longest stretch with no change of position (gapTo 0: still none at the end), dBefore / dAfter its sides */
struct Trace { int n; uint32_t t[N_PTS]; int16_t d[N_PTS]; uint32_t slow, slowAt; int polls, other; uint8_t otherFlag;
               uint32_t gapFrom, gapTo; int16_t dBefore, dAfter; };
static Trace tr[2][N_PLACES][2];                           /* pass, place, 0 down / 1 up */
static bool have[N_PLACES];

/* Setmode `mode` at time 0, then GetlocP back to back for 1.5 s, the changes of the position less `target` into `out`. */
static void trace(uint8_t mode, int target, Trace &out) {
    out.n = 0; out.slow = 0; out.slowAt = 0; out.polls = 0; out.other = 0; out.otherFlag = 0;
    out.gapFrom = 0; out.gapTo = 0; out.dBefore = 0; out.dAfter = 0;
    uint32_t lastChange = 0; uint32_t best = 0;
    const uint8_t pm[1] = { mode }; timed(0x0E, pm, 1, false);
    const uint32_t T = now();
    int last = 0x7FFF;
    while (now() - T < MS(1500)) {
        const uint32_t a = timed(0x11, 0, 0, false);
        const uint32_t t = now() - T;
        out.polls++;
        if (!a) { if (!out.other) out.otherFlag = lastFlag; out.other++; continue; }
        if (a > out.slow) { out.slow = a; out.slowAt = t; }
        if (nrb >= 8) {
            const int lba = (unbcd(rb[5]) * 60 + unbcd(rb[6])) * 75 + unbcd(rb[7]);
            const int d = lba - target;
            if (d != last) {
                if (last != 0x7FFF && t - lastChange > best) { best = t - lastChange; out.gapFrom = lastChange; out.gapTo = t; out.dBefore = (int16_t)last; out.dAfter = (int16_t)d; }
                if (out.n < N_PTS) { out.t[out.n] = t; out.d[out.n] = (int16_t)d; out.n++; }
                last = d; lastChange = t;
            }
        }
    }
    if (last != 0x7FFF && now() - T - lastChange > best) { out.gapFrom = lastChange; out.gapTo = 0; out.dBefore = (int16_t)last; out.dAfter = (int16_t)last; }
}

int main() {
    printf("\ncdrom/cd-spindown\nAfter a speed change with the head held: GetlocP back to back for 1.5 s, at five places, double to single and back.\n");
    printf("Cycles at the system clock (1 ms = 33,869) from the Setmode's INT3; positions in sectors from the place. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    timed(0x0A, 0, 0, true);                                /* Init: the motor up, the drive settled */
    timed(0x0B, 0, 0, false);                               /* Mute */
    const uint8_t td[1] = { 0 }; timed(0x14, td, 1, false); /* GetTD 0: the lead-out */
    const int leadOut = nrb >= 3 ? (unbcd(rb[1]) * 60 + unbcd(rb[2])) * 75 : 74 * 60 * 75;
    for (int p = 0; p < N_PLACES; p++) have[p] = ((PLACE[p][0] * 60 + PLACE[p][1]) * 75 + PLACE[p][2]) + 150 < leadOut;
    for (int pass = 0; pass < 2; pass++)
        for (int p = 0; p < N_PLACES; p++) {
            if (!have[p]) continue;
            const int target = (PLACE[p][0] * 60 + PLACE[p][1]) * 75 + PLACE[p][2];
            const uint8_t up[1] = { 0x80 }; timed(0x0E, up, 1, false);
            const uint8_t pl[3] = { bcd(PLACE[p][0]), bcd(PLACE[p][1]), bcd(PLACE[p][2]) }; timed(0x02, pl, 3, false);
            timed(0x16, 0, 0, true);                        /* SeekP to its INT2: held there */
            idle(MS(1500));
            trace(0x00, target, tr[pass][p][0]);
            idle(MS(1000));
            trace(0x80, target, tr[pass][p][1]);
        }
    timed(0x09, 0, 0, true);                                /* Pause */
    timed(0x0C, 0, 0, false);                               /* Demute */
    ExitCriticalSection();
    printf("lead-out %02d:%02d\n", leadOut / 4500, (leadOut / 75) % 60);
    for (int pass = 0; pass < 2; pass++)
        for (int p = 0; p < N_PLACES; p++) {
            if (!have[p]) continue;
            for (int dir = 0; dir < 2; dir++) {
                const Trace &x = tr[pass][p][dir];
                printf("%02d:%02d:%02d %s: blind %uk-%uk (%d -> %d); %d polls, slowest %u at %uk;", PLACE[p][0], PLACE[p][1], PLACE[p][2],
                       dir ? "to 2X" : "to 1X", (unsigned)(x.gapFrom / 1000), (unsigned)(x.gapTo / 1000), x.dBefore, x.dAfter,
                       x.polls, (unsigned)x.slow, (unsigned)(x.slowAt / 1000));
                if (x.other) printf(" (%d not INT3, the first INT%u)", x.other, x.otherFlag);
                for (int i = 0; i < x.n; i++) printf(" %uk:%d", (unsigned)(x.t[i] / 1000), x.d[i]);
                printf("\n");
            }
        }
    printf("cdrom/cd-spindown: Done.\n");
    return 0;
}
