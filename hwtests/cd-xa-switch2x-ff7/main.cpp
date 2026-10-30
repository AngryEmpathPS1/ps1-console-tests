/*
 * cdrom/cd-xa-switch2x-ff7 - cd-xa-switch2x on FINAL FANTASY VII (JAPAN) DISC 1 (2026-10-26), for a console that reads no
 * other pressed disc here: the SCPH-5000 (a Japanese drive reads only a Japanese disc, and has no unlock command).
 * cd-xa-switch2x itself stays the program its consoles' logs came from: rebuilt with FF7 beside The X-Files, its X-Files
 * rows moved by tens of cycles on this deterministic core (2,560,616 -> 2,573,272 at one D 200 row) - the code moved, and
 * these instruments time code. So the port is its own program: the same method, rows and spacing, on FF7's RCKTFAIL.MOV
 * where The X-Files has FOXLOGO.STR (below). Its references run on --disc=ff7jp; consolerun runs it with --with=ff7jpdisc.
 * THIS ONE'S RULE: `node hwtests/run.js cd-xa-switch2x-ff7 --disc=ff7jp --ntsc --settle=600 --frames=12000 --quiet=6000`.
 * PREDICTED, v0.377.199 (2026-10-26), the drive model / the SCPH-5000's own program (SC430916, PSX_MODEL=SCPH-5000, its
 * BIOS 2.2 J): the disc check finds RCKTFAIL.MOV on both; READ CHECK at double speed 6.54M on both; SEEK 00:20:05 D 510
 * 3.45M-3.48M / 3.44M-3.46M, D 345 4.08M-4.09M / 6.20M-6.22M, D 200 2.36M-2.57M / 6.63M; 54:09:52 D 805 4.66M-4.68M /
 * 4.64M-4.66M, D 510 4.73M-4.74M / 4.71M-4.72M, D 200 3.78M-3.81M / 3.76M-3.79M; all 20 trials run.
 *
 * cd-xa-switch2x's own header (2026-10-06, of The X-Files):
 * cdrom/cd-xa-switch2x - leaving an XA movie stream at DOUBLE SPEED on a pressed disc: The X-Files (USA) Disc 1's own
 * FOXLOGO.STR, the game's own way out of it and The Solitaire's (2026-10-06).
 *
 * WHY. hwtests/cd-xa-switch replays The Solitaire (Japan)'s switch from its XA music to a file, the conversation
 * that crashes that game on this core's CD model and never on the drives' own programs. It needs the games disc, a
 * CD-R, and all three consoles read CD-Rs at single speed only: the SCPH-7501 and 5501 gave the
 * sector-first rule there (a waiting sector goes out BEFORE the command's answer, as the programs do and the model
 * does not) but nobody has seen it at the game's speed, double. A pressed disc reads at double speed, and The X-Files
 * is the pressed disc every CD test here already uses.
 *   The X-Files' own CD code, traced (tools/cdtrace.js, f0-f6000 under the sweep's deep schedule): its logo movies
 * are STR files read with Setloc, Setmode E0h (double speed, XA on, no filter, whole sectors), ReadS - FOXLOGO.STR at
 * 51:27:47 first - each video sector an INT1, taken as acknowledge at ~+2,200, want-data, a DMA of 8 words and one of
 * 504. A press ends the movie the careful way: Pause, wait for its INT2, then Setmode 80h, Setloc 51:16:67
 * (LOADING.BS), Setmode A0h, ReadN.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled; Timer 2 at sysclock/8 unwrapped in software. Every interrupt is
 * taken as the game takes one (acknowledged 2,300 cycles after it shows; an INT1's want-data at +2,900 and 512 words by
 * DMA, the first four - header, subheader and the data's first word - kept). Init, Demute (as the game), the disc
 * check (FOXLOGO.STR's first sector: file 1, channel 1), then a READ CHECK: the data at 00:20:05 at double speed, the
 * 1st to the 30th sector (29 sector times, 6,548,000 cycles); a drive that cannot read the disc at double speed runs
 * every row at single, as cd-xa-switch does.
 *   SEEK    as cd-xa-switch's rows (data read from D sectors on, then Setloc + ReadN; the ReadN's write to its target's
 *           first sector), at The Solitaire's place (00:20:05: D 510, 345, 200 - SOUNDS1/CM.VB here, plain data) and the
 *           game's own (51:16:67, LOADING.BS: D 805 - FOXLOGO.STR's start, the game's seek - 510 and 200). Three times
 *           each.
 *   PAUSE   the game's way: FOXLOGO.STR streamed as the game streams it, 24 sectors in, then Pause written W cycles
 *           after an INT1 (W = 20,000 + n x an eighth of a sector, n = 0..7, so the stream's phase walks a sector); every
 *           interrupt to the Pause's INT2, then the game's Setmode 80h, Setloc 51:16:67, Setmode A0h, ReadN with its own
 *           spacing (3,400 cycles after the INT2, 384,500 after Setmode's answer, 3,300, 2,250), and LOADING.BS's ten
 *           sectors; Pause.
 *   SWITCH  The Solitaire's way at double speed: the movie streamed as above, then W after an INT1 the model's sequence
 *           A (Setmode A0h, ReadN, Setloc, Setloc, Setmode A0h, ReadN) or the programs' B (Setloc, Setmode A0h, ReadN)
 *           to LOADING.BS, each command 1,450 cycles after the last answer was taken, every interrupt to the file's eighth
 *           sector. A at the eight W, B at four.
 *
 * READ IT LIKE THIS. Each PAUSE and SWITCH trial prints one line per command written and per interrupt, with times from
 * its first write; an INT1 shows its header (MSF), file:channel, submode (48 a video sector, 64 audio - an audio sector
 * comes as data only with XA off - 00 a null). The rows to compare: an INT1 between a command's write and its INT3 (the
 * sector-first order, "M*3") or after it ("M3*"); the Pause's INT3 and INT2 times (the model's Pause is a turn of the disc,
 * M367); the first LOADING.BS sector (the seek back). SEEK's rows lay beside cd-xa-switch's model and program numbers.
 *
 * NOTHING PRINTS WHILE A TRIAL RUNS (cd-xa-switch's rule: Unirom's TTY is 115,200 baud, ~2,940 cycles a character).
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-xa-switch2x --disc=xfiles --ntsc --settle=600 --frames=12000 --quiet=6000`.
 * HERE AT v0.369 AND ON THE THREE DRIVE PROGRAMS (PSX_CD_FIRMWARE: SC430930 the 5501's, SC430940 the 7501's, SC430944 the
 * 9001's), 2026-10-06. READ CHECK 6.54M on all four.
 *   SEEK (the head D sectors on - its read's first sector seen before the 44 more; cd-xa-switch's rows wait 45 from the
 *     ReadN, the seek included, so the two tests' numbers are not the same measurement):
 *       v0.369     00:20:05 D 510 3.80M-3.90M, D 345 3.64M-3.78M, D 200 1.96M-2.20M; 51:16:67 D 805, 510, 200 all 3.66M-3.88M
 *       programs   00:20:05 D 510 3.43M-3.54M, D 345 3.31M-3.35M, D 200 2.87M-2.94M; 51:16:67 D 805 4.93M-4.99M, D 510
 *                  3.63M-3.74M, D 200 6.19M-6.25M (all three alike to ~1%; where the target comes round, not the distance)
 *   PAUSE: INT3 25,000-80,000 cycles after the Pause on all four, INT2 3.92M-4.14M (a turn of the disc, M367: the model
 *     within 0.03M of the programs at every phase). The file's first sector 8.24M-8.53M after the Pause here, 7.39M-7.60M
 *     on the programs (one 11.47M, a turn more). A sector due just after the Pause is written: the programs hand it over
 *     BEFORE the Pause's answer at the last two phases ("P*3", W 189,344 and 217,568, on all three); the model answers
 *     first and drops it (once, at W 161,120, it delivers one before).
 *   SWITCH, the order (M Setmode, R ReadN, L Setloc; 3 its answer; * a stray INT1 - FOXLOGO's 51:27:73 every time):
 *       v0.369     every stray AFTER an answer: A W 20,000 "M3 R3 L3 L3 M3* R3" - The Solitaire's fatal place - and so at
 *                  2 of 8 phases; B once ("L3 M3* R3").
 *       programs   every stray BEFORE the answer of the command it lands in ("M*3", "L*3", "R*3"), at every phase of A and
 *                  B, 1-2 a trial; never "M3*R". The file's first sector 3.13M-3.35M after the switch (7.40M-7.45M where
 *                  a stray came in the first Setmode), the model 3.90M-4.21M.
 * So at the game's speed the drive programs keep the sector-first order cd-xa-switch's single-speed rows found on the
 * SCPH-7501 and 5501. A console decides it.
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
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void until(uint32_t t) { while ((int32_t)(now() - t) < 0) { } }

static uint32_t sink[600];
static void cdDma(uint32_t words) {
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x00008000);   /* DMA3 on */
    write32(0x1F8010B0, (uint32_t)sink & 0x1FFFFFFF);
    write32(0x1F8010B4, words);
    write32(0x1F8010B8, 0x11000000);                          /* to RAM, start, trigger */
    for (uint32_t k = 0; (read32(0x1F8010B8) & 0x01000000) && k < 2000000; k++) { tick(); }   /* rule 1: bounded */
}

/* FOXLOGO.STR (the game's first movie), LOADING.BS (where it goes after it), and The Solitaire's music position (00:20:05,
 * plain data here: SOUNDS1/CM.VB).
 * HERE, FINAL FANTASY VII (JAPAN) DISC 1 (2026-10-26, the SCPH-5000's disc: a Japanese drive reads only a Japanese one):
 * RCKTFAIL.MOV at 54:20:32 (244382), a movie built as FOXLOGO.STR is (video
 * on file 1 channel 1, one XA audio sector in eight, 37.8 kHz stereo, at nearly its radius: 51:27); the data 805 sectors
 * before it (243577, 54:09:52, inside SETO.STR) as LOADING.BS, the same seek back; 00:20:05 plain data on it too
 * (WORLD/WM2.TXZ). Read off the library's image (scratch/ff7jp_ls.js, kept as experiments/ff7jp_ls.js). */
static const uint32_t STREAM = 244382, FILE_LBA = 243577, SOLI = 1355;
static void printMsf(uint32_t lba) { const uint32_t a = lba + 150; printf("%02d:%02d:%02d", (int)(a / 4500), (int)((a / 75) % 60), (int)(a % 75)); }

struct Ev { uint32_t t; uint8_t type; uint8_t n; uint8_t b[8]; uint8_t hdr[12]; uint32_t w3; };
static Ev ev;
static uint32_t lbaOf(const uint8_t *h) {
    const uint32_t m = (h[0] >> 4) * 10 + (h[0] & 15), s = (h[1] >> 4) * 10 + (h[1] & 15), f = (h[2] >> 4) * 10 + (h[2] & 15);
    return (m * 60 + s) * 75 + f - 150;
}
/* The next interrupt (or the deadline), taken as the game takes one: acknowledged at +2,300; an INT1's want-data at
 * +2,900 and 512 words by DMA (the game's 8 + 504), its header, subheader and first data word kept. */
static bool take(uint32_t deadline) {
    uint8_t f;
    for (;;) {
        f = irqFlag();
        if (f) break;
        if ((int32_t)(now() - deadline) >= 0) return false;
    }
    const uint32_t t = now();
    ev.t = t; ev.type = f; ev.n = 0; ev.w3 = 0;
    until(t + 2300);
    while (hasResp() && ev.n < 8) ev.b[ev.n++] = resp();
    for (int dn = 0; dn < 32 && hasResp(); dn++) resp();
    ackAll();
    if (f == 1) {
        until(t + 2900);
        want(false); want(true);
        cdDma(4);
        const uint8_t *h = (const uint8_t *)sink;
        for (int i = 0; i < 12; i++) ev.hdr[i] = h[i];
        ev.w3 = sink[3];
        cdDma(508);
    }
    return true;
}

struct Rec { uint32_t t; uint8_t kind, c; Ev e; };      /* kind 0 a command written, 1 an interrupt, 2 a timeout */
static Rec recs[140];
static int nrec;
static void rec(uint8_t kind, uint32_t t, uint8_t c) {
    if (nrec >= 140) return;
    recs[nrec].kind = kind; recs[nrec].t = t; recs[nrec].c = c;
    if (kind == 1) recs[nrec].e = ev;
    nrec++;
}
static const char *NAME[0x20] = { 0, "Getstat", "Setloc", "Play", 0, 0, "ReadN", 0, "Stop", "Pause", "Init", 0, "Demute",
                                  "Setfilter", "Setmode", 0, "GetlocL", "GetlocP", 0, 0, 0, 0, 0, 0, 0, 0, 0, "ReadS" };
static void printRecs(uint32_t t0) {
    for (int k = 0; k < nrec; k++) {
        const Rec &r = recs[k];
        if (r.kind == 0) { printf("    +%8d %s\n", r.t - t0, NAME[r.c & 0x1F] ? NAME[r.c & 0x1F] : "?"); continue; }
        if (r.kind == 2) { printf("    TIMEOUT\n"); continue; }
        const Ev &e = r.e;
        printf("    +%8d INT%d", e.t - t0, e.type);
        for (int i = 0; i < e.n; i++) printf(" %02x", e.b[i]);
        if (e.type == 1) printf("  %02x:%02x:%02x %d:%d %02x", e.hdr[0], e.hdr[1], e.hdr[2], e.hdr[4], e.hdr[5], e.hdr[6]);
        printf("\n");
    }
    nrec = 0;
}

static uint8_t lastResp[8], lastN;
/* A command, written now; returns its first response's type (3 or 5), taking (and with `log` recording) every
 * interrupt up to it. */
static uint8_t issue(uint8_t c, const uint8_t *p, int np, bool log) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t tw = now();
    command(c);
    if (log) rec(0, tw, c);
    for (;;) {
        if (!take(tw + 70000000)) { if (log) rec(2, now(), c); return 0; }
        if (log) rec(1, ev.t, 0);
        if (ev.type == 3 || ev.type == 5) { lastN = ev.n; for (int i = 0; i < ev.n; i++) lastResp[i] = ev.b[i]; return ev.type; }
    }
}
static uint8_t quiet(uint8_t c, const uint8_t *p = 0, int np = 0) { return issue(c, p, np, false); }
static void locOf(uint32_t lba, uint8_t *p) { const uint32_t a = lba + 150; p[0] = bcd(a / 4500); p[1] = bcd((a / 75) % 60); p[2] = bcd(a % 75); }
static void setloc(uint32_t lba, bool log = false) { uint8_t p[3]; locOf(lba, p); issue(0x02, p, 3, log); }
/* Takes interrupts for `cycles`, as the game's handler would. */
static void idle(uint32_t cycles, bool log) {
    const uint32_t end = now() + cycles;
    while (take(end)) if (log) rec(1, ev.t, 0);
}

/* The speed the rows run at: double, unless the read check finds the drive reads this disc at single speed only (then
 * every mode byte drops its speed bit and W steps in eighths of a single-speed sector, as cd-xa-switch). */
static uint8_t SPD = 0x80;
static uint32_t SECT = 225792;

static bool tryRate(uint8_t mode, uint32_t sectorTime, const char *name) {
    const uint8_t m[1] = { mode };
    quiet(0x0E, m, 1); idle(67737600, false);             /* 2 s: the speed change */
    setloc(SOLI);
    const uint8_t ty = quiet(0x06);
    const uint8_t s0 = lastN ? lastResp[0] : 0, s1 = lastN > 1 ? lastResp[1] : 0;
    int n = 0, e = 0; uint8_t st = 0; uint32_t first = 0, last = 0;
    const uint32_t end = now() + 203212800;               /* 6 s */
    while (n < 30 && take(end)) { if (ev.type == 1) { if (!n) first = ev.t; last = ev.t; n++; } else if (ev.type == 5) e++; if (ev.n) st = ev.b[0]; }
    quiet(0x09); idle(4000000, false);
    const uint32_t span = n > 1 ? last - first : 0, due = 29 * sectorTime;
    printf("READ CHECK at %s speed: ReadN INT%d %02x %02x; %d of 30 sectors from 00:20:05 in 6 s, first to last %u cycles (29 sector times: %u), %d errors, last stat %02x\n",
           name, ty, s0, ty == 5 ? s1 : 0, n, (unsigned)span, (unsigned)due, e, st);
    return n == 30 && span < due + due / 2;
}
static int readCheck() {
    if (tryRate(0xA0, 225792, "double")) return 2;
    return tryRate(0x20, 451584, "single") ? 1 : 0;
}

/* The seek, timed by its target's first sector: data read from D sectors on (45 sectors before, ~0.3 s at double
 * speed), Setloc target, ReadN; the ReadN's write to the first sector at or after the target (cd-xa-switch's seekRows). */
static void seekRow(uint32_t target, uint32_t D) {
    const uint8_t a0[1] = { (uint8_t)(0x20 | SPD) };
    uint32_t t[3] = { 0, 0, 0 }, at[3] = { 0, 0, 0 };
    for (int rep = 0; rep < 3; rep++) {
        quiet(0x0E, a0, 1);
        setloc(target + D - 45);
        quiet(0x06);
        /* The read's first sector before the 44 more (2026-10-06): the first row at 51:16:67 came from 00:20:05, and on
         * the drives' programs that sled move outlasted the 45 sectors' wait - its time (33.5M-35.3M cycles) was the
         * move's, not the seek back's. */
        { const uint32_t end = now() + 169344000; while (take(end)) if (ev.type == 1 && lbaOf(ev.hdr) >= target + D - 45) break; }
        idle(44 * SECT, false);
        setloc(target);
        const uint32_t tw = now();
        quiet(0x06);
        const uint32_t end = now() + 67737600;
        while (take(end)) if (ev.type == 1 && lbaOf(ev.hdr) >= target && lbaOf(ev.hdr) < target + 600) { t[rep] = ev.t - tw; at[rep] = lbaOf(ev.hdr); break; }
        quiet(0x09); idle(3000000, false);
    }
    printf("  "); printMsf(target); printf(" D %3d :", (int)D);
    for (int rep = 0; rep < 3; rep++) printf(" %8d (+%d)", t[rep], at[rep] ? (int)(at[rep] - target) : -1);
    printf("\n");
}

/* The movie as the game starts it (Setloc, Setmode E0h, ReadS: its spacing), then 24 of its sectors taken; returns the
 * last INT1's time. */
static uint32_t startStream() {
    const uint8_t e0[1] = { (uint8_t)(0x60 | SPD) };
    setloc(STREAM); idle(27000, false);
    quiet(0x0E, e0, 1); idle(1000, false);
    quiet(0x1B);
    int n = 0; uint32_t last = 0;
    const uint32_t end = now() + 135475200;               /* 4 s */
    while (n < 24 && take(end)) if (ev.type == 1) { n++; last = ev.t; }
    return n == 24 ? last : 0;
}
/* Waits until W cycles after an INT1 - the last one taken, or a later one if it comes first. */
static void afterInt1(uint32_t last, uint32_t W) {
    for (;;) {
        const uint32_t t = now();
        if ((int32_t)(t - (last + W)) >= 0) return;
        const uint8_t f = irqFlag();
        if (f) { take(t); if (ev.type == 1) last = ev.t; }
    }
}

static void pauseTrial(uint32_t W) {
    const uint32_t last = startStream();
    if (!last) { printf("P W %6d: the movie never streamed 24 sectors\n", (int)W); quiet(0x09); idle(4000000, false); return; }
    afterInt1(last, W);
    nrec = 0;
    const uint32_t t0 = now();
    issue(0x09, 0, 0, true);                              /* Pause */
    { const uint32_t end = now() + 135475200; while (take(end)) { rec(1, ev.t, 0); if (ev.type == 2 || ev.type == 5) break; } }
    uint8_t loc[3]; locOf(FILE_LBA, loc);
    const uint8_t m80[1] = { (uint8_t)(0x00 | SPD) }, a0[1] = { (uint8_t)(0x20 | SPD) };
    idle(3400, true);
    issue(0x0E, m80, 1, true); idle(384500, true);       /* Setmode 80h */
    issue(0x02, loc, 3, true); idle(3300, true);         /* Setloc 51:16:67 (FILE_LBA) */
    issue(0x0E, a0, 1, true); idle(2250, true);          /* Setmode A0h */
    issue(0x06, 0, 0, true);                              /* ReadN */
    int got = 0;
    const uint32_t end = now() + 67737600;               /* 2 s */
    while (got < 10 && take(end)) { rec(1, ev.t, 0); if (ev.type == 1 && lbaOf(ev.hdr) >= FILE_LBA && lbaOf(ev.hdr) < FILE_LBA + 10) got++; }
    issue(0x09, 0, 0, true);
    idle(4000000, true);
    printf("P W %6d:\n", (int)W);
    printRecs(t0);
}

static void switchTrial(char path, uint32_t W) {
    const uint32_t last = startStream();
    if (!last) { printf("%c W %6d: the movie never streamed 24 sectors\n", path, (int)W); quiet(0x09); idle(4000000, false); return; }
    afterInt1(last, W);
    nrec = 0;
    const uint32_t t0 = now();
    uint8_t loc[3]; locOf(FILE_LBA, loc);
    const uint8_t a0[1] = { (uint8_t)(0x20 | SPD) };
    if (path == 'A') {
        issue(0x0E, a0, 1, true); idle(1450, true);
        issue(0x06, 0, 0, true); idle(1450, true);
        issue(0x02, loc, 3, true); idle(1450, true);
    }
    issue(0x02, loc, 3, true); idle(1450, true);
    issue(0x0E, a0, 1, true); idle(1450, true);
    issue(0x06, 0, 0, true);
    int got = 0;
    const uint32_t end = now() + 67737600;               /* 2 s */
    while (got < 8 && take(end)) { rec(1, ev.t, 0); if (ev.type == 1 && lbaOf(ev.hdr) >= FILE_LBA && lbaOf(ev.hdr) < FILE_LBA + 10) got++; }
    issue(0x09, 0, 0, true);
    idle(4000000, true);
    printf("%c W %6d:\n", path, (int)W);
    printRecs(t0);
}

/* The disc check: Setmode A0h, Setloc, ReadN at `stream`; its first sector (or error) printed; true for file 1 channel 1
 * within its first 40 sectors. */
static bool discCheck(uint32_t stream) {
    const uint8_t a0[1] = { 0xA0 };
    quiet(0x0E, a0, 1); setloc(stream); quiet(0x06);
    bool got = false;
    { const uint32_t end = now() + 135475200; while (take(end)) if (ev.type == 1 || ev.type == 5) { got = true; break; } }
    const uint8_t ty = got ? ev.type : 0;
    const uint32_t l = (got && ty == 1) ? lbaOf(ev.hdr) : 0;
    const bool ok = ty == 1 && l >= stream && l < stream + 40 && ev.hdr[4] == 1 && ev.hdr[5] == 1;
    printf("disc check: INT%d, %02x:%02x:%02x file %d channel %d submode %02x, word %08x\n", ty, ev.hdr[0], ev.hdr[1], ev.hdr[2],
           ev.hdr[4], ev.hdr[5], ev.hdr[6], (unsigned)ev.w3);
    quiet(0x09); idle(4000000, false);
    return ok;
}

int main() {
    printf("\ncdrom/cd-xa-switch2x-ff7\n");
    printf("Leaving an XA movie stream at double speed: Final Fantasy VII (Japan) Disc 1's RCKTFAIL.MOV, The X-Files' way out\n");
    printf("(cd-xa-switch2x) and The Solitaire's. Needs that disc. Cycles at the system clock (1 ms = 33,869). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                                     /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                      /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    quiet(0x0A); idle(70000000, false);                   /* Init and its INT2 */
    quiet(0x0C);                                          /* Demute */
    /* The disc check: RCKTFAIL.MOV's first sector, file 1 channel 1. */
    const bool ff7 = discCheck(STREAM);
    int rc = 0;
    if (!ff7) {
        printf("THIS IS NOT FINAL FANTASY VII (JAPAN) DISC 1 (54:20:32 should be RCKTFAIL.MOV, file 1 channel 1) - put it in and run again.\n");
    } else if (!(rc = readCheck())) {
        printf("THIS DRIVE CANNOT READ THE DISC AT EITHER SPEED - every row would be read errors, so none is run.\n");
    } else {
        if (rc == 1) {
            SPD = 0x00; SECT = 451584;
            printf("SINGLE SPEED: this drive reads the disc at single speed only, so every row runs there (W in eighths of a\n");
            printf("single-speed sector). The game's speed is double.\n");
        }
        printf("\nSEEK: data read from D sectors on, Setloc + ReadN; the ReadN's write to its target's first sector:\n");
        seekRow(SOLI, 510);
        seekRow(SOLI, 345);
        seekRow(SOLI, 200);
        seekRow(FILE_LBA, 805);
        seekRow(FILE_LBA, 510);
        seekRow(FILE_LBA, 200);
        printf("\nPAUSE (the game's way: Pause, its INT2, Setmode 80h, Setloc "); printMsf(FILE_LBA);
        printf(", Setmode A0h, ReadN), times from the Pause:\n");
        for (int w = 0; w < 8; w++) pauseTrial(20000 + w * (SECT / 8));
        printf("\nSWITCH (The Solitaire's: A the model's sequence, B the drive programs'), times from the first write:\n");
        for (int w = 0; w < 8; w++) switchTrial('A', 20000 + w * (SECT / 8));
        for (int w = 0; w < 8; w += 2) switchTrial('B', 20000 + w * (SECT / 8));
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\ncdrom/cd-xa-switch2x-ff7: Done.\n");
    for (;;) VSync(0);
}
