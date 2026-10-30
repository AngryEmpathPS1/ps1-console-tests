/*
 * cdrom/cd-boot-steps - the drive's steps in a game's boot, and the holds a seek leaves (2026-10-07, for the CD
 * rebuild's pieces 6 and 7)
 *
 * WHY.
 *   BIOS READTOC. The BIOS sends ReadTOC before LoadExec to a drive dated 95/07/06 or later (the SCPH-5501, 7501 and
 *     9001), right after reading the game's program at double speed and pausing: on SC430940 (the 7501's own drive
 *     program) four frames after the Pause's INT2. hwtests/cd-readtoc times ReadTOC from a single-speed idle head; this
 *     emulator times the double-speed case as the spin-down (0.7 s) and THEN the read, which fits the 7501's boot
 *     video (W1 -> P1 7.86 s here against 7.79 / 8.19 s) but was never measured alone. Here it is, beside the same at
 *     single speed.
 *   FIRST INIT. A game's first Init comes right after the BIOS: cd-init's FIRST row (25.4M-25.8M) is an Init after a
 *     BIOS that left its drive at double speed, from wherever the head was. The same under control.
 *   INIT WHILE CIRCLING AT DOUBLE SPEED. Init, Setmode 80h, a second, Init again. Both drive programs then declare home
 *     wherever their PLL locks again at single speed - sectors 27 to 33, the head having run ~38 sectors up the spiral
 *     with no frames to jump back on - which rests on their servo model's spin-down, an assumption. What a console's
 *     head does there, and how long it takes.
 *   HOLDS AFTER A SEEK. cd-head's IDLE rows show every console's hold after SeekL 00:02:16 circling TWO tracks
 *     (00:02:02..00:02:18, a single 00:02:10 between), where this emulator circles one; after SeekL 00:30:00 one track.
 *     Here at double speed, far out (52:00:00) at both speeds, and after SeekP.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at the system clock unwrapped in software. A pressed disc
 * of at least 53 minutes (The X-Files Disc 1). Nothing prints until each part is done.
 *   BIOS READTOC: Setloc 00:02:24, SeekL, Setmode 80h (00h in the single-speed rows), ReadN, 8 sectors, Pause and its
 *     INT2, four frames (2,264,000 cycles), ReadTOC: its INT3 from the write, its INT2 from the INT3, then GetlocP's
 *     absolute sector and Getparam's mode byte. Four times at double speed, twice at single.
 *   FIRST INIT: the same read and Pause, then Init instead. Four times.
 *   INIT WHILE CIRCLING AT 2X: Init to its INT2, half a second, Setmode 80h, a second, Init timed as above, then GetlocP
 *     every 20 ms for 1.2 s, as runs of climbing sectors. Three times.
 *   HOLDS: SeekL 00:02:16 at double speed; SeekL 52:00:00 at single and at double speed; SeekP 00:02:16 at single:
 *     GetlocP every 10 ms for 1.2 s after each seek's INT2, as runs.
 *
 * READ IT LIKE THIS. BIOS READTOC's double-speed rows less its single-speed ones is what a speed change costs a
 * ReadTOC; the mode byte says whether ReadTOC resets the speed (the programs answer 20h). FIRST INIT against cd-init's
 * FIRST row. In the holds, a run that climbs about nine sectors is one track; about seventeen, two.
 *
 * THE RULE: run it here first. ON THIS CORE AT v0.377.10 (The X-Files Disc 1, PSX_BIOS scph7001.bin, --ntsc --settle=600):
 *   BIOS READTOC   2x INT2 52.46M after the INT3 (the spin-down then the read), 1x 28.78M; mode 20h; GetlocP 00:01:73.
 *   FIRST INIT     26.49M-26.51M (cd-init's FIRST row on the consoles: 25.4M-25.8M).
 *   CIRCLING AT 2X 26.36M-26.37M, the head circling 00:01:66..00:01:74 after (the far path; KNOWN-OPEN in src).
 *   HOLDS          one track after SeekL 00:02:16 at 2x (00:02:10..18); 51:59:59..52:00:02 at both speeds; SeekP
 *                  00:02:16 00:02:07..15.
 * ON THE DRIVES' OWN PROGRAMS (SC430940 and SC430944, PSX_CD_FIRMWARE): BIOS READTOC 2x 28.4M-32.6M and an INT5 at 63.3M
 * on the fourth row on both, 1x 15.9M-20.0M (the programs read faster than any console's cd-readtoc); FIRST INIT
 * 15.6M-15.8M; CIRCLING AT 2X the INT2 112,000-122,000 after the INT3 (once 15.0M), GetlocP frozen at 00:01:72-73 for
 * ~360 ms, then a reading or two at 00:02:10-00:02:36 and the home circle again; HOLDS one track after SeekL (00:02:11..19,
 * 51:59:60..52:00:03), SeekP as here.
 * `node hwtests/run.js cd-boot-steps --disc --frames=9000 --quiet=4000` (--disc added 2026-10-09)
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
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint32_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next interrupt: which, up to 8 response bytes, when; drained and acknowledged. */
struct Got { uint8_t flag, n, b[8]; uint32_t t; };
static Got take(uint32_t limit = MS(3000)) {
    Got g; g.flag = 0; g.n = 0; g.t = 0;
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        /* An old board's flag can read 1 before 3, or 4 before 5 (psx-spx "Unstable IRQ Flag polling"; the SCPH-1001's
         * cd-irq-glitch: once in 3,000): such a first read is read again, or the INT3 is acknowledged as an INT1 and a
         * later wait for it never ends (cd-ack-times hung the 1001 so, 2026-10-09). The time is the first read's. */
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    while (hasResp() && g.n < 8) g.b[g.n++] = resp();
    drain();
    ackAll();
    return g;
}
static Got takeKind(uint8_t k, uint32_t limit = MS(3000)) {
    for (;;) { const Got g = take(limit); if (!g.flag || g.flag == k || g.flag == 5) return g; }
}
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); command(0x02); takeKind(3);
}
static void setmode(uint8_t m) { ackAll(); param(m); command(0x0E); takeKind(3); }
static Got seek(uint8_t cmd, uint32_t lba) { setloc(lba); ackAll(); command(cmd); takeKind(3); return takeKind(2, MS(8000)); }
static void pauseAll() { ackAll(); command(0x09); for (int i = 0; i < 24; i++) { const Got g = take(MS(2000)); if (!g.flag || g.flag == 2) break; } idle(MS(1)); ackAll(); drain(); }
static void initDrive() { ackAll(); command(0x0A); take(); takeKind(2, MS(8000)); }
static int lbaOf(uint8_t m, uint8_t s, uint8_t f) { return (int)((unbcd(m) * 60 + unbcd(s)) * 75 + unbcd(f)) - 150; }
#define NOPOS (-100000)                                         /* below it: no position, the INT number */
static int getlocP() {
    ackAll(); command(0x11);
    const Got g = take();
    return (g.flag == 3 && g.n >= 8) ? lbaOf(g.b[5], g.b[6], g.b[7]) : NOPOS - g.flag;
}
static uint8_t getparamMode() { ackAll(); command(0x0F); const Got g = take(); return g.flag == 3 && g.n >= 2 ? g.b[1] : 0xFF; }
static void msf(int lba) {
    if (lba <= NOPOS) { printf("INT%d", NOPOS - lba); return; }
    const int a = lba + 150;
    printf("%02d:%02d:%02d", a / 4500, (a / 75) % 60, a % 75);
}

/* A command's two responses, timed: INT3 from its write, INT2 from the INT3; then where the head is and the mode. */
struct Step { uint8_t f3, f2, mode; uint32_t t3, t2; int pos; };
static Step timed(uint8_t c) {
    Step s;
    ackAll(); const uint32_t t0 = now(); command(c);
    const Got g3 = take(MS(3000)); s.f3 = g3.flag; s.t3 = g3.t - t0;
    const Got g2 = takeKind(2, MS(12000)); s.f2 = g2.flag; s.t2 = g2.flag ? g2.t - g3.t : 0;
    s.pos = getlocP();
    s.mode = getparamMode();
    return s;
}
static void showStep(const char* what, const Step& s) {
    printf("  %-8s INT%d %9u   INT%d %10u after it   then GetlocP ", what, s.f3, (unsigned)s.t3, s.f2, (unsigned)s.t2);
    msf(s.pos); printf("  mode %02x\n", s.mode);
}
/* The BIOS's read before LoadExec: Setloc 00:02:24, SeekL, Setmode, ReadN, 8 sectors, Pause, four frames. */
static void biosRead(uint8_t mode) {
    seek(0x15, 24);
    setmode(mode);
    ackAll(); command(0x06); takeKind(3);
    for (int i = 0; i < 8; i++) takeKind(1);
    pauseAll();
    idle(2264000);
}

/* GetlocP every `period` cycles, `n` times; the absolute sector of each (or the INT number). */
#define NPOLL 120
static int pollPos[NPOLL]; static int pollN;
static void poll(uint32_t period, int n) {
    const uint32_t t0 = now();
    pollN = 0;
    while (pollN < n && pollN < NPOLL) {
        const uint32_t due = t0 + period * (uint32_t)pollN;
        while ((int32_t)(now() - due) < 0) {}
        pollPos[pollN++] = getlocP();
    }
}
static void showPoll(const char* name) {
    printf("  %s\n   ", name);
    int i = 0, col = 0;
    while (i < pollN) {
        int j = i + 1;
        while (j < pollN && pollPos[i] > NOPOS && pollPos[j] >= pollPos[j - 1] && pollPos[j] - pollPos[j - 1] <= 4) j++;
        printf(" "); msf(pollPos[i]);
        if (j - i > 1) { printf(".."); msf(pollPos[j - 1]); }
        printf("(%d)", j - i);
        if (++col % 5 == 0 && j < pollN) printf("\n   ");
        i = j;
    }
    printf("\n");
}

static Step rt[6], fi[4], cz[3];
static int czPos[3][60]; static int czN[3];

int main() {
    printf("\ncdrom/cd-boot-steps\n");
    printf("The drive's steps in a game's boot (ReadTOC and Init after a double-speed read), an Init while circling at double speed, and the holds a seek leaves.\n");
    printf("Needs a pressed disc of 53+ minutes. Cycles at the system clock (1 ms = 33,869). About a minute; nothing prints until each part is done.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    initDrive();
    setmode(0x00);

    for (int i = 0; i < 6; i++) { biosRead(i < 4 ? 0x80 : 0x00); rt[i] = timed(0x1E); }
    setmode(0x00); idle(MS(1000));
    printf("BIOS READTOC: Setloc 00:02:24, SeekL, Setmode, ReadN, 8 sectors, Pause, four frames, ReadTOC:\n");
    for (int i = 0; i < 6; i++) showStep(i < 4 ? "2x" : "1x", rt[i]);

    for (int i = 0; i < 4; i++) { biosRead(0x80); fi[i] = timed(0x0A); }
    setmode(0x00); idle(MS(1000));
    printf("FIRST INIT: the same double-speed read and Pause, then Init:\n");
    for (int i = 0; i < 4; i++) showStep("Init", fi[i]);

    for (int i = 0; i < 3; i++) {
        initDrive(); idle(MS(500)); setmode(0x80); idle(MS(1000));
        cz[i] = timed(0x0A);
        poll(MS(20), 60); czN[i] = pollN; for (int k = 0; k < pollN; k++) czPos[i][k] = pollPos[k];
    }
    printf("INIT WHILE CIRCLING AT 2X: Init, half a second, Setmode 80h, a second, Init; then GetlocP every 20 ms for 1.2 s:\n");
    for (int i = 0; i < 3; i++) {
        showStep("Init", cz[i]);
        pollN = czN[i]; for (int k = 0; k < pollN; k++) pollPos[k] = czPos[i][k];
        showPoll("after it");
    }

    printf("HOLDS: GetlocP every 10 ms for 1.2 s after each seek's INT2, as runs of climbing sectors (count):\n");
    setmode(0x80); idle(MS(1000)); seek(0x15, 16); poll(MS(10), 120); showPoll("after SeekL 00:02:16 at double speed");
    setmode(0x00); idle(MS(1000)); seek(0x15, 52 * 4500 - 150); poll(MS(10), 120); showPoll("after SeekL 52:00:00 at single speed");
    setmode(0x80); idle(MS(1000)); seek(0x15, 52 * 4500 - 150); poll(MS(10), 120); showPoll("after SeekL 52:00:00 at double speed");
    setmode(0x00); idle(MS(1000)); seek(0x16, 16); poll(MS(10), 120); showPoll("after SeekP 00:02:16 at single speed");
    setmode(0x00);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncdrom/cd-boot-steps: Done.\n");
    for (;;) VSync(0);
}
