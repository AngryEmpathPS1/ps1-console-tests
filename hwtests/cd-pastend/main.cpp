/*
 * cdrom/cd-pastend - what a seek that cannot land answers, and when: past the
 * end of the disc, and onto an audio track.
 *
 * WHY (STATUS M237, and "a seek past the end, on the drive's own program").
 * Two consoles disagree on the first byte of a failed seek past the end:
 * pcsx-redux nugget's SCPH-9001 reads INT5 06h,10h (seek error, motor on) on its
 * burned CD-R, after at least 0.5 s ("will sometimes fail in roughly 650ms");
 * ps1-tests' console (cdrom/getloc) reads 04h (seek error, motor off) at 74:30:00
 * on a disc whose lead-out still read at 74:00:03, and then GetlocL and GetlocP
 * fail with 04h. This emulator answers 04h,10h after 650 ms, pinned to getloc.
 * The SCPH-9001's own drive program (tools/cddrive.js) gives BOTH: 06h,10h where
 * what lies past the written area still reflects (a CD-R's blank groove, a
 * pressed disc's mirror band), 04h,10h where the head loses focus there (losing
 * focus stops the motor before the report). The disc decides, not the console.
 * A pressed game disc short of its edge - Final Fantasy VII Disc 1 ends at 70:39
 * - should then read 06h,10h, after ~5.5 s on the program model (its seek timeout,
 * 125 runs of an every-64-ticks job); the 9001's "roughly 650ms" says the real
 * drive gives up sooner, and this test times it.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at sysclock/8,
 * unwrapped in software (cd-pause's clock). The disc's length and track count
 * from GetTN / GetTD(0), then, each from an Init, SeekP 00:02:00 and Setloc:
 *   1. SeekL to the lead-out's start + 2:30 (past the written area: a pressed
 *      disc's lead-out is about 90 s) - the INT3, the second response's bytes and
 *      time; at once Nop, GetlocL, GetlocP (getloc's order); then Nop every 100 ms
 *      for 3 s, a line each time its stat changes (when the motor bit goes).
 *   2. SeekP there - the same (the 9001: 06h,10h after >= 0.6 s).
 *   3. SeekL to the lead-out's start + 0:30, + 1:30, + 2:30, + 3:30, + 4:30 -
 *      INT2 where the head found the lead-out's sectors, INT5 where it did not:
 *      how far past the end this disc reads, and where its reflection ends.
 *   4. On a disc with a second track (audio), SeekL 10 s into it - the 9001:
 *      06h,04h after "roughly 4s".
 * Every target is held at 74:00:00 or inside it (EDGE). Every second response
 * waits at most 20 s ("(none in 20 s)").
 *
 * WHY NOTHING FURTHER OUT (2026-10-02). Until then rows 1 and 2 sought 85:00:00,
 * as nugget's own suite does on its SCPH-9001. psx-spx (nocash, at 19h,15h):
 * nothing tells the controller that the sled has reached its outer end, so a
 * command that drives it there makes it "forcefully hit against the outer edge",
 * and done often enough that can wreck the mechanism; a Setloc/Seek with too
 * large a value (his example: minute 99h) does the same. On the servo's spiral (src/cdservo.js: 25 mm to the
 * first sector, 1.6 um a track, 1.27-1.3 m/s) 85:00 is over 62 mm out, past the
 * disc's 60 mm edge, so the sled would push against its stop until the program's
 * timeout - over five seconds, twice a run, on one console. 74:00:00 is where
 * every drive must reach to read a full disc. The question (the first byte, and
 * when it comes) is asked as well just past the lead-out. MEASURED on the drive's
 * own program (tools/cdpastendpredict.js, the old rows against the new): Final
 * Fantasy VII Disc 1 at 85:00:00 INT5 06h,10h after 5.434 s, at 73:09:00 (its
 * lead-out + 2:30) INT5 06h,10h after 5.489 s; The X-Files Disc 1 (the sheet's
 * disc) at 85:00:00 after 10.583 s, at 60:29:00 after 5.238 s - the same byte and
 * the same Nop, GetlocL and GetlocP after it, and on The X-Files five seconds
 * fewer of the sled driving outward. an AI's console notes (section 34, not included) raised it; psx-spx is the
 * source.
 *
 * THE RULE: run it here first. This core answers every case past the end with
 * INT5 04h,10h after 650 ms and fails GetlocL/GetlocP with 04h until Init; onto
 * audio, 06h,04h after the seek and 256 sectors of looking.
 *
 * ON THIS CORE AT v0.280 (2026-09-30, `node hwtests/run.js cd-pastend --disc
 * --frames=12000 --quiet=4000`; --disc=<Ridge Racer> for the audio row): SeekL and SeekP
 * 85:00:00 INT3 02, then INT5 04 10 after 0.650 s; Nop 04, GetlocL and GetlocP
 * INT5 04 04, the stat 04 for good; every lead-out+N:30 row the same (this core
 * has no lead-out past 2 s); audio track 2 +10 s INT5 06 04 after 3.515 s.
 * AT v0.283 (M283: the disc's lead-out is written and reads, Disc.writtenEnd):
 * lead-out+0:30 and +1:30 INT2 02 after ~1.21 s, +2:30 on INT5 04 10 after 0.650
 * s, as the program reads them; the 85:00:00 rows as before.
 * THE DRIVE'S OWN PROGRAM on Final Fantasy VII Disc 1, with rows 1-2 at 85:00:00 then (tools/cdpastendpredict.js,
 * any disc image: a mirror past the written area, its lead-out 90 s): SeekL and SeekP 85:00:00 INT5
 * 06 10 after ~5.43 s; Nop at once 02, GetlocL and GetlocP INT5 03 80, the Nop
 * 00 within 0.1 s (the motor stopping); lead-out+0:30 and +1:30 INT2 02 (read in
 * the lead-out), +2:30 on INT5 06 10 after ~5.5 s. With the head losing focus
 * past the end instead: INT5 04 10 and the Nop 00. On Ridge Racer, audio track 2
 * +10 s: INT5 06 04 after 8.46 s.
 * AT v0.306 (2026-10-02, every row inside 74:00:00; `node hwtests/run.js
 * cd-pastend --disc --frames=12000 --quiet=4000` with --disc= each disc's image),
 * on the sheet's two discs. THE X-FILES DISC 1 (lead-out at 57:59): past the end (60:29:00) SeekL and
 * SeekP INT5 04 10 after 0.650 s, Nop 04, GetlocL and GetlocP INT5 04 04;
 * lead-out+0:30 and +1:30 INT2 02 after ~0.99 s; +2:30 on INT5 04 10 after 0.650 s.
 * MOTO RACER WORLD TOUR (23 tracks, lead-out at 62:56): past the end the same;
 * lead-out+0:30 and +1:30 INT5 06 04 after ~4.47 s (its last track is music: a
 * SeekL into the lead-out after it finds no data header here); +2:30 on INT5 04
 * 10; audio track 2 +10 s INT5 06 04 after 3.719 s.
 * The program on the same discs (tools/cdpastendpredict.js): The X-Files past the
 * end INT5 06 10 after ~5.24 s, Nop 02, GetlocL and GetlocP INT5 03 80, the lead-out
 * rows INT2 02 after ~1.4 s; Moto Racer past the end INT5 06 10 after ~5.34 s, its
 * lead-out+0:30 and +1:30 INT2 02 after ~1.62 s - where the model says 06 04 - and
 * audio track 2 +10 s INT5 06 04 after 8.53 s. The console decides both.
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
static uint8_t irqFlag() { idx(1); return *CD3 & 0x1F; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
static uint8_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

/* The unwrapped clock: Timer2 counts sysclock/8; call tick() often. */
static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }

/* The next interrupt (up to `limit` cycles): its number, 0 if none came, with its
 * response bytes in r[] (n of them), acknowledged. */
static uint8_t r[16];
static int n;
static uint8_t next(uint32_t limit) {
    uint32_t t0 = now();
    uint8_t f = 0;
    while (!(f = irqFlag() & 7)) if (now() - t0 > limit) return 0;
    n = 0;
    while (hasResp() && n < 16) r[n++] = resp();
    ackAll();
    return f;
}
static void show(const char *what, uint8_t f) {
    printf("%s", what);
    if (!f) { printf(" (none)\n"); return; }
    printf(" INT%d", f);
    for (int i = 0; i < n; i++) printf(" %02x", r[i]);
    printf("\n");
}
static const uint32_t SEC = 33868800;
static void simple(uint8_t cmd) { command(cmd); next(SEC); }
static void init() { command(0x0A); next(SEC); next(5 * SEC); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}

/* From an Init, SeekP 00:02:00, Setloc `lba`, then `cmd`: its INT3 and its second
 * response, timed from the INT3. With `follow`, getloc's queries and the stat's
 * changes over 3 s after it. */
static void attempt(const char *label, uint8_t cmd, uint32_t lba, bool follow) {
    init();
    param(0x00); simple(0x0E);
    setloc(0); command(0x16); next(SEC); next(5 * SEC);
    setloc(lba);
    uint32_t a = lba + 150;
    printf("%s %02d:%02d:%02d:", label, a / 4500, (a / 75) % 60, a % 75);
    command(cmd);
    uint8_t f = next(SEC);
    uint32_t t0 = now();
    printf(" INT%d %02x,", f, n ? r[0] : 0);
    f = next(20 * SEC);
    uint32_t dt = now() - t0;
    if (!f) { printf(" (none in 20 s)\n"); return; }
    printf(" then INT%d", f);
    for (int i = 0; i < n; i++) printf(" %02x", r[i]);
    printf(" after %d.%03d s\n", dt / SEC, (dt % SEC) / (SEC / 1000));
    if (!follow) return;
    command(0x01); show("  Nop at once:", next(SEC));
    command(0x10); show("  GetlocL:", next(SEC));
    command(0x11); show("  GetlocP:", next(SEC));
    uint8_t last = 0xFF;
    uint32_t s0 = now();
    while (now() - s0 < 3 * SEC) {
        uint32_t w = now();
        while (now() - w < SEC / 10) { }
        command(0x01);
        uint8_t g = next(SEC);
        if (g == 3 && n && r[0] != last) {
            uint32_t e = now() - s0;
            printf("  Nop at %d.%03d s: %02x\n", e / SEC, (e % SEC) / (SEC / 1000), r[0]);
            last = r[0];
        }
    }
}

int main() {
    printf("\ncdrom/cd-pastend\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    init();
    simple(0x0C);                                   /* Demute */

    command(0x13); next(SEC);                       /* GetTN */
    uint8_t lastTrack = n >= 3 ? unbcd(r[2]) : 1;
    param(0); command(0x14); next(SEC);             /* GetTD(0): the lead-out */
    uint32_t total = n >= 3 ? (unbcd(r[1]) * 60 + unbcd(r[2])) * 75 : 0;
    uint32_t t2 = 0;
    if (lastTrack >= 2) { param(bcd(2)); command(0x14); next(SEC); t2 = n >= 3 ? (unbcd(r[1]) * 60 + unbcd(r[2])) * 75 : 0; }
    printf("disc: %d tracks, lead-out at %d:%02d (%d sectors)\n", lastTrack, total / 4500, (total / 75) % 60, total);

    /* Nothing past 74:00:00: see the header (psx-spx's outer edge). */
    const uint32_t EDGE = 74 * 4500 - 150;
    uint32_t past = total - 150 + 2 * 4500 + 2250;  /* the lead-out + 2:30 */
    if (past > EDGE) past = EDGE;
    if (past < total - 150) printf("this disc's lead-out is past 74:00:00: every row lands inside its data, not past its end\n");
    attempt("SeekL past the end", 0x15, past, true);
    attempt("SeekP past the end", 0x16, past, true);
    uint32_t prevT = 0;
    for (int m = 0; m < 5; m++) {
        char label[40];
        uint32_t t = total - 150 + m * 4500 + 2250;
        if (t > EDGE) t = EDGE;
        if (m && t == prevT) { printf("SeekL lead-out+%d:30: held at 74:00:00, as the row before\n", m); continue; }
        prevT = t;
        sprintf(label, "SeekL lead-out+%d:30", m);
        attempt(label, 0x15, t, false);
    }
    if (lastTrack >= 2 && t2) attempt("SeekL audio track 2 +10 s", 0x15, t2 - 150 + 750, false);
    else printf("no second track: the onto-audio case needs a disc with one\n");

    init();
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\n06h,10h: what lies past the written area reflected. 04h,10h: the head\n");
    printf("lost its focus there (the drive's own program, tools/cddrive.js).\n");
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
