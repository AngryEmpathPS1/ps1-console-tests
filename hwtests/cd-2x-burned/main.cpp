/*
 * cdrom/cd-2x-burned - what a drive does when a CD-R it reads only at single speed is asked for double, from the hub
 * to the edge (2026-10-06).
 *
 * WHY. All three consoles read the burned test discs at single speed only. The SCPH-9001, asked for double
 * speed on the games disc, answered every command 42h (seeking), refused every Pause (INT5 43h 80h), and its GetlocL
 * read real headers all the while - it found sectors and never settled on one; the SCPH-7501 gave a seek error; both
 * read every sector at single speed. Those discs are short (one and ten minutes), so all of it was near the hub, where a
 * disc turns fastest (constant linear speed: ~2.5 times the edge's). Whether double speed fails on a CD-R everywhere or
 * only there is the question, and a drive's handling of a disc it cannot hold is its program's own error handling -
 * which only the drive programs model here (CDROMLLE; the model always reads), and which games meet on worn discs.
 *
 * WHAT IT DOES. Needs THE DOUBLE-SPEED DISC (tools/cd2xdisc.js, burned: 72 minutes, every sector marked 'D' + its LBA);
 * the games disc works too, at the places it reaches. Unlocked first (psx-spx's secret unlock). Interrupts off, the
 * CD's flags polled; Timer 2 at sysclock/8 unwrapped in software. The disc's length from GetTD(0); then at every place
 * of 00:02:16, 02:00, 05:00, 09:00, 20:00, 30:00, 40:00, 50:00, 60:00 and 70:00 that lies 20 s inside it:
 *   1x      single speed, Setloc, ReadN: 20 sectors (the control), Pause.
 *   2x      double speed (2 s after the Setmode for the spin-up), Setloc, ReadN, 3 s with GetlocP every quarter second
 *           (where the drive says its head is), then Pause and its answers. A drive whose Pause is refused or never
 *           completes gets an Init (and 3 s for its INT2) before the next row: RECOVERY.
 * At the first, middle and last place also:
 *   SWITCH  reading at single speed, 10 sectors in, Setmode 80h written mid-read: 3 s with GetlocP polls, Pause.
 *   SEEK    at double speed, SeekL 300 sectors on and SeekP back: each to its second answer (INT2) or its error.
 * And at the end BACK: single speed at 00:02:16 again, 5 sectors - did the drive come back?
 * BEFORE ALL OF THAT, A SURVEY (2026-10-06):
 * double speed once, then a short read (5 sectors, 3 s at most) at 05:00, 60:00, 20:00, 70:00, 40:00 and 00:02:16 in
 * that order, each jump a long one. If it reads at double speed anywhere, the places run as above; if nowhere, they get
 * their single-speed control only - the answer is in, and more double-speed tries would only work the drive.
 * GIVING UP (2026-10-06): a place that reads nothing at
 * either speed gets no speed change or seeks, and two such places in a row end the places (STOPPED EARLY in the
 * printout). The longest any double-speed try lasts is 3 s, its Pause 2 s and an Init 3 s, so a drive that cannot read
 * the disc at all is done in about a minute.
 * Every interrupt is logged with its time (from the row's first write) and its response bytes; a run of sectors is one
 * line (how many, the first and last markers, and how many were not the next one); nothing prints until the row ends
 * (Unirom's TTY is 115,200 baud: ~2,940 cycles a character). A SUMMARY table ends the printout: per place, the 1x and
 * 2x sectors, the 2x read's first sector and its INT5s, and whether the Pause was refused.
 *
 * READ IT LIKE THIS. The 1x rows say the disc reads (20 sectors, each the next marker, a sector every 451,584 cycles).
 * The 2x rows are the finding: whether any sector comes, at which places, the stat bytes (02h motor, 20h reading, 40h
 * seeking, 04h seek error), the INT5s' bytes, what GetlocP answers (a position moving, stuck, or refused), how long the
 * drive tries before it gives up, and whether a Pause can stop it. The emulator's model reads everything at either speed;
 * the drive programs read a perfect image perfectly too - so here the rows are this core's, and only a console has the
 * error handling to show.
 *
 * THE RULE: run here first: `node hwtests/run.js cd-2x-burned --disc=2x --ntsc --frames=24000 --quiet=6000`.
 * HERE AT v0.369, the double-speed disc (lead-out 72:00, all ten places): 1x 20 sectors everywhere; 2x 434-448 sectors in
 * 3 s, the first 0.61M-3.81M cycles after the ReadN; no INT5, every Pause completed; the mid-read speed change ~0.44M
 * cycles to the next sector at every place (the read goes straight on); BACK 5 sectors.
 * ON THE SCPH-7501's PROGRAM (PSX_CD_FIRMWARE=SC430940, settled; a perfect image): 1x 20 everywhere, 2x 430-443 sectors,
 * the first 1.71M-4.79M after the ReadN (the slowest from 30:00 out); no INT5; every Pause completed. The mid-read speed
 * change stopped the reading at the middle place (40:00: 14.0M cycles, ~0.4 s, to the next sector), not at 00:02:16 or
 * 70:00 (0.44-0.45M) - and an earlier version of this test saw it stop at 00:02:16 instead (22.0M; STATUS): where the
 * program waits for the spindle is a draw. SeekL's second answer 2.13M-3.44M, SeekP's 5.25M-7.60M.
 * (The games disc runs it too: GetTD gives its lead-out at 10:02, and the four places inside it.)
 * THE SCPH-9001 (2026-10-06, the cd-2x disc burned at 10x on CMC Magnetics media; STATUS "The SCPH-9001's seven"): the
 * survey read NOTHING at double speed anywhere - a seek error after 2.1-2.5 s and the motor stopped (05:00, 00:02:16),
 * or the Pause refused and the Init failing (60:00, 20:00) - and single speed read 20 of 20 from 00:02:16 to 50:00, 12
 * at 60:00, 2 at 70:00. So the double-speed failure is the media at every radius, not the short discs. Not run
 * on the other consoles.
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
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t data() { idx(0); return *CD2; }
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint32_t unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

static uint32_t clk8;
static uint16_t lastT;
static inline uint32_t now() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; return clk8 * 8; }
#define MS(n) ((uint32_t)(n) * 33869u)

/* The log. kind 'c' a command written (c), 1-5 an interrupt; an INT1 entry is a RUN of sectors with one stat byte. */
struct Ev { uint32_t t, t2; uint8_t kind, c, n; uint8_t b[8]; uint32_t mark, mark2; uint16_t count, odd; };
#define NEV 56
static Ev evs[NEV];
static int nev, dropped;
static Ev *push() { if (nev < NEV) return &evs[nev++]; dropped++; return 0; }

static uint32_t t0;
static bool awaiting; static uint32_t awaitT; static uint8_t lastCmd;
static uint8_t lastResp[8], lastN, lastType;
/* This row's counts, for the summary. */
static int got, int5n; static uint8_t int5b[2]; static uint32_t first1; static uint8_t lastStat;

static void cmdWrite(uint8_t c, const uint8_t *p = 0, int np = 0) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t t = now();
    command(c);
    Ev *e = push(); if (e) { e->t = t - t0; e->kind = 'c'; e->c = c; }
    awaiting = true; awaitT = t; lastCmd = c;
}
static void takeOne(uint8_t f) {
    const uint32_t t = now();
    uint8_t b[8]; uint8_t n = 0;
    while (hasResp() && n < 8) b[n++] = resp();
    for (int dn = 0; dn < 32 && hasResp(); dn++) resp();
    uint32_t mark = 0;
    if (f == 1) {
        want(true);
        const uint32_t tw = now(); while (!(*CD0 & 0x40) && now() - tw < MS(1)) {}
        const uint8_t m0 = data(), m1 = data(), m2 = data(), m3 = data();
        mark = m0 | (m1 << 8) | (m2 << 16) | ((uint32_t)m3 << 24);
        want(false);
        if (!got) first1 = t - t0;
        got++;
    }
    ackAll();
    if (n) lastStat = b[0];
    if (f == 5) { if (!int5n) { int5b[0] = n ? b[0] : 0; int5b[1] = n > 1 ? b[1] : 0; } int5n++; }
    if (f == 1 && nev && evs[nev - 1].kind == 1 && evs[nev - 1].b[0] == (n ? b[0] : 0)) {
        Ev &r = evs[nev - 1];
        const uint32_t expect = ((r.mark2 >> 8) + 1) & 0xFFFFFF;
        if ((mark >> 8) != expect || ((mark & 0xFF) != 'D' && (mark & 0xFF) != 'A')) r.odd++;
        r.t2 = t - t0; r.mark2 = mark; r.count++;
        return;
    }
    Ev *e = push();
    if (e) {
        e->t = e->t2 = t - t0; e->kind = f; e->n = n; for (int i = 0; i < n; i++) e->b[i] = b[i];
        e->mark = e->mark2 = mark; e->count = 1; e->odd = (f == 1 && (mark & 0xFF) != 'D' && (mark & 0xFF) != 'A') ? 1 : 0;
    }
    if (f == 3 || f == 5) { awaiting = false; lastType = f; lastN = n; for (int i = 0; i < n; i++) lastResp[i] = b[i]; }
}
/* Takes interrupts until `until`, or `sectors` sectors, or (stopOn) an INT2/INT5 after the last command; with `pollLoc` a
 * GetlocP every quarter second while no command waits. Returns the type that stopped it (0: the time ran out). */
static uint8_t pump(uint32_t until, bool pollLoc, int sectors = 0, bool stopOn = false) {
    uint32_t lastPoll = now();
    for (;;) {
        const uint32_t t = now();
        if ((int32_t)(t - until) >= 0) return 0;
        const uint8_t f = irqFlag();
        if (f) {
            takeOne(f);
            if (sectors && got >= sectors) return 1;
            if (stopOn && (f == 2 || (f == 5 && lastCmd != 0x11))) return f;
            continue;
        }
        if (awaiting && t - awaitT > MS(500)) awaiting = false;
        if (pollLoc && !awaiting && t - lastPoll > MS(250)) { cmdWrite(0x11); lastPoll = t; }
    }
}
/* A command and its first answer (or 0.5 s), logged; returns the answer's type (0: none). */
static uint8_t cmd(uint8_t c, const uint8_t *p = 0, int np = 0) {
    cmdWrite(c, p, np);
    const uint32_t end = now() + MS(500);
    while (awaiting && (int32_t)(now() - end) < 0) { const uint8_t f = irqFlag(); if (f) takeOne(f); }
    return awaiting ? 0 : lastType;
}
static void setloc(uint32_t lba) { const uint32_t a = lba + 150; const uint8_t p[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) }; cmd(0x02, p, 3); }
static void setmode(uint8_t m) { cmd(0x0E, &m, 1); }
/* Pause and its second answer; true if it completed (INT3 then INT2). */
static bool pause() { const uint8_t a = cmd(0x09); return a == 3 && pump(now() + MS(2000), false, 0, true) == 2; }

static const char *NAME[0x20] = { 0, "Getstat", "Setloc", "Play", 0, 0, "ReadN", 0, "Stop", "Pause", "Init", 0, "Demute",
                                  "Setfilter", "Setmode", 0, "GetlocL", "GetlocP", 0, 0, "GetTD", "SeekL", "SeekP", 0, 0, 0, 0, "ReadS" };
static void printLog(const char *name) {
    printf("  %s:\n", name);
    for (int k = 0; k < nev; k++) {
        const Ev &e = evs[k];
        if (e.kind == 'c') { printf("    +%9u %s\n", (unsigned)e.t, NAME[e.c & 0x1F] ? NAME[e.c & 0x1F] : "?"); continue; }
        if (e.kind == 1) {
            printf("    +%9u INT1 %02x", (unsigned)e.t, e.n ? e.b[0] : 0);
            if (e.count > 1) printf(" x%d to +%u", e.count, (unsigned)e.t2);
            printf(", %c%u", (char)(e.mark & 0xFF), (unsigned)(e.mark >> 8));
            if (e.count > 1) printf(" .. %c%u", (char)(e.mark2 & 0xFF), (unsigned)(e.mark2 >> 8));
            if (e.odd) printf(" (%d not the next)", e.odd);
            printf("\n");
            continue;
        }
        printf("    +%9u INT%d", (unsigned)e.t, e.kind);
        for (int i = 0; i < e.n; i++) printf(" %02x", e.b[i]);
        printf("\n");
    }
    if (dropped) printf("    (%d more events not kept)\n", dropped);
    nev = 0; dropped = 0;
}
static void row() { nev = 0; dropped = 0; got = 0; int5n = 0; int5b[0] = int5b[1] = 0; first1 = 0; lastStat = 0; t0 = now(); }
/* Prints a row's log with interrupts on (the drive is paused or idle by then). */
static void show(const char *name) { ExitCriticalSection(); printLog(name); EnterCriticalSection(); }

/* The secret unlock (psx-spx), as cd-xa-switch. */
static void unlock() {
    const uint8_t t22[1] = { 0x22 };
    row(); cmd(0x19, t22, 1);
    char reg[12] = { 0 };
    for (int i = 0; i < lastN && i < 11; i++) reg[i] = (char)lastResp[i];
    const char *r = (reg[4] == 'U') ? "of America" : (reg[4] == 'E') ? "(Europe)" : 0;
    printf("drive region: %s - %s\n", reg, r ? "unlocking" : "no unlock for it");
    if (!r) return;
    static const char *const S[7] = { 0, "Licensed by", "Sony", "Computer", "Entertainment", 0, 0 };
    for (int k = 0; k < 7; k++) {
        const char *s = (k == 5) ? r : S[k];
        uint8_t p[16]; int n = 0;
        if (s) while (s[n] && n < 16) { p[n] = (uint8_t)s[n]; n++; }
        cmd(0x50 + k, p, n);
    }
    nev = 0;
}

#define NPLACE 10
static const uint32_t PLACE[NPLACE] = { 16, 8850, 22350, 40350, 89850, 134850, 179850, 224850, 269850, 314850 };
static const char *const PLACENAME[NPLACE] = { "00:02:16", "02:00:00", "05:00:00", "09:00:00", "20:00:00", "30:00:00",
                                               "40:00:00", "50:00:00", "60:00:00", "70:00:00" };
struct Sum { bool ran; int n1, n2, int5; uint8_t e[2], stat2; uint32_t first2; bool pauseOk, recovered; };
static Sum sums[NPLACE];
/* THE SURVEY'S ORDER (2026-10-06): 05:00, 60:00,
 * 20:00, 70:00, 40:00, 00:02:16 - every jump a long one, outward and back. */
#define NSURVEY 6
static const int SURVEY[NSURVEY] = { 2, 8, 4, 9, 6, 0 };
static Sum survey[NSURVEY];

/* The 2x row's Pause refused or never completed: an Init, and 3 s for its INT2, before anything else. */
static bool recover() {
    row(); cmd(0x0A);
    const uint8_t f = pump(now() + MS(3000), false, 0, true);
    show("RECOVERY (Init after a Pause that did not complete)");
    return f == 2;
}

int main() {
    printf("\ncdrom/cd-2x-burned\n");
    printf("A CD-R read at single and double speed from the hub to the edge, a speed change mid-read, seeks at double speed.\n");
    printf("Needs the double-speed disc (or the games disc). Cycles at the system clock (1 ms = 33,869). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                                     /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                      /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    unlock();
    row(); cmd(0x0A); pump(now() + MS(2000), false, 0, true);   /* Init and its INT2 */
    cmd(0x0C);                                            /* Demute */
    /* The disc's length: GetTD(0), the lead-out's minute and second. */
    row(); { const uint8_t p0[1] = { 0x00 }; cmd(0x14, p0, 1); }
    const uint32_t leadout = (lastType == 3 && lastN >= 3) ? (unbcd(lastResp[1]) * 60 + unbcd(lastResp[2])) * 75 - 150 : 0;
    /* The disc check: 00:02:16's marker at single speed. */
    row(); setmode(0x00); setloc(16); cmd(0x06); pump(now() + MS(3000), false, 1); pause();
    bool marked = false;
    for (int k = 0; k < nev; k++) if (evs[k].kind == 1 && (evs[k].mark & 0xFF) == 'D' && (evs[k].mark >> 8) == 16) marked = true;
    printf("disc: lead-out at LBA %u (%u:%02u), 00:02:16 %s\n", (unsigned)leadout, (unsigned)((leadout + 150) / 4500),
           (unsigned)(((leadout + 150) / 75) % 60), marked ? "marked D16" : "NOT marked");
    if (!marked) {
        printf("THIS IS NOT THE DOUBLE-SPEED DISC (or the games disc), or the drive refused it - the check read:\n");
        show("disc check");
    } else {
        nev = 0;
        int use[NPLACE], nuse = 0;
        for (int p = 0; p < NPLACE; p++) if (PLACE[p] + 20 * 75 < leadout) use[nuse++] = p;
        printf("%s: %d places, 00:02:16 to %s\n", leadout > 60 * 4500 ? "the double-speed disc" : "a shorter marked disc", nuse,
               PLACENAME[use[nuse - 1]]);
        /* THE SURVEY: double speed once (2 s to spin up), then at each place Setloc, ReadN, up to 3 s for 5 sectors, Pause
         * - and an Init, and double speed again, if the Pause does not complete. */
        bool any2x = false;
        row(); setmode(0x80); pump(now() + MS(2000), false); nev = 0;
        for (int k = 0; k < NSURVEY; k++) {
            const int p = SURVEY[k];
            if (!(PLACE[p] + 20 * 75 < leadout)) continue;
            Sum &V = survey[k];
            V.ran = true;
            ExitCriticalSection(); printf("\nSURVEY %d at %s (LBA %u), double speed:\n", k + 1, PLACENAME[p], (unsigned)PLACE[p]); EnterCriticalSection();
            row(); setloc(PLACE[p]);
            const uint32_t tR = now() - t0;
            cmd(0x06); pump(now() + MS(3000), false, 5);
            V.n2 = got; V.first2 = got ? first1 - tR : 0; V.int5 = int5n; V.e[0] = int5b[0]; V.e[1] = int5b[1]; V.stat2 = lastStat;
            if (got) any2x = true;
            V.pauseOk = pause();
            show("2x");
            if (!V.pauseOk) { V.recovered = recover(); row(); setmode(0x80); pump(now() + MS(2000), false); nev = 0; }
        }
        ExitCriticalSection();
        printf("\n%s\n", any2x ? "THE SURVEY READ AT DOUBLE SPEED: every place gets its rows."
                                : "THE SURVEY READ NOTHING AT DOUBLE SPEED: the places get their single-speed control only.");
        EnterCriticalSection();
        /* GIVING UP (2026-10-06): two places in a row that
         * read nothing at either speed stop the places - the drive or the disc is not reading, and the rest would only
         * work it. */
        int dead = 0; int stoppedAt = -1;
        for (int u = 0; u < nuse; u++) {
            if (dead >= 2) { stoppedAt = use[u - 1]; break; }
            const int p = use[u];
            const uint32_t P = PLACE[p];
            Sum &S = sums[p];
            S.ran = true;
            ExitCriticalSection(); printf("\nAT %s (LBA %u):\n", PLACENAME[p], (unsigned)P); EnterCriticalSection();
            row(); setmode(0x00); pump(now() + MS(2000), false); setloc(P); cmd(0x06); pump(now() + MS(3000), false, 20); pause();
            S.n1 = got;
            show("1x");
            if (!any2x) { S.n2 = -1; S.pauseOk = true; dead = S.n1 ? 0 : dead + 1; continue; }
            row(); setmode(0x80); pump(now() + MS(2000), false); setloc(P);
            const uint32_t tR = now() - t0;               /* the ReadN's write, on the row's clock */
            cmd(0x06); pump(now() + MS(3000), true);
            S.n2 = got; S.first2 = got ? first1 - tR : 0; S.int5 = int5n; S.e[0] = int5b[0]; S.e[1] = int5b[1]; S.stat2 = lastStat;
            S.pauseOk = pause();
            show("2x");
            if (!S.pauseOk) S.recovered = recover();
            dead = (S.n1 || S.n2) ? 0 : dead + 1;
            if (dead) continue;                           /* nothing read here: no speed change or seeks on top */
            if (u == 0 || u == nuse / 2 || u == nuse - 1) {
                row(); setmode(0x00); pump(now() + MS(2000), false); setloc(P); cmd(0x06); pump(now() + MS(3000), false, 10);
                setmode(0x80); pump(now() + MS(3000), true);
                const bool ok = pause();
                show("SWITCH (Setmode 80h after 10 sectors at 1x)");
                if (!ok) recover();
                row(); setloc(P + 300); cmd(0x15); pump(now() + MS(3000), false, 0, true);
                setloc(P); cmd(0x16); pump(now() + MS(3000), false, 0, true);
                show("SEEK at 2x (SeekL 300 on, SeekP back)");
            }
        }
        row(); setmode(0x00); pump(now() + MS(2000), false); setloc(16); cmd(0x06); pump(now() + MS(3000), false, 5); pause();
        ExitCriticalSection(); printf("\n"); EnterCriticalSection();
        show("BACK at 1x, 00:02:16");
        ExitCriticalSection();
        if (stoppedAt >= 0) printf("\nSTOPPED EARLY after %s: two places in a row read nothing at either speed, so the rest were not tried.\n",
                                   PLACENAME[stoppedAt]);
        printf("\nSUMMARY (1x: sectors of 20; 2x: sectors in 3 s, the first one's time from the ReadN, INT5s and the first one's bytes, the last\n");
        printf("stat; Pause: completed or refused, and whether an Init brought the drive back):\n");
        printf(" the survey, in its order (2x: sectors of 5):\n");
        for (int k = 0; k < NSURVEY; k++) {
            const Sum &V = survey[k];
            if (!V.ran) continue;
            printf("  %s          2x %3d", PLACENAME[SURVEY[k]], V.n2);
            if (V.n2) printf(" from +%u", (unsigned)V.first2); else printf("           ");
            printf("  INT5 x%d", V.int5);
            if (V.int5) printf(" (%02x %02x)", V.e[0], V.e[1]);
            printf("  stat %02x  Pause %s", V.stat2, V.pauseOk ? "ok" : "REFUSED");
            if (!V.pauseOk) printf(", Init %s", V.recovered ? "brought it back" : "did NOT complete");
            printf("\n");
        }
        printf(" the places:\n");
        for (int p = 0; p < NPLACE; p++) {
            const Sum &S = sums[p];
            if (!S.ran) continue;
            if (S.n2 < 0) { printf("  %s  1x %2d   2x not tried (the survey read nothing at double speed)\n", PLACENAME[p], S.n1); continue; }
            printf("  %s  1x %2d   2x %3d", PLACENAME[p], S.n1, S.n2);
            if (S.n2) printf(" from +%u", (unsigned)S.first2); else printf("           ");
            printf("  INT5 x%d", S.int5);
            if (S.int5) printf(" (%02x %02x)", S.e[0], S.e[1]);
            printf("  stat %02x  Pause %s", S.stat2, S.pauseOk ? "ok" : "REFUSED");
            if (!S.pauseOk) printf(", Init %s", S.recovered ? "brought it back" : "did NOT complete");
            printf("\n");
        }
        EnterCriticalSection();
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\ncdrom/cd-2x-burned: Done.\n");
    for (;;) VSync(0);
}
