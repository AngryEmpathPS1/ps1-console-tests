/*
 * cdrom/cd-gameday - NFL GameDay 2001's modchip check, replayed on its own disc (2026-10-09).
 *
 * WHY. NFL GameDay 2001 (USA) - and NCAA GameBreaker 2001, the same loader - checks for a modchip before its title:
 * tools/cdtrace.js on the model (scratch/gameday_trace2.json, f3065-f3320 of the deep schedule) has it
 *     GetTD 02, ReadTOC, GetID, Setloc 22:14:00 (track 2's start), Setmode 01h (CD-DA), SeekP,
 *     22.2M cycles later Mute, Play, and 57,000 cycles after the Play's write Test 19h,04h,
 *     then 113.5M cycles (200 frames) later Test 19h,05h - the SCEx counters - and Pause.
 * psx-spx: 19h,04h resets the counters and reads SCEx "from the current location (the SCEx is stored only in the
 * Lead-In area, so, if the drive head is elsewhere, it will usually not find any strings, unless a modchip is
 * permanently simulating SCEx strings)"; 19h,05h is 00h,00h then. This core (M261) answers 00 00 there and keeps
 * PLAYING. But console day's cd-cmds asked the same in the DATA area (S2, Setloc 00:02:16): the 7501 answered
 * 02 01 and the 5501 03 03, and on both 19h,04h set the drive READING (INT1s at stat 22h after it); the SCPH-9001's
 * own program ends a Play there and reads on with no INT1 (audio sectors carry no header). Nothing has asked a console
 * inside an AUDIO track, which is where the game asks - and what a console counts there is what the game was written
 * to accept.
 *
 * WHERE IT ASKS. 22:14:00 is in the DATA track: GameDay 2001's track 1 runs to 44:34 and its audio track 2 starts at
 * 44:36 (GetTD 02 answered 02 44 36 in the trace). The game plays its own data area as audio, muted, and asks there -
 * where console day's cd-cmds counted SCEx (Setloc 00:02:16: 02 01 on the 7501, 03 03 on the 5501).
 *
 * WHAT IT DOES. Interrupts off, the CD polled, Timer 2 at sysclock/8 unwrapped. Needs GameDay 2001's disc (its 22:14
 * is data; on another disc the rows ask wherever 22:14 lies). First the counters the disc's insertion left (S1), and
 * GetTD 02. Then:
 *   G  the game's sequence at 22:14:00, its own spacing; every interrupt in the 200 frames after 19h,04h shown with
 *      its time; then 19h,05h and Pause. Twice.
 *   P  the same, with Getstat and GetlocP every 4M cycles (~7 frames) after 19h,04h: what the drive does - plays on
 *      (stat 82h, the position moving), or reads (22h) - and where the head is.
 *   A  P inside the AUDIO track (track 2's start from GetTD): the question nobody has asked a console.
 *   C  the control: P at 22:14 without 19h,04h - the Play left alone. 19h,05h at its end.
 * Times are cycles from the 19h,04h write (C: from where it would have been), in thousands.
 *
 * READ IT LIKE THIS. G's 19h,05h line is the game's answer. P's rows say whether the Play survived 19h,04h. A count
 * above zero in track 2 is SCEx found where psx-spx says there is none.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.59 (GameDay 2001's image, under the runner): S1 01 01; GetTD 02
 * 02 44 36; G: Test04 <3> 82 and nothing in the 200 frames, Test05 <3> 00 00, Pause <3> 82 <2> 02 - both runs; P and C:
 * stat 82h at every probe, the position moving through 22:14 at the disc's pace with or without 19h,04h; A the same in
 * track 2 (index 00's countdown, then 01). The model plays on and counts nothing - psx-spx's account.
 * ON THE SCPH-7501's OWN PROGRAM (SC430940, the disc in from power-on: `PSX_CD_FIRMWARE=... run.js ... --settle=600`;
 * under hwrunner --emu the disc goes into a running program with no lid cycle and it never sees it): 19h,04h answers
 * <3> 82 and then the PLAY ENDS - every probe after it stat 02, the position frozen at 22:13:61 (A: 44:35:64, track 2's
 * pregap); the control plays on (s42, then 82 with the position moving); 19h,05h 00 00 everywhere. So the model and
 * the program part on whether a Play survives 19h,04h, and agree on the count; a console decides both.
 * THE CONSOLES (2026-10-09, all four, two runs each) part by board, and v0.377.66 models it (src/psx.js CDROM Test 04h):
 * the 7501 and 9001 hold the Play (02h, GetlocP 22:13:74 / 44:35:63), the 1001 and 5501 play on (02h at 4M, then 82h).
 * ON THIS CORE AT v0.377.66: as the 7501 Test04 <3> 02, P held at 22:13:74, A at 44:35:74, and the Pause after 19h,05h
 * <3> 82 - the Play resumed within 2M, where the consoles' Pause found 02h in 14 of 16 rows (a Play's start time after
 * a SeekP: here under 1M, there ~6.7M - the seek model's); as the 5501 Test04 <3> 02, P 82h from 4M (the consoles 02h
 * at 4M, 82h from 8M: that start time again), A 02h at 4M, then 82h; C 82h from 4M on both (the consoles 42h at 4M).
 * AT v0.377.67 (a Play seeks as a read does): every probe row the consoles' status for status - C 42h at 4M and 82h at 8M
 * on 22:14:04 (theirs 22:14:03), the 5501's P and A 02h then 82h (A 44:36:52 at 8M, theirs 44:36:53), the 7501's G Pause
 * after 19h,05h 02h, its INT2 7.3M cycles on (theirs 1.3-7.9M); GetlocP at 4M reads the hold's 22:13:74 where the
 * consoles' head is passing 22:13:69 (the subchannel while seeking, not modelled).
 * `node hwtests/run.js cd-gameday --disc=gameday --quiet=1500`
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
/* The next interrupt within `limit` cycles; 0 if none. *at is when it was seen. */
static uint8_t nextInt(uint32_t limit, uint32_t* at) {
    const uint32_t t0 = now();
    for (;;) { const uint32_t t = now(); const uint8_t f = irqFlag(); if (f) { *at = t; return f; } if (t - t0 > limit) { *at = t; return 0; } }
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
static void cmd(const char* name, uint8_t c) { out(" "); out(name); command(c); take(); }
static uint8_t quiet(uint8_t c) { command(c); uint32_t at; uint8_t f = nextInt(70000000, &at); uint8_t r = hasResp() ? resp() : 0; for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); return f ? r : 0xEE; }
/* Every interrupt until `until` (cycles from origin), shown; the count of INT1s alone if they are many. */
static void drainUntil(uint32_t until) {
    int ones = 0; uint8_t st1 = 0;
    for (;;) {
        const int32_t left = (int32_t)(origin + until - now());
        if (left <= 0) break;
        uint32_t at; const uint8_t f = nextInt((uint32_t)left, &at);
        if (!f) break;
        if (f == 1) { st1 = hasResp() ? resp() : 0; for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); if (ones++ < 3) { out(" <1>"); outHex(st1); out("@"); outDec((int)(at - origin) / 1000); } continue; }
        out(" <"); line[lp++] = '0' + f; out(">"); for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); } ackAll(); out("@"); outDec((int)(at - origin) / 1000);
    }
    if (ones > 3) { out(" +"); outDec(ones - 3); out("x<1>"); outHex(st1); }
}
/* After a disc change the drive reports the shell open (INT5 01 80) until a Getstat clears it, and it spins up and reads
 * the disc's lead-in before it answers 02h: Getstat every 25 ms until it does, 10 s at most (the drive's own program,
 * the disc put in after the runner started, answered every command INT5 01 80 without this). */
static void ready() {
    out("RDY");
    bool inited = false;
    for (int i = 0; i < 400; i++) {
        command(0x01); uint32_t a; const uint8_t f = nextInt(70000000, &a);
        const uint8_t st = hasResp() ? resp() : 0xFF; for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll();
        if (f == 3 && st == 0x02) { out(" stat 02 after "); outDec(i); out(" Getstats"); return; }
        if (i < 6 || (i % 40) == 0) { out(" <"); line[lp++] = '0' + f; out(">"); outHex(st); }
        /* The motor off with the lid shut (the drive's own program with the disc put in under it: stat 00 for 10 s):
         * Init spins it up - its INT3, then its INT2 within 5 s. */
        if (f == 3 && !(st & 0x12) && !inited && i >= 2) {
            inited = true; out(" Init");
            command(0x0A); take(); take(140000000);
        }
        waitUntil(now() + 846720);
    }
    out(" (never ready)");
}
static uint8_t bcd(int v) { return (uint8_t)((v / 10) * 16 + v % 10); }

static uint8_t t2m, t2s;
/* The game's set-up: Setloc to track 2's start, Setmode 01h, SeekP, its 22.2M cycles, Mute, Play. */
static void setup(const char* tag, uint8_t m, uint8_t sec) {
    out(tag);
    param(m); param(sec); param(0x00); cmd("Setloc", 0x02);
    param(0x01); cmd("Setmode01", 0x0E);
    cmd("SeekP", 0x16); take();                               /* its INT3 then INT2 */
    flush(); out("   ");
    waitUntil(now() + 22204182 - 2000000);                    /* the game's gap, from SeekP's write, less what SeekP took */
    cmd("Mute", 0x0B);
    cmd("Play", 0x03);
}
static void test04() {
    waitUntil(now() + 20000);                                 /* 57,000 cycles after the Play's write, with its INT3 taken */
    origin = now();
    param(0x04); cmd("Test04", 0x19);
}
static void test05AndPause() {
    out(" |"); param(0x05); cmd("Test05", 0x19); drainUntil((now() - origin) + 2000000);
    flush(); out("   "); cmd("Pause", 0x09); for (int i = 0; i < 4; i++) { uint8_t f = take(); if (f == 2 || f == 0) break; }
    cmd("Demute", 0x0C);
    flush();
}

static void probe() {
    out(" ["); outDec((int)(now() - origin) / 1000); out("k");
    command(0x01); uint32_t at; nextInt(70000000, &at); out(" s"); if (hasResp()) outHex(resp()); for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll();
    command(0x11); uint8_t f = nextInt(70000000, &at);
    if (f == 3) { out(" "); for (int i = 0; i < 8 && hasResp(); i++) { outHex(resp()); if (i == 1 || i == 4) out("|"); } }
    else { out(" <"); line[lp++] = '0' + f; out(">"); for (int dn = 0; dn < 32 && hasResp(); dn++) { out(" "); outHex(resp()); } }
    for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll();
    out("]");
    if (lp > 180) { flush(); out("   "); }
}

int main() {
    printf("\ncdrom/cd-gameday\n");
    printf("NFL GameDay 2001's modchip check on its own disc: Play at 22:14:00 (data), Test 19h,04h, 200 frames, 19h,05h.\n");
    printf("Times in thousands of cycles from the 19h,04h write. There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    const uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;
    origin = now();

    ready(); flush();
    out("S1"); param(0x05); cmd("Test05", 0x19); flush();      /* what the disc's insertion left */
    command(0x0A); { uint32_t a; nextInt(70000000, &a); for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); nextInt(140000000, &a); for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); }
    out("TD"); param(0x02); command(0x14);
    { uint32_t a; nextInt(70000000, &a); out(" <3>"); uint8_t b[3] = { 0, 0, 0 }; for (int i = 0; i < 3 && hasResp(); i++) { b[i] = resp(); out(" "); outHex(b[i]); } for (int dn = 0; dn < 32 && hasResp(); dn++) resp(); ackAll(); t2m = b[1]; t2s = b[2]; }
    flush();
    if (t2m == 0 && t2s == 0) { printf("no track 2 - this test needs a disc with an audio track 2\n\nDone.\n"); for (;;) {} }

    for (int r = 0; r < 2; r++) {
        setup("G ", 0x22, 0x14);
        test04();
        flush(); out("   200 frames:"); drainUntil(113514176);
        test05AndPause();
    }
    setup("P ", 0x22, 0x14);
    test04();
    flush(); out("  ");
    for (int k = 0; k < 28; k++) { waitUntil(origin + (uint32_t)(k + 1) * 4000000u); probe(); }
    waitUntil(origin + 113514176);
    test05AndPause();
    setup("A ", t2m, t2s);
    test04();
    flush(); out("  ");
    for (int k = 0; k < 28; k++) { waitUntil(origin + (uint32_t)(k + 1) * 4000000u); probe(); }
    waitUntil(origin + 113514176);
    test05AndPause();
    setup("C ", 0x22, 0x14);
    waitUntil(now() + 20000); origin = now();
    flush(); out("  ");
    for (int k = 0; k < 28; k++) { waitUntil(origin + (uint32_t)(k + 1) * 4000000u); probe(); }
    waitUntil(origin + 113514176);
    test05AndPause();

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
