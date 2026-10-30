/*
 * cdrom/cd-scex-hold - how far a held head goes: Test 19h,04h's hold on a one-chip board, timed (2026-10-10).
 *
 * WHY. cd-scex-play (all four consoles, two runs each) left two rows of the one-chip boards' (SCPH-7501, 9001) hold
 * unmodelled. L: a Play held 26M cycles by 19h,04h and restarted by 19h,05h is PLAYING 4M cycles later exactly 27 sectors
 * past its held position on all three console runs (22:14:22 -> 22:14:49, 22:14:18 -> 22:14:45, 22:14:24 -> 22:14:51),
 * where the model restarts at the hold. I and J: a paused drive held 17M cycles shows the Q 37-49 sectors past the held
 * one 1M cycles after 19h,05h, then seeks back (42h). One hold length each cannot say what the head does while held:
 * whether it runs on a fixed distance and stops, tracks on at the disc's speed the whole hold, or jumps back a track a
 * turn as a pause does. This asks with four hold lengths each, and probes soon after 19h,05h.
 *   L1-L4  a Play at 22:14:00 (SeekP, 20M cycles, Play), 19h,04h 15M cycles into it, held 2M / 8M / 20M / 41M cycles,
 *          19h,05h, then Getstat and GetlocP at +0.5M, +1M, +2M, +4M and +8M. A restart that plays at hold + k for any
 *          length is a fixed run-on; one that grows with the length is tracking.
 *   I1-I4  paused after a SeekP to 22:14:00, 19h,04h, held 2M / 8M / 20M / 41M cycles, 19h,05h, probes at +0.3M, +0.6M,
 *          +1M, +2M, +4M, +8M: where the Q is before the seek back says how far the head went in the hold.
 * Each row ends with a Pause, sent again 10M cycles on while one is refused (INT5 80h), and Demute.
 *
 * WHAT IT DOES. cd-scex-play's code (interrupts off, the CD polled, Timer 2 at sysclock/8 unwrapped, a half-set flag read
 * again before it is taken), mode 01h for L and 00h for I. Any disc whose 22:14:00 is in a data track: X-Files Disc 1 is
 * one data track to 57:57 (the console tests' disc), and NFL GameDay 2001 (cd-scex-play's) will do. Times in thousands of
 * cycles from 19h,04h's write; a probe is [time stat track index|relative|absolute].
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.70 as the SCPH-7501 (X-Files Disc 1): every L row holds 82h at
 * 22:14:18 and restarts AT it - 02h through +4M (its seek hidden), 82h on 22:14:20-24 at +8M, whatever the hold; every I
 * row's Q stands at 22:13:74 through +0.6M after 19h,05h (the model's Q in a seek is the hold's), then the seek back:
 * I1 (2M) plays 22:14:00 by +0.9M, I2 (8M) shows 42h at +2M and plays by +8M, I3 (20M) still 42h at +8M and its Pause
 * refused, I4 (41M) plays by +8M - the seek planned from where the head tracked to (scexQ + elapsed / P).
 * THE CONSOLE (the SCPH-7501, 2026-10-10, four runs alike to the sector, X-Files Disc 1): the held head tracks on at the
 *   disc's speed (5 / 19 / 45 / 92 sectors past the hold after 2M / 8M / 20M / 41M); L1 and L2 play on from the head
 *   (82h at +0.4M on 22:14:23 and 22:14:37), L3 and L4 read 82h on 22:14:63 / 22:15:35 at +0.4M, then 42h - the seek back
 *   - and 82h at +8M on 22:14:51 / 22:14:54; I1 plays from 22:14:00 at +0.9M, I2 from the head (22:14:12 at +0.3M), I3
 *   and I4 seek back (42h) and play 22:14:27 / 22:14:30 at +8M.
 * AT v0.377.71 (CD_SCEX_PLAY_REACH: on from the head within 28 sectors of the hold's Q, else back to there): the status
 *   sequence the console's on every row, the +8M positions within 1-2 sectors; GetlocP in the first ~2M reads the hold's
 *   Q (the subchannel in a seek, not modelled), and I1's first probe 82h (its paused head 2 sectors on from the console's).
 * `node hwtests/run.js cd-scex-hold --disc=xfiles --quiet=1500`
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

static void probeAt(uint32_t t) { waitUntil(origin + t); probe(); }

int main() {
    printf("\ncdrom/cd-scex-hold\n");
    printf("Test 19h,04h's hold timed: a Play held 2M-41M cycles (L1-L4), a paused drive held the same (I1-I4).\n");
    printf("Times in thousands of cycles from the 19h,04h write. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    const uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;
    origin = now();
    ready(); flush();

    static const uint32_t HOLD[4] = { 2000000u, 8000000u, 20000000u, 41000000u };
    static const char* LT[4] = { "L1 ", "L2 ", "L3 ", "L4 " };
    static const char* IT[4] = { "I1 ", "I2 ", "I3 ", "I4 " };
    for (int k = 0; k < 4; k++) {
        setup(LT[k], 0x01, PLAY);
        waitUntil(now() + 15000000 - 20000);
        test04(); probeAt(HOLD[k] - 500000u);
        waitUntil(origin + HOLD[k]); test05(); flush(); out("  ");
        probeAt(HOLD[k] + 500000u); probeAt(HOLD[k] + 1000000u); probeAt(HOLD[k] + 2000000u);
        probeAt(HOLD[k] + 4000000u); probeAt(HOLD[k] + 8000000u);
        pauseEnd();
    }
    for (int k = 0; k < 4; k++) {
        setup(IT[k], 0x00, PAUSED);
        test04(); probeAt(HOLD[k] - 500000u);
        waitUntil(origin + HOLD[k]); test05(); flush(); out("  ");
        probeAt(HOLD[k] + 300000u); probeAt(HOLD[k] + 600000u); probeAt(HOLD[k] + 1000000u);
        probeAt(HOLD[k] + 2000000u); probeAt(HOLD[k] + 4000000u); probeAt(HOLD[k] + 8000000u);
        pauseEnd();
    }

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
