/*
 * cdrom/cd-xa-switch - leaving an XA music stream for a data read, with the commands, spacing and sector reads
 * of Simple 1500 Series Vol. 8 - The Solitaire (Japan) (2026-10-06).
 *
 * WHY. Under the sweep's deep schedule that game crashes on this emulator's CD model (A0h 40h, a bus error at
 * 80013ED4h through a pointer read from a file never loaded) on 3 of 4 timing draws of M368's model and at 3 of 5
 * press shifts of M366's, and NEVER on the drive's own programs (the SCPH-7501's and 9001's, 13 runs). Its own CD
 * code, traced (tools/cdtrace.js): title music is an XA stream - Setfilter 01,07, Setmode C8h, Setloc 00:20:05,
 * Demute, ReadS, then GetlocL every three frames. Leaving it for a file at 00:08:34, it writes on the model
 *   A   Setmode A0h, ReadN, Setloc 00:08:34, Setloc 00:08:34, Setmode A0h, ReadN
 * and on the drive's program
 *   B   Setloc 00:08:34, Setmode A0h, ReadN
 * each command ~1,450 cycles after the last one's INT3 is acknowledged. With XA off (A0h) the music's sectors
 * come in as data; the game reads each INT1's first three words (header and subheader) and, when they are not
 * the sector it wants, Pauses. Where that stray INT1 falls among A's six commands decides the crash: inside the
 * first Setloc on M366's model (the game finishes its sequence and recovers), after the second Setmode A0h on
 * M368's (the game Pauses in place of its ReadN and gives the file up).
 *   And which of A or B the game writes depends on where its GetlocL poll falls, which the seek back into the
 * music at f1824 sets: ~0.10 s on the model from 00:26:11 (456 sectors back, past DuckStation's switch point at
 * ~330) and 0.05 s from 00:24:06 (301 back) - a two-frame step the consoles' cd-seek rows do not show (the
 * SCPH-7501, 2x near the start: 256 and 512 sectors back both 2.5-3.0M cycles).
 *
 * WHAT IT DOES. Needs THE GAMES DISC (tools/cdgamesdisc.js, burned): Solitaire's layout - its data files below
 * 00:20:05, its music from there (file 1, channels 0-7 in turn, 37.8 kHz stereo, every sector audio) - every
 * sector marked 'D'/'A' + its LBA. A burned disc is locked, so the drive is unlocked first (psx-spx "Secret
 * Unlock Commands", 50h-56h with the region Test 19h,22h names). Interrupts off, the CD's flags polled; Timer 2 at
 * sysclock/8 unwrapped in software (cd-race's clock). Each interrupt is taken as the game takes it: acknowledged
 * 2,350 cycles after it shows, an INT1's three words read by DMA at +3,000, and its data (512 words) after those
 * when it is the file's.
 *   SEEK      the seek back to 00:20:05 while the drive streams the music D sectors further on (D = 510, 345, 200 -
 *             the head where a read 45 sectors before has carried it in 0.3 s), the music read
 *             as data (Setmode A0h), then Setloc 00:20:05 and ReadN: the time from the ReadN's write to its first
 *             sector. Three times each. (Not by the stat or GetlocP - see seekRows.)
 *   SWITCH    the music started, Getstat every two frames until it reads 22h (as the game's), GetlocL every three
 *             frames (as the game's) K times, then after W cycles more the game's own sequence, A or B, with every
 *             interrupt logged until eight sectors of 00:08:34 on have come in (or 2 s), then Pause. A at W = 0,
 *             28,224 ... 197,568 (an eighth of a sector apart, so the stream's phase at the switch walks a whole
 *             sector), B at four of those.
 *
 * READ IT LIKE THIS. Each SWITCH trial prints the last GetlocL (the stream's place), then one line per event:
 * the time from the switch's first write, the command written or the interrupt and its response bytes, and for
 * an INT1 its header MSF, file:channel and marker. The rows to compare are the FIRST INT1 (a music sector's, or
 * none before the ReadN) and where it falls among the commands, and the first 00:08:34. SEEK's rows are the
 * seek times this core's curve has the step in.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-xa-switch --disc=games --ntsc --frames=14000 --quiet=6000`.
 * ON THIS CORE AT v0.367, ON M368's BRANCH CORE, AND ON THE DRIVE'S OWN PROGRAMS (PSX_CD_FIRMWARE), 2026-10-06:
 *   SEEK, the first sector of 00:20:05 (D 510's first time pays the speed change from Init's single speed, 11.2M-13.9M):
 *     v0.367 / M368  D 510 3.78M-3.93M   D 345 3.64M-3.89M   D 200 2.01M-2.14M   (the model's 0.1 s / 0.05 s step)
 *     SC430940       D 510 1.79M         D 345 1.40M-1.55M   D 200 1.47M (3.48M once: a turn missed)
 *     SC430944       D 510 1.84M-1.89M   D 345 1.49M-1.56M   D 200 1.39M-1.59M
 *   SWITCH: the music's sectors come in between A's commands, 0-2 of them, and none or one in B. Where the game
 *     cannot survive one - right after the second Setmode's answer, before its ReadN ("M3*R") - v0.367 puts one at
 *     4 of A's 8 phases, M368 at 1, SC430940 and SC430944 at none: the programs hand a waiting sector over BEFORE
 *     the Setmode's answer ("M*3R", 4 and 4 of 8), which is console day's sector-first rule (M368). The file's first
 *     sector: the model 3.98M-4.36M after the switch (A) and 3.75M-4.06M (B); the programs 2.62M-3.20M and
 *     3.09M-3.47M.
 * Until 2026-10-06's printing fix the trials printed as they ran and read 3-4 strays a sequence: the TTY's time
 * between commands let more sectors in (see printRecs).
 * THE SCPH-9001 (2026-10-06) could not read a burned copy of this disc at double speed - a copy that reads back sector
 * for sector, raw, in a PC drive, its EDC and ECC a pressed disc's: every SEEK row 0 (no sector in 2 s), every command
 * of every trial answered 42h (seeking) and every Pause refused (INT5 43h 80h), while GetlocL read the music's headers
 * (00:20:25, file 1 channel 4): the drive found sectors and never settled on one. So a READ CHECK runs first now (see
 * readCheck) and a drive that fails it runs no row. With the check before SEEK, D 510's first time no longer pays
 * Init's speed change: v0.367 D 510 3.80M-3.90M, D 345 3.64M-3.83M, D 200 1.96M-2.18M; SC430944 D 510 1.80M-3.88M,
 * D 345 1.40M-1.61M, D 200 1.41M-1.49M; READ CHECK 6,545,784 and 6,544,408 cycles.
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

/* ------------------------------------------------------------------------------------------------------------
 * The interrupt taker. take() waits for the next interrupt (or the deadline), handles it as the game would, and
 * logs it. Its record: type, response bytes, and for an INT1 the header MSF, subheader file/channel/submode and
 * the marker.
 */
struct Ev { uint32_t t; uint8_t type; uint8_t n; uint8_t b[8]; uint8_t hdr[12]; uint32_t mark; };
static Ev ev;
static uint32_t wantFrom = 0xFFFFFFFF;   /* an INT1 at or after this LBA has its data read too */
static const uint32_t MUSIC = 1355, FILE_LBA = 484;  /* 00:20:05 (The Solitaire's music) and 00:08:34 (its file) */
static uint32_t lbaOf(const uint8_t *h) {
    const uint32_t m = (h[0] >> 4) * 10 + (h[0] & 15), s = (h[1] >> 4) * 10 + (h[1] & 15), f = (h[2] >> 4) * 10 + (h[2] & 15);
    return (m * 60 + s) * 75 + f - 150;
}
static bool take(uint32_t deadline) {
    uint8_t f;
    for (;;) {
        f = irqFlag();
        if (f) break;
        if ((int32_t)(now() - deadline) >= 0) return false;
    }
    const uint32_t t = now();
    ev.t = t; ev.type = f; ev.n = 0; ev.mark = 0;
    until(t + 2350);
    while (hasResp() && ev.n < 8) ev.b[ev.n++] = resp();
    for (int dn = 0; dn < 32 && hasResp(); dn++) resp();
    ackAll();
    if (f == 1) {
        until(t + 3000);
        want(false); want(true);
        cdDma(3);
        const uint8_t *h = (const uint8_t *)sink;
        for (int i = 0; i < 12; i++) ev.hdr[i] = h[i];
        const uint32_t l = lbaOf(ev.hdr);
        if (l >= wantFrom) { cdDma(512); }
        else { cdDma(1); }                      /* the marker word, to tell sectors apart (a 4th word) */
        ev.mark = sink[0];                      /* want-data stays set, as the game leaves it until the next INT1 */
    }
    return true;
}
/*
 * NOTHING PRINTS WHILE A TRIAL RUNS (2026-10-06): over Unirom's
 * cable the kernel's TTY goes out at 115,200 baud, ~87 us (~2,940 cycles) a character, so a line printed between
 * two of the game's commands would move the replay by tens of thousands of cycles - and by a different amount on
 * a console than here. Each trial's commands and interrupts go to RAM; they print once the drive is paused.
 */
struct Rec { uint32_t t; uint8_t kind, c; Ev e; };      /* kind 0 a command written, 1 an interrupt, 2 a timeout */
static Rec recs[120];
static int nrec;
static void rec(uint8_t kind, uint32_t t, uint8_t c) {
    if (nrec >= 120) return;
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
        if (e.type == 1) {
            printf("  %02x:%02x:%02x %d:%d %02x", e.hdr[0], e.hdr[1], e.hdr[2], e.hdr[4], e.hdr[5], e.hdr[6]);
            if (lbaOf(e.hdr) < FILE_LBA) printf(" %c%d", (char)(e.mark & 0xFF), (int)(e.mark >> 8));
        }
        printf("\n");
    }
    nrec = 0;
}

/* A command, written now; returns its first response's type (3 or 5), recording every interrupt up to it. */
static uint8_t lastResp[8], lastN;
static uint8_t issue(uint8_t c, const uint8_t *p, int np, uint32_t t0, bool log) {
    (void)t0;
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
static uint8_t quiet(uint8_t c, const uint8_t *p = 0, int np = 0) { return issue(c, p, np, 0, false); }
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    const uint8_t p[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) };
    quiet(0x02, p, 3);
}
/* Waits out INT1s and INT2s for `cycles`, as the game's handler would take them. */
static void idle(uint32_t cycles, uint32_t t0, bool log) {
    (void)t0;
    const uint32_t end = now() + cycles;
    while (take(end)) if (log) rec(1, ev.t, 0);
}

/* The secret unlock (psx-spx): Test 19h,22h names the region; "for U/C" -> "of America", "for Europe" -> "(Europe)". */
static void unlock() {
    const uint8_t t22[1] = { 0x22 };
    quiet(0x19, t22, 1);
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
        quiet(0x50 + k, p, n);
    }
}

/* THE SPEED THE ROWS RUN AT (2026-10-06): the game's, double, unless the read check finds the drive reads this disc at
 * single speed only (the SCPH-7501 and 9001 with their burned copies: a seek error at double speed, every sector at
 * single) - then every mode byte drops its speed bit and W steps in eighths of a single-speed sector. What the rows ask
 * (which comes first, a waiting sector or the command's answer) is the drive program's rule at either speed.
 * FORCE is a patch point: "SPEED=ONE!" in a copy of the exe runs single speed on a drive that reads double (how the
 * emulator's own single-speed rows are made to lay beside a console's). */
static const volatile char FORCE[11] = "SPEED=AUTO";
static uint8_t SPD = 0x80;
static uint32_t SECT = 225792;

/* The music as the game starts it: Setfilter 01,07; Setmode C8h; Setloc 00:20:05; Demute; ReadS. */
static uint32_t startMusic(bool log, uint32_t t0) {
    const uint8_t f[2] = { 0x01, 0x07 }, m[1] = { (uint8_t)(0x48 | SPD) };
    issue(0x0D, f, 2, t0, log); idle(1450, t0, log);
    issue(0x0E, m, 1, t0, log); idle(1450, t0, log);
    const uint32_t a = MUSIC + 150;
    const uint8_t p[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) };
    issue(0x02, p, 3, t0, log); idle(1450, t0, log);
    issue(0x0C, 0, 0, t0, log); idle(1450, t0, log);
    const uint32_t tw = now();
    issue(0x1B, 0, 0, t0, log);
    return tw;
}

/* The seek back into the music, timed by a signal every drive gives: the music read as DATA (Setmode A0h - XA off,
 * so every one of its sectors is an INT1), D sectors on, then Setloc 00:20:05 and ReadN; the time from that ReadN's
 * write to the first sector of 00:20:05 on, and which sector it was. The game's own seek is a ReadS with XA on; the
 * mechanics are the drive's either way, and neither the stat nor GetlocP can time it: the drive's own program
 * answers 22h at once to a read written while it reads where the model answers 42h, and the model answers GetlocP
 * with the target from the seek's start (2026-10-06, both here). */
static void seekRows() {
    printf("SEEK: reading the music as data D sectors on, Setloc 00:20:05 + ReadN; write -> its first sector:\n");
    static const uint32_t D[3] = { 510, 345, 200 };
    const uint8_t a0[1] = { (uint8_t)(0x20 | SPD) };
    for (int d = 0; d < 3; d++) {
        uint32_t t[3] = { 0, 0, 0 }, at[3] = { 0, 0, 0 };
        for (int rep = 0; rep < 3; rep++) {
            quiet(0x0E, a0, 1);
            setloc(MUSIC + D[d] - 45);
            quiet(0x06);
            idle(45 * SECT, 0, false);                    /* ~45 sectors on (0.3 s at double speed), each taken as the game takes one */
            setloc(MUSIC);
            const uint32_t tw = now();
            quiet(0x06);
            const uint32_t end = now() + 67737600;
            while (take(end)) if (ev.type == 1 && lbaOf(ev.hdr) >= MUSIC && lbaOf(ev.hdr) < MUSIC + 600) { t[rep] = ev.t - tw; at[rep] = lbaOf(ev.hdr); break; }
            quiet(0x09); idle(3000000, 0, false);
        }
        printf("  D %3d :", D[d]);                        /* printed with the drive paused (see printRecs) */
        for (int rep = 0; rep < 3; rep++) printf(" %8d (+%d)", t[rep], at[rep] ? (int)(at[rep] - MUSIC) : -1);
        printf("\n");
    }
}

/* READ CHECK (2026-10-06): the music read as data, the time from its first sector to its 30th - 29 sector times (a
 * rate, so the speed change before the first sector does not count; a first version counted sectors in a second and
 * the change took 0.65 s of it). On the SCPH-9001 a burned copy of this disc - one that reads back sector for sector
 * in a PC drive - never read at double speed: every command answered 42h, no sector came, and a Pause was refused
 * (INT5 43h 80h). The SCPH-7501 then read its disc check (00:02:16, single speed) and nothing at all in 3 s at double
 * speed, where it reads cd-xa's CD-R perfectly - at single speed, all cd-xa uses. So the check waits out the speed
 * change (2 s after the Setmode), prints ReadN's own answer, allows 6 s, and if double speed fails it times single
 * speed too: the printout says which speed this drive reads the disc at. */
static bool tryRate(uint8_t mode, uint32_t sectorTime, const char *name) {
    const uint8_t m[1] = { mode };
    quiet(0x0E, m, 1); idle(67737600, 0, false);          /* 2 s: the speed change */
    setloc(MUSIC);
    const uint8_t ty = quiet(0x06);
    const uint8_t s0 = lastN ? lastResp[0] : 0, s1 = lastN > 1 ? lastResp[1] : 0;
    int n = 0, e = 0; uint8_t st = 0; uint32_t first = 0, last = 0;
    const uint32_t end = now() + 203212800;               /* 6 s */
    while (n < 30 && take(end)) { if (ev.type == 1) { if (!n) first = ev.t; last = ev.t; n++; } else if (ev.type == 5) e++; if (ev.n) st = ev.b[0]; }
    quiet(0x09); idle(4000000, 0, false);
    const uint32_t span = n > 1 ? last - first : 0, due = 29 * sectorTime;
    printf("READ CHECK at %s speed: ReadN INT%d %02x %02x; %d of 30 sectors of the music in 6 s, first to last %u cycles (29 sector times: %u), %d errors, last stat %02x\n",
           name, ty, s0, ty == 5 ? s1 : 0, n, (unsigned)span, (unsigned)due, e, st);
    return n == 30 && span < due + due / 2;
}
/* 2: the drive reads the disc at double speed; 1: at single only; 0: not at all. */
static int readCheck() {
    if (FORCE[6] != 'O' && tryRate(0xA0, 225792, "double")) return 2;
    return tryRate(0x20, 451584, "single") ? 1 : 0;
}

static void switchTrial(char path, uint32_t W, int K) {
    startMusic(false, 0);
    uint8_t st = 0; int polls = 0;
    do { idle(1132242, 0, false); quiet(0x01); st = lastN ? lastResp[0] : 0; polls++; } while ((st & 0x60) != 0x20 && polls < 30);
    uint8_t g[8] = { 0 }; int gn = 0;
    for (int k = 0; k < K; k++) {
        idle(1698363, 0, false);
        quiet(0x10);
        gn = lastN; for (int i = 0; i < gn; i++) g[i] = lastResp[i];
    }
    idle(1450 + W, 0, false);
    nrec = 0;
    const uint32_t t0 = now();
    wantFrom = FILE_LBA;
    const uint32_t a = FILE_LBA + 150;
    const uint8_t loc[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) }, a0[1] = { (uint8_t)(0x20 | SPD) };
    if (path == 'A') {
        issue(0x0E, a0, 1, t0, true); idle(1450, t0, true);
        issue(0x06, 0, 0, t0, true); idle(1450, t0, true);
        issue(0x02, loc, 3, t0, true); idle(1450, t0, true);
    }
    issue(0x02, loc, 3, t0, true); idle(1450, t0, true);
    issue(0x0E, a0, 1, t0, true); idle(1450, t0, true);
    issue(0x06, 0, 0, t0, true);
    int got = 0;
    const uint32_t end = now() + 67737600;               /* 2 s */
    while (got < 8 && take(end)) { rec(1, ev.t, 0); if (ev.type == 1 && lbaOf(ev.hdr) >= FILE_LBA) got++; }
    issue(0x09, 0, 0, t0, true);
    idle(4000000, t0, true);
    wantFrom = 0xFFFFFFFF;
    /* The drive is paused: now the trial prints (printRecs). */
    printf("%c W %6d: %d Getstats to 22h, GetlocL", path, W, polls);
    for (int i = 0; i < gn; i++) printf(" %02x", g[i]);
    printf("\n");
    printRecs(t0);
}

int main() {
    printf("\ncdrom/cd-xa-switch\n");
    printf("Leaving an XA music stream for a data read, with The Solitaire's commands and spacing. Needs the games disc.\n");
    printf("Cycles at the system clock (1 ms = 33,869). No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                                     /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                      /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    unlock();
    quiet(0x0A); idle(70000000, 0, false);                /* Init and its INT2 */
    quiet(0x0C);                                          /* Demute */
    /* The disc check: 00:02:16's marker. */
    const uint8_t m0[1] = { 0x20 };                       /* whole sectors: the header, then the marker */
    quiet(0x0E, m0, 1); setloc(16); wantFrom = 0xFFFFFFFF; quiet(0x06);
    const bool got = take(now() + 70000000);
    const uint8_t ty = got ? ev.type : 0, st = (got && ev.n) ? ev.b[0] : 0, er = (got && ev.n > 1) ? ev.b[1] : 0;
    const bool games = ty == 1 && (ev.mark & 0xFF) == 'D' && (ev.mark >> 8) == 16;
    int rc = 0;
    quiet(0x09); idle(4000000, 0, false);
    if (!games && ty != 1) {
        /* A burned disc the unlock did not open answers the read with an error, or not at all (the 9001's own
         * program here with the unlock left out, 2026-10-06: nothing in 2 s). */
        printf("THE DRIVE REFUSED THE READ (INT%d %02x %02x) - a burned disc still locked? Boot it from Unirom's menu.\n", ty, st, er);
    } else if (!games) {
        printf("THIS IS NOT THE GAMES DISC (00:02:16 reads %08x) - put it in and run again.\n", ev.mark);
    } else if (!(rc = readCheck())) {
        printf("THIS DRIVE CANNOT READ THE DISC AT EITHER SPEED - every row would be read errors, so none is run.\n");
    } else {
        if (rc == 1) {
            SPD = 0x00; SECT = 451584;
            printf("SINGLE SPEED: this drive reads the disc at single speed only, so every row runs there (Setmode 48h / 20h,\n");
            printf("a sector 451,584 cycles, W in eighths of one). The game's speed is double.\n");
        }
        seekRows();
        printf("\nSWITCH (A: the model's sequence; B: the drive program's), times from the switch's first write:\n");
        for (int w = 0; w < 8; w++) switchTrial('A', w * (SECT / 8), 6);
        for (int w = 0; w < 8; w += 2) switchTrial('B', w * (SECT / 8), 6);
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
