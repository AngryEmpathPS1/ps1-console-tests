/*
 * cdrom/cd-scex - where on the disc the drive finds the SCEx string, and how often (2026-10-05, for the SCPH-9001).
 *
 * WHY. psx-spx (19h,04h): "the SCEx is stored only in the Lead-In area, so, if the drive head is elsewhere, it will
 * usually not find any strings". Console day said otherwise: cd-cmds' S2 - Test04 with the head at 00:02:19 of The
 * X-Files Disc 1, two seconds, Test05 - read 03h,03h on the SCPH-5501 (the CXD2545Q board, the string serially on the
 * drive CPU's Port B bit 1) and 02h,01h on the SCPH-7501 (the CXD2938Q, keyed SUBQ packets). This emulator puts the
 * string in the lead-in only (src/cdservo.js scexFrame, scexLine), a string every 19.2 frames - which would give
 * about seven in S2's 1.8 s even where it is. So two things are unmeasured: how far into the data area the wobble
 * carries the string, and how far apart the strings are. This asks both of one disc, on each drive.
 *
 * WHAT IT DOES. The counters the drive holds first (B: Test05 before anything, history). Then for each START
 * position: Setloc + ReadN, two sectors, Pause (the head parked there); Test04 (counters reset, receiving on - and on
 * the SCPH-5501 the drive READS from there at single speed, console day); a wait; Test05; every INT1 counted until
 * Test05's INT3; Pause. P rows wait 2 s at positions from the pregap (00:00:10) to 00:40:00; T rows wait 0.5, 1, 2,
 * 4 and 8 s at 00:02:00, so the counts against time give the strings' spacing.
 *
 * READ IT LIKE THIS. Each row: "<3> total success" from Test05 (strings begun, strings matched), then +n x<1> to
 * SS:FF if sectors came first (the C2 answers behind them). The data-area rows that read 00 00 bound the wobble's
 * extent; the T rows' success against the wait gives the period (a string is 4 characters of 11 bits at 250 baud,
 * 176 ms, plus the gap - this emulator assumes 80 ms).
 *
 * SAFE FOR THE DRIVE: nothing past 00:40:00 of a 57:59 disc, ~1.5 minutes. Run it with The X-Files Disc 1 (or any
 * licensed disc - say which).
 *
 * THE RULE: run here first. ON THIS CORE AT v0.364 (`node hwtests/run.js cd-scex --disc --frames=9000 --quiet=1000`): every row
 * 00 00 on the model (no string outside the lead-in) - the console's numbers are what a model would be fitted to.
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
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t data() { idx(0); return *CD2; }

static uint32_t clk8; static uint16_t lastT;
static inline uint32_t now() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; return clk8 * 8; }
static void waitCycles(uint32_t c) { uint32_t t0 = now(); while (now() - t0 < c) {} }

static char line[200]; static int lp;
static void out(const char* s) { while (*s && lp < 190) line[lp++] = *s++; }
static void outHex(uint8_t v) { const char* h = "0123456789ABCDEF"; line[lp++] = h[v >> 4]; line[lp++] = h[v & 15]; }
static void outDec(int v) { char b[12]; int k = 0; do { b[k++] = '0' + v % 10; v /= 10; } while (v); while (k) line[lp++] = b[--k]; }
static void flush() { line[lp] = 0; printf("%s\n", line); lp = 0; }

/* The next interrupt's number, or 0 after ~2 s of nothing (every wait is bounded: a console must never hang). */
static uint8_t waitAny() { uint32_t t0 = now(); for (;;) { uint8_t f = irqFlag(); if (f) return f; if (now() - t0 > 70000000) return 0; } }
/* A sector's INT1: its stat, and the header's SS:FF (whole-sector mode, Setmode 20h). */
static uint8_t st1, ss1, ff1;
static void takeSector() { st1 = resp(); drain(); want(true); data(); ss1 = data(); ff1 = data(); want(false); ackAll(); }
/* Any other interrupt, shown whole. */
static void showInt(uint8_t f) { out(" <"); line[lp++] = '0' + f; out(">"); for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); } ackAll(); }

/* A command; INT1s that come before its first response are counted (and the last one's SS:FF kept); returns the
 * response's interrupt (0: none in 2 s). */
static uint8_t cmd(const char* name, uint8_t c, int* sectors) {
    out(" "); out(name); command(c);
    for (int n = 0; n < 2000; n++) {
        const uint8_t f = waitAny();
        if (f == 1) { takeSector(); if (sectors) (*sectors)++; continue; }
        if (f == 0) { out(" (none)"); return 0; }
        showInt(f);
        return f;
    }
    out(" (sectors only)");
    return 1;
}
/* A Pause: its INT3 and INT2, sectors before them skipped. */
static void pause() {
    out(" Pause"); command(0x09);
    for (int n = 0; n < 2000; n++) {
        const uint8_t f = waitAny();
        if (f == 1) { takeSector(); continue; }
        if (f == 0) { out(" (none)"); return; }
        showInt(f);
        if (f != 3) return;
    }
}
/* The head parked at m:s:f - Setloc, ReadN, two sectors, Pause (as cd-cmds' S2 parks it). */
static void park(uint8_t m, uint8_t s, uint8_t f) {
    param(m); param(s); param(f); command(0x02); waitAny(); drain(); ackAll();
    command(0x06); waitAny(); drain(); ackAll();
    for (int k = 0; k < 2;) { const uint8_t x = waitAny(); if (x == 1) { takeSector(); k++; } else if (x == 0) break; else { drain(); ackAll(); } }
    command(0x09);
    for (int n = 0; n < 2000; n++) { const uint8_t x = waitAny(); if (x == 1) { takeSector(); continue; } drain(); ackAll(); if (x != 3) break; }
}
/* One row: park, Test04, wait, Test05 (its INT1s counted), Pause. */
static void row(const char* tag, uint8_t m, uint8_t s, uint8_t f, uint32_t waitC, const char* waitName) {
    park(m, s, f);
    out(tag); out(" "); outHex(m); out(":"); outHex(s); out(":"); outHex(f); out(" "); out(waitName);
    param(0x04); cmd("Test04", 0x19, nullptr);
    waitCycles(waitC);
    int n = 0;
    param(0x05); cmd("Test05", 0x19, &n);
    if (n) { out(" +"); outDec(n); out("x<1>"); outHex(st1); out(" to "); outHex(ss1); out(":"); outHex(ff1); }
    pause();
    flush();
}

int main() {
    printf("\ncdrom/cd-scex\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    const uint32_t S = 33868800;
    /* P: 2 s at each position, from the pregap to 00:40:00 */
    row("P", 0x00, 0x00, 0x10, 2 * S, "2s");
    row("P", 0x00, 0x02, 0x00, 2 * S, "2s");
    row("P", 0x00, 0x03, 0x00, 2 * S, "2s");
    row("P", 0x00, 0x04, 0x00, 2 * S, "2s");
    row("P", 0x00, 0x06, 0x00, 2 * S, "2s");
    row("P", 0x00, 0x10, 0x00, 2 * S, "2s");
    row("P", 0x00, 0x30, 0x00, 2 * S, "2s");
    row("P", 0x01, 0x00, 0x00, 2 * S, "2s");
    row("P", 0x05, 0x00, 0x00, 2 * S, "2s");
    row("P", 0x20, 0x00, 0x00, 2 * S, "2s");
    row("P", 0x40, 0x00, 0x00, 2 * S, "2s");
    /* T: the wait at 00:02:00, for the strings' spacing */
    row("T", 0x00, 0x02, 0x00, S / 2, "0.5s");
    row("T", 0x00, 0x02, 0x00, S, "1s");
    row("T", 0x00, 0x02, 0x00, 2 * S, "2s");
    row("T", 0x00, 0x02, 0x00, 4 * S, "4s");
    row("T", 0x00, 0x02, 0x00, 8 * S, "8s");

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
