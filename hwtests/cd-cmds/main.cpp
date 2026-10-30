/*
 * cdrom/cd-cmds - two console logs of CD command sequences, replayed so this
 * core can be graded against them line by line (M200).
 *
 * R  rama's Mechacon bus sniffer on a console (MiSTer issue #169, 2022-08-30):
 *    Setloc 02:01:07 + ReadS, eight INT1s (stat 22), then MotorOn while the
 *    read runs:
 *        C:MotorOn <5> 23 20        the read carries on
 *        <1> 23  x8                 stat bit 0 stays set in every INT1
 *        C:GetStat <3> 22           until the next command clears it
 *
 * N  nocash's no$psx diag, @@disk_setloc_read (DIAG.A22), results recorded in
 *    its source; Setmode 20h, each step followed by the next two sectors' frame
 *    numbers:
 *        Setloc 00:02:11, ReadN             11,12
 *        ReadN (already reading: ignored)   13,14
 *        Setloc 00:03:01 (memorized)        15,16
 *        Pause, ReadN (the memorized one)   01,02
 *        Pause, ReadN (nothing memorized)   02,03   the last sector AGAIN
 *        Setloc 00:02:11, ReadN             11,12   not ignored: Setloc changed
 *        Setloc 00:02:44, 00:02:22, ReadN   22,23   the last Setloc wins
 *
 * S  (added 2026-09-28, M261) the SCEx counters, Test 19h,05h --> INT3(total,
 *    success): S1 before anything else (the boot's; psx-spx "typically 01h,01h
 *    for Licensed PSX Data CDs"), S2 after 19h,04h with the head in the data
 *    area and a 2 s wait (psx-spx: no strings away from the lead-in, 00h,00h;
 *    DuckStation answers 00h,00h always). NFL GameDay 2001's modchip check is
 *    S2's sequence from its audio track.
 *
 * THE RULE: run here first (hwtests/README.md). The M199 core answers MotorOn
 * with INT3 and INT2 and restarts a repeated ReadN; M200 is meant to read as
 * above.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js cd-cmds --disc`):
 *   as printed at M199: MotorOn <5> 23 20 in a read, the INT1s after it 23, GetStat <3> 22; the N rows
 *   land on frames 11,12 / 13,14 / 15,16 / 01,02 / 02,03 / 11,12 / 22,23 - the console line beside each.
 * AT v0.261 (the S rows): S1 Test05 <3> 01 01; S2 Test04 <3> 02, then Test05 <3> 00 00. Before M261 every
 *   Test05 answered the stat alone (S1 <3> 02, S2 <3> 02).
 * CONSOLE DAY (2026-10-05), the SCPH-5501 (controller 97/01/10 C2): S1 <3> 0C 0C, and S2's Test05 met a SECTOR
 *   first - <1>22@04:06, stat 22, 137 sectors past the Pause: Test04 had set the drive READING. The program ended
 *   there with the drive still reading, and cd-race, next, never had its Init answered. Since then S2 counts the
 *   INT1s after Test05 up to its INT3 and ends with a Pause. Here (v0.361): Test05 <3> 00 00 (none), then Pause
 *   <3> 02 <2> 02. On the drive's own program (SC430944, 99/02/01 C3): Test05 <3> 00 00 +1000x<1>22 to 15:44 - it
 *   reads on too, from the Pause's position after Test05 rather than after Test04 - then Pause <3> 22 <2> 02.
 *   This emulator's model does neither.
 * THE RUN (2026-10-05): S2's 1,000 INT1s are 13 s of silence on a C3 drive, past the runner's 400 quiet frames - on
 *   the SCPH-7501's program it stopped there and was graded as missing its last five lines. So:
 *   `node hwtests/run.js cd-cmds --disc --quiet=1500`
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

static char line[200]; static int lp;
static void out(const char* s) { while (*s && lp < 190) line[lp++] = *s++; }
static void outHex(uint8_t v) { const char* h = "0123456789ABCDEF"; line[lp++] = h[v >> 4]; line[lp++] = h[v & 15]; }
static void flush() { line[lp] = 0; printf("%s\n", line); lp = 0; }

static uint8_t waitAny() { uint32_t t0 = now(); for (;;) { uint32_t t = now(); uint8_t f = irqFlag(); if (f) return f; if (t - t0 > 70000000) return 0; } }

/* Take the next interrupt and describe it: INT1 as its stat and the header's
 * SS:FF (whole-sector mode, so the data FIFO starts with MM:SS:FF), anything
 * else as its INT number and every response byte. */
static uint8_t take() {
    uint8_t f = waitAny();
    if (f == 0) { out(" (none)"); return 0; }
    if (f == 1) {
        uint8_t st = resp(); drain();
        want(true); data(); uint8_t s = data(), fr = data(); want(false);
        ackAll();
        out(" <1>"); outHex(st); out("@"); outHex(s); out(":"); outHex(fr);
    } else {
        out(" <"); line[lp++] = '0' + f; out(">");
        for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); }
        ackAll();
    }
    return f;
}
/* A command and its first response. */
static void cmd(const char* name, uint8_t c) { out(" C:"); out(name); command(c); take(); }
static void setloc(uint8_t m, uint8_t s, uint8_t f) { param(m); param(s); param(f); cmd("Setloc", 0x02); }
/* Take INT1s until n have been seen (other interrupts are shown as they come). */
static void sectors(int n) { for (int k = 0; k < n;) { if (take() == 1) k++; else if (lp > 150) flush(); } }
/* A Pause: its first response and its second. */
static void pause() { cmd("Pause", 0x09); for (int i = 0; i < 6; i++) { uint8_t f = take(); if (f == 2 || f == 0) break; } }
static void outDec(int v) { char b[12]; int k = 0; do { b[k++] = '0' + v % 10; v /= 10; } while (v); while (k) line[lp++] = b[--k]; }
/* INT1s counted until another interrupt (shown whole), 2 s of nothing, or 1,000 of them (13 s at 1x). */
static void untilNot1() {
    int n = 0; uint8_t st = 0, s = 0, fr = 0;
    for (;;) {
        const uint8_t f = waitAny();
        if (f == 1) {
            st = resp(); drain(); want(true); data(); s = data(); fr = data(); want(false); ackAll();
            if (++n < 1000) continue;
        }
        if (n) { out(" +"); outDec(n); out("x<1>"); outHex(st); out(" to "); outHex(s); out(":"); outHex(fr); }
        if (f == 0) out(" (none)");
        else if (f != 1) { out(" <"); line[lp++] = '0' + f; out(">"); for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); } ackAll(); }
        return;
    }
}

int main() {
    printf("\ncdrom/cd-cmds\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;
    /* S1: the SCEx counters the boot left, before anything resets them (Init is not among psx-spx's resets) */
    out("S1"); param(0x05); cmd("Test05", 0x19); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h */

    /* R: rama's sequence */
    out("R"); setloc(0x02, 0x01, 0x07); cmd("ReadS", 0x1B); flush();
    out(" "); sectors(8); flush();
    out(" "); cmd("MotorOn", 0x07); sectors(8); flush();
    out(" "); cmd("GetStat", 0x01); flush();
    pause(); flush();
    printf("R  console: MotorOn <5> 23 20; the INT1s after it 23; GetStat <3> 22\n");

    /* N: nocash's sequence */
    out("N1"); setloc(0x00, 0x02, 0x11); cmd("ReadN", 0x06); sectors(2); flush();
    out("N2"); cmd("ReadN", 0x06); sectors(2); flush();
    out("N3"); setloc(0x00, 0x03, 0x01); sectors(2); flush();
    out("N4"); pause(); cmd("ReadN", 0x06); sectors(2); flush();
    out("N5"); pause(); cmd("ReadN", 0x06); sectors(2); flush();
    out("N6"); setloc(0x00, 0x02, 0x11); cmd("ReadN", 0x06); sectors(2); flush();
    out("N7"); setloc(0x00, 0x02, 0x44); setloc(0x00, 0x02, 0x22); cmd("ReadN", 0x06); sectors(2); flush();
    pause(); flush();
    printf("N  console (frames): 11,12 / 13,14 / 15,16 / 01,02 / 02,03 / 11,12 / 22,23\n");

    /* S2: 19h,04h with the head in the data area, 2 s ("wait 1-2 seconds before expecting any results"), 19h,05h */
    out("S2"); setloc(0x00, 0x02, 0x16); cmd("ReadN", 0x06); sectors(4); pause(); flush();
    out("  "); param(0x04); cmd("Test04", 0x19);
    { uint32_t t0 = now(); while (now() - t0 < 67737600) {} }
    param(0x05); cmd("Test05", 0x19);
    /* What comes after it (console day, 2026-10-05). On the SCPH-5501 Test05's first interrupt was a SECTOR's INT1 -
     * "C:Test05 <1>22@04:06", stat 22 (reading), the head two seconds past the Pause - so Test04 had set the drive
     * reading. This program ended there, the drive still reading, and cd-race, next, never had its Init answered
     * among the INT1s: no line for 13 minutes, the console deaf. The INT1s are counted now until Test05's INT3, and
     * a Pause leaves the drive paused, as the other rows leave it. */
    untilNot1(); flush();
    out("  "); pause(); flush();
    printf("S  psx-spx: S1 typically 01 01 (a licensed disc booted), S2 00 00 (no SCEx outside the lead-in)\n");

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
