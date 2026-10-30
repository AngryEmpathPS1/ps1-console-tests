/*
 * cdrom/cd-scex-play - Test 19h,04h (read SCEx) against a Play, the cases cd-gameday did not ask (2026-10-09).
 *
 * WHY. cd-gameday (NFL GameDay 2001's own check, on its own disc) sends 19h,04h 57,000 cycles after a Play - inside the
 * Play's seek - and the four consoles part by board: the one-chip CXD2938Q boards (SCPH-7501, 9001) END the Play, stat
 * 02h, GetlocP held at one position for 113.5M cycles; the two-chip boards (SCPH-1001, 5501) play on, the seek bit never
 * shown. v0.377.66 models that. Three things it did not ask, which the drive programs on this core answer differently:
 *   L  19h,04h while the Play is PLAYING (15M cycles in, past the seek on every console): SC430940 (the 7501's program)
 *      keeps stat 82h and holds the head - GetlocP frozen - and the Play resumes after 19h,05h; SC430930 (the 5501's)
 *      plays on; the model plays on on both.
 *   H  19h,04h in the Play's seek, then 19h,05h: does 19h,05h bring the Play back? cd-gameday's Pause 2M cycles after
 *      19h,05h found it playing on the 7501's first row of each run, ended on its other rows and every 9001 row; the
 *      7501's program resumes it.
 *   Q  19h,04h in the seek, then Setmode 01h (a command that moves nothing) 12M cycles on: does any command release the
 *      held GetlocP? The model releases it at every command but Getstat and GetlocP.
 *   I  19h,04h with the drive PAUSED at 22:14 in mode 00h after a SeekP; J the same after a ReadN (cd-cmds S2's order at
 *      22:14): cd-cmds S2 found every console reading on (INT1s at 22h) after 19h,04h paused at 00:02:20. SC430930 PLAYS
 *      from the paused position in I. INT1s are counted, not acknowledged one by one.
 * And each row's Pause, with its INT2 timed: cd-gameday's Pauses after an ended Play took 1.3-7.9M cycles on the
 * one-chip consoles, where the model's idle Pause answers in ~9,000.
 *
 * WHAT IT DOES. Interrupts off, the CD polled, Timer 2 at sysclock/8 unwrapped; NFL GameDay 2001's disc (22:14:00 is in
 * its data track; any disc whose 22:14:00 is data will do). Each row: Setloc 22:14:00, Setmode, SeekP, 20M cycles,
 * Mute, Play (I: Pause instead of Play), then 19h,04h; Getstat and GetlocP every 4M cycles ([time stat track|index|
 * relative|absolute]); 19h,05h; probes again; Pause; Demute. Times are thousands of cycles from 19h,04h's write.
 *
 * READ IT LIKE THIS. A position that stays the same over probes is a held head; s82 with a moving position is playing.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.66 (GameDay 2001's image), the model:
 *   as the 7501 (servo): L holds 82h at 22:14:28 and resumes after 19h,05h (42h at +4M, 82h after); H holds 02h at
 *   22:13:74 (cd-gameday's console position), then 02h, 42h, 82h by +9M; Q - Setmode keeps the hold; I holds at 22:13:70
 *   and plays after 19h,05h; J holds at 22:13:73 and reads after it (INT1s);
 *   as the 5501 (line): L plays on; H 82h from 4M (here a Play starts well under 1M after a SeekP, the consoles ~6.7M -
 *   the seek model's); Q plays on; I plays from 4M; J reads from 4M (INT1s).
 * AT v0.377.67 (a Play seeks as a read does - planSeek - and a held Play's restart hides its seek): as the 7501 H 02h at
 *   +1M and +5M after 19h,05h, 82h by +9M; as the 5501 H and Q 02h at 4M, 82h from 8M on 22:14:03 - SC430930's rows.
 * AT v0.377.68 (the held head tracks on; a Pause refused while a Play's seek shows): as the 7501 L holds 82h at
 *   22:14:18 and restarts AT the hold (02h at +4M, 82h on 22:14:23 at +8M); H 82h by +9M on 22:14:06; Q keeps the hold;
 *   I and J 02h at +1M, 42h at +5M, the Pause refused (INT5 43h 80h) and taken 10M on; as the 5501 every row within a
 *   sector of the 5501 console's.
 * THE CONSOLES (two runs each, console-logs/2026-10-09-SCPH-*-gameday): the 1001 and 5501 the line rows to the sector;
 *   the 7501 and 9001 the servo rows - L held at 22:14:18-24 and restarted ~27 sectors PAST the hold (82h on 22:14:45-51
 *   at +4M; KNOWN-OPEN, the model restarts at it); H 82h on 22:14:08 by +9M; I and J 42h already at +1M with the Q 37-49
 *   sectors past the hold (the head tracked on under the frozen Q; the subchannel in a seek is not modelled), the Pause
 *   refused and taken 10M on.
 * THE DRIVE PROGRAMS (--settle=600): SC430940 (the 7501's) - L holds 82h at 22:14:46 and resumes; H holds 02h at
 *   22:13:61, moving at +1M (02h), 82h by +5M; Q keeps the hold; I holds, then seeks (42h) and plays after 19h,05h, and
 *   a Pause then is refused (INT5 43h 80h) - here taken; J the same with a read. SC430930 (the 5501's) - L plays on;
 *   H 02h at 4M, then 82h; Q plays on; I plays from 4M; J reads from 4M.
 * `node hwtests/run.js cd-scex-play --disc=gameday --quiet=1500`
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

static uint32_t clk8; static uint16_t lastT;
static inline uint32_t now() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; return clk8 * 8; }
static void waitUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) { } }

static char line[240]; static int lp;
static void out(const char* s) { while (*s && lp < 230) line[lp++] = *s++; }
static void outHex(uint8_t v) { const char* h = "0123456789ABCDEF"; if (lp < 230) { line[lp++] = h[v >> 4]; line[lp++] = h[v & 15]; } }
static void outDec(int v) { char b[12]; int k = 0; if (v < 0) { out("-"); v = -v; } do { b[k++] = '0' + v % 10; v /= 10; } while (v); while (k && lp < 230) line[lp++] = b[--k]; }
static void flush() { line[lp] = 0; printf("%s\n", line); lp = 0; }

static uint32_t origin;
/* The next interrupt within `limit` cycles; 0 if none. *at is when it was seen. A half-set flag (psx-spx's "Unstable
 * IRQ Flag polling", the SCPH-1001's: 1 before 3, 4 before 5) is read again before it is taken. */
static uint8_t nextInt(uint32_t limit, uint32_t* at) {
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now(); uint8_t f = irqFlag();
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { *at = t; return f; }
        if (t - t0 > limit) { *at = t; return 0; }
    }
}
/* Take an interrupt and show it: <n> and every response byte, @ its time from origin in thousands of cycles. */
static uint8_t take(uint32_t limit = 70000000) {
    uint32_t at; const uint8_t f = nextInt(limit, &at);
    if (!f) { out(" (none)"); return 0; }
    out(" <"); line[lp++] = '0' + f; out(">");
    for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); }
    ackAll();
    out("@"); outDec((int)(at - origin) / 1000);
    return f;
}
/* A command and its first response; INT1s before it are counted and dropped (a drive reading on, row I). */
static int ones;
static uint8_t cmd(const char* name, uint8_t c) {
    out(" "); out(name); command(c);
    for (;;) {
        uint32_t at; const uint8_t f = nextInt(70000000, &at);
        if (f == 1) { for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); if (++ones < 100000) continue; }
        if (!f) { out(" (none)"); return 0; }
        out(" <"); line[lp++] = '0' + f; out(">"); for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); } ackAll();
        out("@"); outDec((int)(at - origin) / 1000);
        return f;
    }
}
/* A command's second response, INT1s counted as above. */
static void second() {
    for (;;) {
        uint32_t at; const uint8_t f = nextInt(70000000, &at);
        if (f == 1) { for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); if (++ones < 100000) continue; }
        if (!f) { out(" (none)"); return; }
        out(" <"); line[lp++] = '0' + f; out(">"); for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); } ackAll();
        out("@"); outDec((int)(at - origin) / 1000);
        return;
    }
}
/* Getstat and GetlocP, INT1s counted and dropped. */
static uint8_t answer(uint8_t c, uint8_t* b, int n) {
    command(c);
    for (;;) {
        uint32_t at; const uint8_t f = nextInt(70000000, &at);
        if (f == 1) { for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); if (++ones < 100000) continue; }
        int k = 0; while (hasResp() && k < 32) { const uint8_t v = resp(); if (k < n) b[k] = v; k++; }
        ackAll();
        return f;
    }
}
static void probe() {
    out(" ["); outDec((int)(now() - origin) / 1000); out("k");
    uint8_t s[1] = { 0 }, g[8] = { 0 };
    answer(0x01, s, 1); out(" s"); outHex(s[0]);
    const uint8_t f = answer(0x11, g, 8);
    if (f == 3) { out(" "); for (int i = 0; i < 8; i++) { outHex(g[i]); if (i == 1 || i == 4) out("|"); } }
    else { out(" <"); line[lp++] = '0' + f; out(">"); }
    out("]");
    if (lp > 180) { flush(); out("   "); }
}
/* Probes every 4M cycles from `from` (cycles from origin) for `count`. */
static void probes(uint32_t from, int count) {
    for (int k = 0; k < count; k++) { waitUntil(origin + from + (uint32_t)k * 4000000u); probe(); }
}
static void ready() {
    out("RDY");
    for (int i = 0; i < 400; i++) {
        uint8_t s[1] = { 0xFF }; const uint8_t f = answer(0x01, s, 1);
        if (f == 3 && s[0] == 0x02) { out(" stat 02 after "); outDec(i); out(" Getstats"); return; }
        if (i < 6 || (i % 40) == 0) { out(" <"); line[lp++] = '0' + f; out(">"); outHex(s[0]); }
        if (f == 3 && !(s[0] & 0x12) && i == 2) { out(" Init"); cmd("Init", 0x0A); second(); }
        waitUntil(now() + 846720);
    }
    out(" (never ready)");
}
/* Setloc 22:14:00, Setmode, then: PLAY - SeekP and its INT2, 20M cycles, Mute, Play (NFL GameDay 2001's order); PAUSED
 * - the same with Pause for Play; READ - ReadN, four INT1s, Pause. Nothing is printed after the Play: a console's
 * printf would move 19h,04h. */
enum { PLAY, PAUSED, READ };
static void setup(const char* tag, uint8_t mode, int kind) {
    out(tag); origin = now();
    param(0x22); param(0x14); param(0x00); cmd("Setloc", 0x02);
    param(mode); cmd("Setmode", 0x0E);
    if (kind == READ) {
        cmd("ReadN", 0x06);
        for (int k = 0; k < 4; k++) { uint32_t at; const uint8_t f = nextInt(70000000, &at); for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll();
            out(" <"); line[lp++] = f ? '0' + f : '-'; out(">"); if (f != 1) break; }
        cmd("Pause", 0x09); second(); flush(); out("   ");
        return;
    }
    cmd("SeekP", 0x16); second();
    flush(); out("   ");
    waitUntil(now() + 20000000);
    cmd("Mute", 0x0B);
    if (kind == PLAY) cmd("Play", 0x03);
    else { cmd("Pause", 0x09); second(); flush(); out("   "); waitUntil(now() + 4000000); }
}
static void test04() { origin = now(); ones = 0; param(0x04); cmd("Test04", 0x19); }   /* INT1s counted from here */
/* 57,000 cycles after the Play's write, its INT3 taken: NFL GameDay 2001's spacing (cd-gameday) */
static void test04InSeek() { waitUntil(now() + 20000); test04(); }
static void test05() { out(" |"); param(0x05); cmd("Test05", 0x19); }
/* Pause until one is taken (a drive seeking refuses it, INT5 80h), with its INT2 timed; Demute. */
static void pauseEnd() {
    flush(); out("   ");
    for (int k = 0; k < 4; k++) {
        if (cmd("Pause", 0x09) == 3) { second(); break; }
        waitUntil(now() + 10000000);
    }
    cmd("Demute", 0x0C);
    if (ones) { out(" INT1s "); outDec(ones); }
    ones = 0;
    flush();
}

int main() {
    printf("\ncdrom/cd-scex-play\n");
    printf("Test 19h,04h against a Play at 22:14:00: while playing (L), in its seek then 19h,05h (H), then Setmode (Q), paused (I).\n");
    printf("Times in thousands of cycles from the 19h,04h write. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    const uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;
    origin = now();
    ready(); flush();

    /* L: playing, 15M cycles in */
    setup("L ", 0x01, PLAY);
    waitUntil(now() + 15000000 - 20000);
    test04(); probes(4000000, 10);
    waitUntil(origin + 41000000); test05(); flush(); out("  "); probes(45000000, 5);
    pauseEnd();

    /* H: in the Play's seek, then 19h,05h */
    setup("H ", 0x01, PLAY);
    test04InSeek(); probes(4000000, 5);
    waitUntil(origin + 21000000); test05(); flush(); out("  "); probes(22000000, 5);
    pauseEnd();

    /* Q: in the seek, then Setmode 01h 12M cycles on */
    setup("Q ", 0x01, PLAY);
    test04InSeek(); probes(4000000, 3);
    waitUntil(origin + 13000000); param(0x01); cmd("Setmode", 0x0E); flush(); out("  "); probes(14000000, 3);
    waitUntil(origin + 26000000); test05(); probes(27000000, 2);
    pauseEnd();

    /* I: paused after a SeekP, mode 00h */
    setup("I ", 0x00, PAUSED);
    test04(); probes(4000000, 5);
    out(" INT1s so far "); outDec(ones);
    waitUntil(origin + 21000000); test05(); flush(); out("  "); probes(22000000, 2);
    pauseEnd();

    /* J: paused after a read, mode 00h - cd-cmds S2 at 22:14 */
    setup("J ", 0x00, READ);
    waitUntil(now() + 4000000);
    test04(); probes(4000000, 5);
    out(" INT1s so far "); outDec(ones);
    waitUntil(origin + 21000000); test05(); flush(); out("  "); probes(22000000, 2);
    pauseEnd();

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
