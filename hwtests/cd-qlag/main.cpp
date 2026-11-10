/*
 * cdrom/cd-qlag - how far the subcode Q runs ahead of the data sector the drive has just delivered: GetlocP sent a chosen delay
 * after an INT1 during a read, the Q it reports less that INT1's sector, and how long after the INT1 the answer came (2026-11-10).
 *
 * WHY. hwtests/cd-pausewin's hold reads 7, 8, -1 on The X-Files and 8, 9, 0 on Doom, on the same consoles (STATUS "The second 5501
 * pauses as the others do"; scratch/fable-cd/NOTES.md section 5). The drive's program (SC430940 / SC430930) steps its hold on a
 * header, then a Q: its INT2 comes at the first Q taken after the header before the target is read, so the Q GetlocP shows there is
 * set by how many subcode frames pass between a sector's subcode and that sector's INT1 - the data's lag behind the subcode, which
 * the emulator holds at 1.5 sectors for every disc, drive and speed (src/cdservo.js DATA_LAG 1 + DECODE_PHASE 0.5). The Pause
 * shows that lag only as a whole sector, once a row. This shows it at every sector: with the delay stepped through one sector time
 * in sixteenths, the delay at which the reported Q steps on is where the next subcode frame falls after an INT1 - the lag's fraction
 * - and the Q less the sector before the step its whole part. Every row also says whether the lag holds still (double speed on The
 * X-Files spans three classes in cd-pausewin) and whether a Q is ever missed (a step of two).
 *
 * WHAT IT DOES. Init, Mute; then eight rows - double speed and single speed at 05:00:00, then at 02:00:00, the four twice: Setmode,
 * Setloc, ReadN; 64 samples: wait for the next INT1 (every INT1 is counted, so its sector is the target + the count - 1), idle the
 * sample's delay (i mod 16 sixteenths of a sector time at that speed), send GetlocP and take its INT3. Recorded: the time from that
 * INT1 to the INT3 and the Q's absolute position less the INT1's sector. After every eighth sample a GetlocL checks the count (its
 * header against the last INT1 counted). Then Pause. Both places are data on any disc whose first track reaches 05:10.
 * Printed per row: the 64 samples "t:d" (t in thousands of cycles from the INT1 to the answer, d the Q less the sector), the INT1s
 * counted, the GetlocL checks that disagreed. Interrupts off; every wait bounded and the flag read again on a first 1 or 4 (rule 1).
 * No assertions - compare against your emulator. Prints "Done.". About 25 s.
 *
 * Run here first: `node hwtests/run.js cd-qlag --disc=xfiles --frames=4000 --quiet=3500` (PSX_MODEL and PSX_CD_FIRMWARE the console's).
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

#define N_ROWS 8
#define N_S 64
struct Row { int n, int1s, checks, off, offFirst; uint16_t t[N_S]; int8_t d[N_S]; };
static Row rows[N_ROWS];
static const int PLACE[2] = { (5 * 60 + 0) * 75, (2 * 60 + 0) * 75 };   /* 05:00:00, 02:00:00 */

int main() {
    printf("\ncdrom/cd-qlag\nThe subcode Q against the sector just delivered: GetlocP a stepped delay after an INT1 of a read.\n");
    printf("Per row 64 samples t:d - t thousands of cycles (1 ms = 33,869) from the INT1 to GetlocP's answer, d the Q's absolute position less that INT1's sector; the delay steps through one sector time in sixteenths, four times. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    timed(0x0A, 0, 0, true);                                /* Init */
    timed(0x0B, 0, 0, false);                               /* Mute */
    for (int r = 0; r < N_ROWS; r++) {
        Row &row = rows[r]; row.n = 0; row.checks = 0; row.off = 0; row.offFirst = 0;
        const bool dbl = !(r & 1);
        const int target = PLACE[(r >> 1) & 1];
        const uint32_t sector = dbl ? 225792u : 451584u;    /* 33,868,800 / 150 and / 75 */
        const uint8_t pm[1] = { (uint8_t)(dbl ? 0x80 : 0x00) }; timed(0x0E, pm, 1, false);
        const uint8_t pl[3] = { bcd(target / 4500), bcd((target / 75) % 60), bcd(target % 75) };
        timed(0x02, pl, 3, false);
        int1s = 0;
        command(0x06);
        { uint8_t f; const uint32_t t0 = now(); do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000)); }   /* ReadN's INT3 */
        const uint32_t T = now();
        for (int i = 0; i < N_S && now() - T < MS(8000); i++) {
            const int before = int1s;
            const uint32_t t0 = now();
            while (int1s == before && now() - t0 < MS(1000)) take(MS(1000));
            if (int1s == before) break;                      /* no sector in a second: the row ends here */
            const int mine = int1s; const uint32_t at = int1At;
            idle((uint32_t)(i & 15) * (sector / 16));
            const uint32_t w = timed(0x11, 0, 0, false);     /* GetlocP */
            const uint32_t t = now() - at;
            int d = 99;
            if (w && nrb >= 8) d = (unbcd(rb[5]) * 60 + unbcd(rb[6])) * 75 + unbcd(rb[7]) - (target + mine - 1);
            row.t[row.n] = (uint16_t)(t / 1000 > 65535 ? 65535 : t / 1000);
            row.d[row.n] = (int8_t)(d > 99 ? 99 : d < -99 ? -99 : d);
            row.n++;
            if ((i & 7) == 7) {                              /* GetlocL: the last header against the count */
                const uint32_t wl = timed(0x10, 0, 0, false);
                if (wl && nrb >= 3) {
                    const int h = (unbcd(rb[0]) * 60 + unbcd(rb[1])) * 75 + unbcd(rb[2]) - (target + int1s - 1);
                    row.checks++;
                    if (h) { if (!row.off) row.offFirst = h; row.off++; }
                }
            }
        }
        row.int1s = int1s;
        timed(0x09, 0, 0, true);                             /* Pause */
    }
    timed(0x0C, 0, 0, false);                               /* Demute */
    ExitCriticalSection();
    for (int r = 0; r < N_ROWS; r++) {
        const Row &row = rows[r];
        printf("%s %s: %d samples, %d INT1s, GetlocL %d checked %d off", (r & 1) ? "1X" : "2X", ((r >> 1) & 1) ? "02:00:00" : "05:00:00", row.n, row.int1s, row.checks, row.off);
        if (row.off) printf(" (first %d)", row.offFirst);
        printf(";");
        for (int i = 0; i < row.n; i++) printf(" %u:%d", (unsigned)row.t[i], row.d[i]);
        printf("\n");
    }
    printf("cdrom/cd-qlag: Done.\n");
    return 0;
}
