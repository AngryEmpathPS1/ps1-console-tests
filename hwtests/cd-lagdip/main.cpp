/*
 * cdrom/cd-lagdip - where the subcode Q falls after each INT1 over a read's first ~0.8 s right after a switch from single to double
 * speed, against the same read 3 s later (the Fable CD session, 2026-11-25).
 *
 * WHY. hwtests/cd-pause-settle: the first Pauses after Setmode 80h (right after a read's first INT1) take 3-6% longer than 3 s later
 * on the SCPH-1001 (every run, The X-Files) and the second SCPH-5501 (2 runs of 3, Doom), decaying over ~8 Pauses; the model has
 * none (console-logs/2026-11-23-pausesettle-README.txt). The drive's program (SC430916) kicks the Pause's jump back at the first
 * subcode-Q handler after it takes the Pause (code at 175Dh sets the stage byte 6Bh to 0Ch; scratch/scex/pausegate.js), so the
 * jump starts when the next Q comes after the INT1 - set by the data's lag behind the subcode. cd-pause-peek saw the jump start
 * ~1 ms later right after the switch; cd-qlag's double-speed rows (each right after a 1x row) put the Q step 0.4-0.6 ms later in
 * their first 16 samples than in their last on the 1001 and on Doom on the 5501 (scratch/scex/qlagdrift2.js), the 1x rows flat.
 * One reading: the DSP's output runs on the crystal while the spindle, locked in speed, is still behind in phase - the data
 * comes out sooner against the disc, then the CLV pulls the phase back. This times that dip and its decay, and how often it comes.
 *
 * WHAT IT DOES. Init, Demute; eight pairs: Setmode 00h, Setloc 00:02:16, ReadN, 20 INT1s, Pause (a settled single-speed read); then
 * Setmode 80h and the "up" row: Setloc 00:02:16, ReadN, 96 samples - wait for the next INT1, idle (i mod 16) sixteenths of a sector
 * time, GetlocP, take its INT3; Pause, wait 3 s, the "set" row: the same with 48 samples; Pause. Per sample: s, the INT1's time
 * from the Setmode 80h's INT3 (thousand cycles), t, the INT1 to GetlocP's answer (thousand cycles), d, the Q's absolute position
 * less the INT1's sector. The step of d within each 16 samples is where the next Q falls after an INT1 (cd-qlag's method).
 * Printed per row: the first INT1's s, then "s:t:d" for every sample. Interrupts off; every wait bounded and the flag read again on
 * a first 1 or 4 (rule 1). No assertions - compare against your emulator. Prints "Done.". About 60 s.
 *
 * Run here first: `node hwtests/run.js cd-lagdip --disc=xfiles --frames=7000 --quiet=6500 --settle=600` (PSX_MODEL and PSX_CD_FIRMWARE the console's).
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
static int int1s;                 /* every INT1 taken since the row's ReadN */
static uint32_t int1At;           /* ...and when the last one was */
static uint8_t take(uint32_t limit) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    const uint32_t at = now();
    nrb = 0; while (nrb < 16 && hasResp()) rb[nrb++] = resp();
    drain();
    if (f == 1) { bfrd(false); int1s++; int1At = at; }
    ackAll();
    return f;
}
/* A command to its INT3 (INT1s on the way are counted); with `second`, on to its INT2. Returns the time to the INT3, 0 on none. */
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

#define N_PAIRS 8
#define N_UP 96
#define N_SET 48
#define TARGET (2 * 75 + 16)       /* 00:02:16 as GetlocP counts it, the 150-sector pregap included */
#define SECTOR2X 225792u           /* 33,868,800 / 150 */
struct Row { int n, first; uint32_t s[N_UP]; uint16_t t[N_UP]; int8_t d[N_UP]; };
static Row up[N_PAIRS], set[N_PAIRS];
static const uint8_t LOC[3] = { 0x00, 0x02, 0x16 };

static uint16_t k16(uint32_t v) { v /= 1000; return (uint16_t)(v > 65535 ? 65535 : v); }
/* Setloc, ReadN, `n` samples timed from `t0`; then Pause. */
static void row(Row &r, int n, uint32_t t0) {
    r.n = 0; r.first = -1;
    timed(0x02, LOC, 3, false);
    int1s = 0;
    command(0x06);
    { uint8_t f; const uint32_t a = now(); do { f = take(MS(3000)); } while (f == 1 && now() - a < MS(3000)); }   /* ReadN's INT3 */
    const uint32_t T = now();
    for (int i = 0; i < n && now() - T < MS(8000); i++) {
        const int before = int1s;
        const uint32_t a = now();
        while (int1s == before && now() - a < MS(1500)) take(MS(1500));
        if (int1s == before) break;                          /* no sector in 1.5 s: the row ends here */
        const int mine = int1s; const uint32_t at = int1At;
        if (r.first < 0) r.first = (int)((at - t0) / 1000);
        idle((uint32_t)(i & 15) * (SECTOR2X / 16));
        const uint32_t w = timed(0x11, 0, 0, false);         /* GetlocP */
        const uint32_t t = now() - at;
        int d = 99;
        if (w && nrb >= 8) d = (unbcd(rb[5]) * 60 + unbcd(rb[6])) * 75 + unbcd(rb[7]) - (TARGET + mine - 1);
        r.s[r.n] = (at - t0) / 1000; r.t[r.n] = k16(t);
        r.d[r.n] = (int8_t)(d > 99 ? 99 : d < -99 ? -99 : d);
        r.n++;
    }
    timed(0x09, 0, 0, true);                                 /* Pause */
}
static void print(const char *label, int p, const Row &r) {
    printf("%s %d: first INT1 %d; %d samples;", label, p, r.first, r.n);
    for (int i = 0; i < r.n; i++) printf(" %u:%u:%d", (unsigned)r.s[i], (unsigned)r.t[i], r.d[i]);
    printf("\n");
}

int main() {
    printf("\ncdrom/cd-lagdip\nThe subcode Q against the sector just delivered, over a read's first 0.8 s after Setmode 80h (\"up\") and 3 s later (\"set\").\n");
    printf("Per sample s:t:d - s thousands of cycles (1 ms = 33,869) from the Setmode's INT3 to the INT1, t from the INT1 to GetlocP's answer, d the Q's absolute position less that INT1's sector; the delay steps through a 2x sector time in sixteenths. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    timed(0x0A, 0, 0, true);                                /* Init */
    timed(0x0C, 0, 0, false);                               /* Demute */
    for (int p = 0; p < N_PAIRS; p++) {
        const uint8_t m1[1] = { 0x00 }; timed(0x0E, m1, 1, false);   /* single speed, a settled read there */
        timed(0x02, LOC, 3, false);
        int1s = 0; command(0x06);
        { const uint32_t a = now(); while (int1s < 20 && now() - a < MS(5000)) take(MS(1500)); }
        timed(0x09, 0, 0, true);
        const uint8_t m2[1] = { 0x80 };
        timed(0x0E, m2, 1, false);
        const uint32_t t0 = now();                          /* the Setmode 80h's INT3 */
        row(up[p], N_UP, t0);
        idle(MS(3000));
        row(set[p], N_SET, t0);
    }
    ExitCriticalSection();
    for (int p = 0; p < N_PAIRS; p++) { print("up ", p, up[p]); print("set", p, set[p]); }
    printf("cdrom/cd-lagdip: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
