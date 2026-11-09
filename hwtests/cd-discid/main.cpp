/*
 * cdrom/cd-discid - which disc is in the drive: Init, then GetTN and GetTD from the drive program's own table of contents
 * (2026-11-08). Not a test: tools/consolerun.js runs it before a run's first CD program (--runner) and compares the lead-out with
 * the disc the run names, so a CD run on the wrong disc or an empty drive stops at once instead of filling a folder with numbers
 * from another disc. Twice in a day it would have: on 2026-11-07 the second SCPH-5501 ran three CD programs with no disc in (STATUS
 * "After a Pause ... CORRECTION"), and on 2026-11-08 the SCPH-7501's CD spread, labelled The X-Files, read Doom - the two discs
 * had changed consoles (its cd-ramdump held Doom's table of contents).
 *
 * Prints one line: "discid: stat SS tracks F-L lead-out MM:SS:FF starts MM:SS ..." (each track's start), or "discid: no disc
 * (<what the drive answered>)". Interrupts off; every wait bounded (rule 1). Prints "Done.".
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
    ackAll();
    return f;
}
/* A command to its first response (its flag; rb holds the bytes), then with `second` its second response's flag in *f2. */
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second, uint8_t *f2) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t f; const uint32_t t0 = now();
    do { f = take(MS(3000)); } while (f == 1 && now() - t0 < MS(3000));
    if (second) { uint8_t g = 0; if (f == 3) { do { g = take(MS(10000)); } while (g == 1 && now() - t0 < MS(10000)); } if (f2) *f2 = g; }
    return f;
}
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

int main() {
    printf("\ncdrom/cd-discid\nWhich disc is in the drive: Init, GetTN, GetTD.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    uint8_t f2 = 0;
    const uint8_t fi = cmd(0x0A, 0, 0, true, &f2);          /* Init to its INT2 */
    const uint8_t st = nrb ? rb[0] : 0;
    /* A DRIVE THAT FINISHES Init BEFORE IT HAS SPUN UP (2026-11-09, the PSone PM-41(2)'s SC430949: Init INT3 stat 00, its INT2,
     * then GetTN INT5 at once - every other console's GetTN answers straight after Init's INT2). Up to 15 s of Getstat (every
     * 50 ms) for the motor on and no seek or read under way, then GetTN up to 20 times 500 ms apart; INT5's bytes kept. */
    uint8_t gs = st; int polls = 0;
    for (const uint32_t t0 = now(); now() - t0 < MS(15000); polls++) {
        if (cmd(0x01, 0, 0, false, 0) == 3 && nrb) gs = rb[0];
        if ((gs & 0x02) && !(gs & 0xE0)) break;
        idle(MS(50));
    }
    uint8_t tn[3] = { 0, 0, 0 }; int ntn = 0;
    uint8_t ft = 0, e5[2] = { 0, 0 }; int tries = 0;
    for (; tries < 20; tries++) {
        ft = cmd(0x13, 0, 0, false, 0);                     /* GetTN: stat, first, last (BCD) */
        if (ft == 3) break;
        if (ft == 5) { e5[0] = nrb > 0 ? rb[0] : 0; e5[1] = nrb > 1 ? rb[1] : 0; }
        idle(MS(500));
    }
    for (int i = 0; i < 3 && i < nrb; i++) tn[i] = rb[i]; ntn = nrb;
    const uint8_t z[1] = { 0 };
    const uint8_t fl = cmd(0x14, z, 1, false, 0);           /* GetTD 0: the lead-out, stat mm ss */
    uint8_t lo[3] = { 0, 0, 0 }; for (int i = 0; i < 3 && i < nrb; i++) lo[i] = rb[i];
    int first = unbcd(tn[1]), last = unbcd(tn[2]);
    static uint8_t starts[100][2];
    int ns = 0;
    if (ft == 3 && fl == 3 && ntn >= 3 && first >= 1 && last >= first && last <= 99)
        for (int t = first; t <= last && ns < 100; t++) {
            const uint8_t p[1] = { (uint8_t)(((t / 10) << 4) | (t % 10)) };
            if (cmd(0x14, p, 1, false, 0) == 3 && nrb >= 3) { starts[ns][0] = rb[1]; starts[ns][1] = rb[2]; ns++; }
        }
    ExitCriticalSection();
    /* "discid-wait:", not "discid:" - tools/consolerun.js takes the first line with "discid:" as the answer */
    printf("discid-wait: Getstat %02X after %d polls; GetTN tries %d", gs, polls, tries + 1);
    if (ft != 3) printf(", last INT5 %02X %02X", e5[0], e5[1]);
    printf("\n");
    if (fi != 3 || f2 != 2 || ft != 3 || fl != 3 || ntn < 3)
        printf("discid: no disc (Init INT%u stat %02X, second INT%u; GetTN INT%u; GetTD INT%u)\n", fi, st, f2, ft, fl);
    else {
        printf("discid: stat %02X tracks %d-%d lead-out %02d:%02d starts", lo[0], first, last, unbcd(lo[1]), unbcd(lo[2]));
        for (int i = 0; i < ns; i++) printf(" %02d:%02d", unbcd(starts[i][0]), unbcd(starts[i][1]));
        printf("\n");
    }
    printf("cdrom/cd-discid: Done.\n");
    return 0;
}
