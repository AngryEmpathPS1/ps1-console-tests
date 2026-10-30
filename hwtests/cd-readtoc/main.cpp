/*
 * cdrom/cd-readtoc - how long does ReadTOC take from where the head is, and
 * where does it leave the head?
 *
 * WHY (M281). The BIOS re-reads the TOC before every LoadExec on a drive dated
 * 95/07/06 or later: the shell sets A000DFFC from Test 20h's date (bfc1e0ac),
 * and LoadExec then sends ReadTOC and GetID before it loads (bfc0d588). Every
 * console after the SCPH-1001's drive takes that path, with the head wherever
 * the last load left it.
 *   The SCPH-9001's own program, run as this emulator's drive (CDROMLLE),
 *   drops to single speed and walks the head in with short sled pulses to the
 *   lead-in. From 65:14 on Resident Evil Director's Cut the INT2 comes 3.2 s
 *   after the command; from 00:02:00, 0.46 s. How far each pulse carries the
 *   sled is the servo model's ASSUMPTION (src/cdservo.js SLED_DRIVE_TPS,
 *   SLED_TAU_S, fitted to M237's long seek), so the far rows are its question.
 *   psx-spx: "the second response appears after about 1 second delay" - from
 *   wherever nocash's head was.
 *   The model (src/psx.js CDROM) answers INT2 after CD_INIT_COMPLETE (476,300
 *   cycles, 14 ms: Init's average from ps1-tests' log) wherever the head is.
 *   It reports the SCPH-1001's drive (95/05/16), so the BIOS never asks it.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled; Timer 2 at the system
 * clock, every 16-bit step added into 32 bits by the polling loops (as
 * cd-init). The disc's length is read (GetTD 0), and a disc with music after
 * its data keeps to the data (GetTD(2), as cd-pause). Then for each position -
 * the start (lba 0), a quarter, a half and nine tenths of the data - twice:
 *   Setloc + SeekL there and its INT2; 500 ms idle; ReadTOC: its INT3 from the
 *   write and its INT2 from the INT3, each with its stat; then GetlocP, which
 *   says where the head was left (track, index, absolute time).
 * Then the same from nine tenths at DOUBLE speed (Setmode 80h), twice, and one
 * ReadTOC straight after another. It needs a disc in the drive (any data
 * disc; a longer one reaches further). About 30 seconds.
 *
 * READ IT LIKE THIS. Cycles at 33.8688 MHz (1 s = 33,868,800). INT2 growing
 * with the start position is a walk to the lead-in; its slope is the sled's
 * speed on the way in, which the drive program's model takes from an
 * assumption. A flat INT2 near 476,300 is the model's constant.
 *
 * ON THIS CORE AT v0.281 (`node hwtests/run.js cd-readtoc --disc --frames=5000
 * --quiet=4000`, cputest's synthetic disc, 74:15):
 *   THE MODEL: INT3 81,131-81,152 and INT2 476,284-476,297 after it from every
 *   position and both speeds, stat 02h, and GetlocP where the head already was
 *   (00:01:71, 18:35:53, 37:09:25, 66:51:33): nothing moved.
 *   THE DRIVE'S OWN PROGRAM (PSX_CD_FIRMWARE=CDROM-firmware/SC430944.S19):
 *   INT3 61,370-136,929 (Init's range), INT2 19.6-19.7M from lba 0, 65.0M from
 *   18:35, 83.6M from 37:09, 120.1M from 66:51, 120.0-128.0M at double speed
 *   (it reads the TOC at single speed), 21.6M straight after another; stat 12h
 *   throughout (the power-on shell latch, which only Getstat clears); GetlocP
 *   00:01:72-73 every time - it parks the head at the start of the program area.
 *   On The X-Files (USA) Disc 1 (57:59, the console run's disc; add
 *   `--disc=<its image> --ntsc`): INT2 19.6M from 0, 60.6M from 14:31, 65.7-69.3M
 *   from 29:01, 105.8M from 52:11, 105.7-113.0M at double speed, 21.6M again.
 * CONSOLE DAY (2026-10-05, The X-Files): the SCPH-5501 21-41M from 0, 150-194M from 14:31, 218-246M from 29:01,
 *   255-274M from 52:11; the SCPH-7501 0.7-1.0 s, 1.9-2.2, 3.3-3.4, 4.8 s. The walk is the program's 2N-track jumps
 *   inward, 128 tracks a step (CX 700400, 4D00), a Q read between - so each console's step time is the measure: the
 *   5501's ~19 ms on the outer stretch, ~49 in the middle and ~86 on the inner one, the 7501's 24-34 ms, the
 *   programs on this servo ~20-25 (its jumps at 20,000 tracks/s; the sled constant does not enter). Slower jumps
 *   alone cannot give the 5501's shape. Since then the 1x+ rows add five starting points (an eighth to seven
 *   eighths): on SC430930 here 33.1M, 56.6M, 82.1M, 86.6M, 100.3M; the model 476k from each. On a drive as slow
 *   as the 5501's they add ~40 s, so the run is now
 *   `node hwtests/run.js cd-readtoc --disc --frames=7500 --quiet=4000`
 *   v0.377.156 (2026-10-21): its rows reach the TTY only near the end of the run, so --quiet bounds the WHOLE run (~4,000
 *   frames on the drive programs); v0.377.156's 2N jumps landing short (more single jumps after) took it past 4,000 and
 *   the plain-start references came back empty (STATUS v0.377.156). Now
 *   `node hwtests/run.js cd-readtoc --disc --frames=7500 --quiet=7500`
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

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
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static uint8_t bcd(uint32_t v) { return ((v / 10) << 4) | (v % 10); }
static uint32_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

/* Wrap-safe: every loop that waits calls now(), far more often than once per 65,536 cycles. */
static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

struct Got { uint8_t flag, stat; uint32_t t; };
/* Wait for any flag (up to `limit` cycles), note when, read its first response byte, drain, acknowledge. */
static Got take(uint32_t limit = MS(3000)) {
    Got g = { 0, 0, 0 };
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        const uint8_t f = irqFlag();
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    if (hasResp()) g.stat = resp();
    drain();
    ackAll();
    return g;
}
/* A command with no parameter response of interest: its INT3, and with `second` its INT2. */
static Got simple(uint8_t c, bool second = false) {
    ackAll();
    command(c);
    Got a = take();
    if (second && a.flag == 3) return take(MS(8000));
    return a;
}
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}

struct Row { Got a, b; uint32_t t0; uint8_t loc[8]; int locN; };
/* ReadTOC: its INT3 from the write, its INT2 from the INT3; then GetlocP. */
static Row readToc() {
    Row r;
    ackAll();
    r.t0 = now();
    command(0x1E);
    r.a = take();
    r.b = r.a.flag == 3 ? take(MS(10000)) : Got{ 0, 0, 0 };
    ackAll();
    command(0x11);                                   /* GetlocP */
    const uint32_t t0 = now();
    while (!irqFlag() && now() - t0 < MS(1000)) {}
    r.locN = 0;
    while (hasResp() && r.locN < 8) r.loc[r.locN++] = resp();
    drain(); ackAll();
    return r;
}

static void show(const char *speed, uint32_t lba, const Row &r) {
    printf("%s from lba=%6u: INT%d %8u (%02x)", speed, (unsigned)lba, r.a.flag, (unsigned)(r.a.t - r.t0), r.a.stat);
    if (r.b.flag) printf("   INT%d %9u after it (%02x)", r.b.flag, (unsigned)(r.b.t - r.a.t), r.b.stat);
    else printf("   no second response");
    if (r.locN == 8) printf("   then GetlocP %02x %02x %02x:%02x:%02x\n", r.loc[0], r.loc[1], r.loc[5], r.loc[6], r.loc[7]);
    else printf("   then GetlocP (%d bytes)\n", r.locN);
}

static Row rows[4][2], dbl[2], again, more[5];

int main() {
    printf("\ncdrom/cd-readtoc\n");
    printf("ReadTOC's two responses from where the head is, and where it leaves it. Needs a disc. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();

    simple(0x0A, true);                              /* Init, and its INT2 */
    simple(0x0C);                                    /* Demute */

    param(0); ackAll(); command(0x14);               /* GetTD(0): the disc's length */
    { const uint32_t t0 = now(); while (!irqFlag() && now() - t0 < MS(1000)) {} }
    resp(); const uint32_t mm = unbcd(resp()), ss = unbcd(resp()); drain(); ackAll();
    const uint32_t total = (mm * 60 + ss) * 75;
    ackAll(); command(0x13);                         /* GetTN */
    { const uint32_t t0 = now(); while (!irqFlag() && now() - t0 < MS(1000)) {} }
    resp(); resp(); const uint32_t lastTrack = unbcd(resp()); drain(); ackAll();
    uint32_t end = total;
    if (lastTrack >= 2) {
        param(bcd(2)); ackAll(); command(0x14);      /* GetTD(2): where the music starts */
        { const uint32_t t0 = now(); while (!irqFlag() && now() - t0 < MS(1000)) {} }
        resp(); const uint32_t m2m = unbcd(resp()), m2s = unbcd(resp()); drain(); ackAll();
        end = (m2m * 60 + m2s) * 75;
    }
    const uint32_t pos[4] = { 0, end / 4, end / 2, end * 9 / 10 };

    param(0x00); simple(0x0E);                       /* Setmode 00h: single speed */
    for (int p = 0; p < 4; p++) {
        for (int i = 0; i < 2; i++) {
            setloc(pos[p]); simple(0x15, true);      /* SeekL there, and its INT2 */
            idle(MS(500));
            rows[p][i] = readToc();
        }
    }
    param(0x80); simple(0x0E);                       /* Setmode 80h: double speed */
    idle(MS(1000));
    for (int i = 0; i < 2; i++) {
        setloc(pos[3]); simple(0x15, true);
        idle(MS(500));
        dbl[i] = readToc();
    }
    again = readToc();                               /* straight after the last one */
    param(0x00); simple(0x0E);
    /* MORE POSITIONS (2026-10-05, after console day): the walk's time by where it starts, finer. The SCPH-5501's
     * 128-track steps took ~19 ms on the outer stretch, ~49 in the middle and ~86 on the inner one; the SCPH-7501's
     * 24-34 ms throughout; this emulator's drive programs ~20-25. Once each, after the rows above (which stay as
     * they were, so console day's logs still line up). */
    const uint32_t morePos[5] = { end / 8, end * 3 / 8, end * 5 / 8, end * 3 / 4, end * 7 / 8 };
    for (int p = 0; p < 5; p++) {
        setloc(morePos[p]); simple(0x15, true);
        idle(MS(500));
        more[p] = readToc();
    }

    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("disc: %u sectors (%u:%02u)", (unsigned)total, (unsigned)(total / 4500), (unsigned)((total / 75) % 60));
    if (end != total) printf(", its data to %u:%02u", (unsigned)(end / 4500), (unsigned)((end / 75) % 60));
    printf("\n");
    for (int p = 0; p < 4; p++) for (int i = 0; i < 2; i++) show("1x", pos[p], rows[p][i]);
    for (int i = 0; i < 2; i++) show("2x", pos[3], dbl[i]);
    show("again", pos[3], again);
    for (int p = 0; p < 5; p++) show("1x+", morePos[p], more[p]);
    printf("Done.\n");
    for (;;) {}
}
