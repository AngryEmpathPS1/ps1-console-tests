/*
 * cdrom/cd-head - where the pickup waits when the drive is idle, how long it
 * takes to read on from a Pause, and what the sector buffer holds after a
 * seek, an Init or a new read.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, CD rows C28, C45, C50 and C55).
 *   THE IDLE HEAD (M110, row C28). After a Pause or a seek the disc turns under
 *     a pickup that holds one track: GetlocP's position advances a sector per
 *     sector time and jumps back a track, and the hold is two sectors past the
 *     target in logical mode (DuckStation's UpdateSubQPosition: "hardware
 *     tests show that it holds much closer to the target sector"). The only
 *     console readings are ps1-tests' cdrom/getloc: 00:01:68 after Init, and
 *     00:02:11 after SeekL 00:02:16 with GetlocL 16. nugget's SCPH-9001 reads
 *     differently after Init (STATUS M237).
 *   THE PAUSE HOLD (row C55). A Pause during a read holds the head one track
 *     BEHIND the last sector read (DuckStation; Nightmare Creatures and Adidas
 *     Power Soccer, M62), so a read of the next sector waits for it to come
 *     round, or seeks: which, and how long, decides streaming games' pace.
 *   THE BUFFER (rows C45, C50). SeekL, Init, Stop and SetSession drop the
 *     sectors already buffered, and a new ReadN drops a sector INT1 not yet
 *     delivered (DuckStation; games tested only by working).
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at the system
 * clock unwrapped in software. Needs a data disc of at least 32 minutes for
 * the mid-disc rows (any game whose data runs that far; a short disc prints
 * an INT5 there and nothing else changes).
 *   IDLE: GetlocP every 20 ms for 1.2 s after Init; after SeekL 00:02:16; after
 *     SeekL 00:30:00; after reading 00:02:16..00:02:31 and a Pause; the same at
 *     double speed. Each reading's absolute MSF, as runs of consecutive
 *     sectors, so the circling shows as a climb and a jump back. GetlocL
 *     after each.
 *   RESUME: 00:02:16..00:02:31 read at single speed and Paused; then Setloc
 *     00:02:32 and ReadN W ms after the Pause's INT2 (W = 0, 30, 100, 300):
 *     the first INT1's time and sector. Then the same at double speed.
 *   BUFFER: whole sectors from 00:02:16, three INT1s acknowledged and not
 *     read, a Pause; then nothing, SeekL 00:06:16, Init, or ReadN at 00:06:16
 *     for one sector; then BFRD set and four bytes read: DRQSTS (1F801800h
 *     bit 6) and the header of whatever is there.
 *   NEW READ: during a single-speed read, D cycles before a sector is due,
 *     Setloc 00:06:16 and ReadN; every interrupt after it with its sector:
 *     does a sector of the old read still arrive after the new ReadN's INT3?
 *
 * READ IT LIKE THIS. GetlocP's absolute sector climbing one a sector time and
 * falling back by about a track is the circling; where it falls back to is
 * the hold. BUFFER rows that read the old header say a seek does NOT drop the
 * buffer.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js cd-head --disc --frames=12000
 * --quiet=6000`, 46 s here):
 *   IDLE     after Init 00:01:67..00:01:74, round and round (cdrom/getloc's
 *            console read 00:01:68 once), GetlocL refused (INT5 80h); after
 *            SeekL 00:02:16, 00:02:10..00:02:17 with GetlocL 00:02:16 (getloc's
 *            console: 00:02:11); after SeekL 00:30:00, 29:59:62..30:00:01; after
 *            reading to 00:02:31 and a Pause, 00:02:25..00:02:32, at either speed.
 *   RESUME   the first INT1 1.64M-3.36M cycles after the ReadN at single speed
 *            and 1.17M-2.09M at double, always 00:02:32: where on the circle
 *            the head is when the ReadN comes.
 *   BUFFER   after nothing, BFRD loads 00:02:18, the last sector read; after
 *            SeekL or Init, DRQSTS reads 1 and the bytes are zero: the buffer
 *            was dropped; after a one-sector ReadN elsewhere, that sector.
 *   NEW READ no sector of the old read arrives after the new ReadN's INT3; one
 *            can arrive between the Setloc's INT3 and the ReadN's.
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
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t data() { idx(0); return *CD2; }
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint32_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next interrupt: which, up to 8 response bytes, when; drained and acknowledged. For an INT1 with `hdr`, the
 * sector's header (BFRD, three bytes) instead. */
struct Got { uint8_t flag, n, b[8]; uint32_t t; };
static Got take(uint32_t limit = MS(3000), bool hdr = false) {
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
    if (g.flag == 1 && hdr) { want(true); g.b[1] = data(); g.b[2] = data(); g.b[3] = data(); want(false); }
    ackAll();
    return g;
}
static Got takeKind(uint8_t k, uint32_t limit = MS(3000), bool hdr = false) {
    for (;;) { const Got g = take(limit, hdr); if (!g.flag || g.flag == k || g.flag == 5) return g; }
}
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); command(0x02); takeKind(3);
}
static void setmode(uint8_t m) { ackAll(); param(m); command(0x0E); takeKind(3); }
static Got seekL(uint32_t lba) { setloc(lba); ackAll(); command(0x15); takeKind(3); return takeKind(2, MS(5000)); }
static void pauseAll() { ackAll(); command(0x09); for (int i = 0; i < 24; i++) { const Got g = take(MS(2000)); if (!g.flag || g.flag == 2) break; } idle(MS(1)); ackAll(); drain(); }
static void initDrive() { ackAll(); command(0x0A); take(); takeKind(2, MS(5000)); }
/* Read `count` sectors from `lba` (INT1s acknowledged, data not taken), then Pause. */
static void readThenPause(uint32_t lba, int count) {
    setloc(lba); ackAll(); command(0x06); take();
    for (int i = 0; i < count; i++) takeKind(1);
    pauseAll();
}
static int lbaOf(uint8_t m, uint8_t s, uint8_t f) { return (int)((unbcd(m) * 60 + unbcd(s)) * 75 + unbcd(f)) - 150; }

/* IDLE: GetlocP every 20 ms for 1.2 s; the absolute sector of each (or -1), then GetlocL. */
#define NIDLE 60
#define NOPOS (-100000)                                         /* below it: no position, the INT number */
static int idlePos[NIDLE]; static uint32_t idleAt[NIDLE]; static int idleN;
static Got locL;
static void pollIdle() {
    const uint32_t t0 = now();
    idleN = 0;
    while (idleN < NIDLE) {
        const uint32_t due = t0 + MS(20) * (uint32_t)idleN;
        while ((int32_t)(now() - due) < 0) {}
        ackAll(); const uint32_t tw = now(); command(0x11);
        const Got g = take();
        idleAt[idleN] = tw - t0;
        idlePos[idleN++] = (g.flag == 3 && g.n >= 8) ? lbaOf(g.b[5], g.b[6], g.b[7]) : NOPOS - g.flag;
    }
    ackAll(); command(0x10); locL = take();
}
static void msf(int lba) {
    if (lba <= NOPOS) { printf("INT%d", NOPOS - lba); return; }
    const int a = lba + 150;
    printf("%02d:%02d:%02d", a / 4500, (a / 75) % 60, a % 75);
}
static void showIdle(const char* name) {
    printf("  %s\n   ", name);
    int i = 0, col = 0;
    while (i < idleN) {
        int j = i + 1;
        while (j < idleN && idlePos[i] > NOPOS && idlePos[j] >= idlePos[j - 1] && idlePos[j] - idlePos[j - 1] <= 3) j++;
        printf(" "); msf(idlePos[i]);
        if (j - i > 1) { printf(".."); msf(idlePos[j - 1]); }
        printf("(%d)", j - i);
        if (++col % 5 == 0 && j < idleN) printf("\n   ");
        i = j;
    }
    printf("\n   GetlocL:");
    if (locL.flag == 3 && locL.n >= 3) { printf(" "); msf(lbaOf(locL.b[0], locL.b[1], locL.b[2])); }
    else printf(" INT%d %02x", locL.flag, locL.n > 1 ? locL.b[1] : 0);
    printf("\n");
}

struct Resume { uint32_t t; int lba; uint8_t flag; };
static Resume res[2][4];
static const uint32_t RESUME_W[] = { 0, 30, 100, 300 };
struct Buf { uint8_t drq, h[4]; };
static Buf buf[4];
struct NrEv { uint8_t flag; int lba; uint32_t t; };
static NrEv nr[12][8]; static int nrN[12]; static uint32_t nrD[12];

int main() {
    printf("\ncdrom/cd-head\n");
    printf("The idle head, reading on from a Pause, and the sector buffer after a seek. Needs a disc (32+ minutes for two rows).\n");
    printf("About half a minute; nothing prints until each part is done.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();

    printf("IDLE: GetlocP's absolute sector every 20 ms for 1.2 s, as runs of climbing sectors (count):\n");
    initDrive(); pollIdle(); showIdle("after Init");
    setmode(0x00); seekL(16); pollIdle(); showIdle("after SeekL 00:02:16");
    seekL(30 * 60 * 75 - 150); pollIdle(); showIdle("after SeekL 00:30:00");
    readThenPause(16, 16); pollIdle(); showIdle("after reading 00:02:16..00:02:31 and a Pause");
    setmode(0x80); idle(MS(1000));
    readThenPause(16, 16); pollIdle(); showIdle("the same at double speed");
    setmode(0x00); idle(MS(1000));
    printf("\n");

    for (int sp = 0; sp < 2; sp++) {                            /* RESUME */
        setmode(sp ? 0xA0 : 0x20); idle(MS(1000));             /* whole sectors: the FIFO starts with the header */
        for (int w = 0; w < 4; w++) {
            readThenPause(16, 16);
            idle(MS(RESUME_W[w]));
            setloc(32);
            ackAll(); const uint32_t t0 = now(); command(0x06); take();
            const Got g = takeKind(1, MS(3000), true);
            res[sp][w].flag = g.flag; res[sp][w].t = g.t - t0;
            res[sp][w].lba = g.flag == 1 ? lbaOf(g.b[1], g.b[2], g.b[3]) : NOPOS;
            pauseAll();
        }
    }
    setmode(0x00); idle(MS(1000));
    printf("RESUME: 00:02:16..00:02:31 read and Paused, then W ms after the Pause's INT2 Setloc 00:02:32 and ReadN:\n");
    printf("  the first INT1 after the ReadN's write, and its sector\n");
    for (int sp = 0; sp < 2; sp++) {
        printf("  %s:", sp ? "double" : "single");
        for (int w = 0; w < 4; w++) {
            printf("   W %3u: ", (unsigned)RESUME_W[w]);
            if (res[sp][w].flag == 1) { printf("%8u ", (unsigned)res[sp][w].t); msf(res[sp][w].lba); }
            else printf("INT%d", res[sp][w].flag);
        }
        printf("\n");
    }
    printf("\n");

    setmode(0x20);                                              /* BUFFER: whole sectors */
    for (int k = 0; k < 4; k++) {
        readThenPause(16, 3);
        if (k == 1) seekL(316);
        else if (k == 2) { initDrive(); setmode(0x20); }
        else if (k == 3) { setloc(316); ackAll(); command(0x06); take(); takeKind(1); pauseAll(); }
        idle(MS(5));
        want(true);
        buf[k].drq = (*CD0 >> 6) & 1;
        for (int i = 0; i < 4; i++) buf[k].h[i] = data();
        want(false);
    }
    printf("BUFFER: three sectors from 00:02:16 read and Paused (their data never taken), then an action, then BFRD:\n");
    static const char* const bn[4] = { "nothing", "SeekL 00:06:16", "Init", "ReadN 00:06:16, one sector, Pause" };
    for (int k = 0; k < 4; k++)
        printf("  %-34s DRQSTS %d, first bytes %02x:%02x:%02x mode %02x\n", bn[k], buf[k].drq, buf[k].h[0], buf[k].h[1], buf[k].h[2], buf[k].h[3]);
    printf("\n");

    setmode(0x20);                                              /* NEW READ */
    setloc(16); ackAll(); command(0x06); take();
    uint32_t t1 = takeKind(1).t, tl = t1;
    for (int i = 0; i < 8; i++) tl = takeKind(1).t;
    const uint32_t P = (tl - t1) / 8;
    int nn = 0;
    for (int32_t D = 0; D <= 110000 && nn < 12; D += 10000, nn++) {
        const Got s = takeKind(1);
        while ((int32_t)(now() - (s.t + P - (uint32_t)D)) < 0) {}
        const uint32_t a = 316 + 150 + (uint32_t)nn * 20;
        ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
        const uint32_t tw = now();
        command(0x02);                                          /* Setloc, then ReadN at its INT3 */
        int n = 0, sawSetloc = 0;
        const uint32_t start = now();
        while (n < 8 && now() - start < MS(400)) {
            const Got g = take(MS(400), true);
            if (!g.flag) break;
            nr[nn][n].flag = g.flag; nr[nn][n].t = g.t - tw;
            nr[nn][n].lba = g.flag == 1 ? lbaOf(g.b[1], g.b[2], g.b[3]) : NOPOS;
            n++;
            if (g.flag == 3 && !sawSetloc) { sawSetloc = 1; ackAll(); command(0x06); }
        }
        nrN[nn] = n; nrD[nn] = (uint32_t)D;
        pauseAll();                                             /* back to a read of 00:02:16 for the next trial */
        setloc(16); ackAll(); command(0x06); takeKind(3);
        takeKind(1); takeKind(1);
    }
    pauseAll();
    printf("NEW READ: single speed, a sector every %u; Setloc (then ReadN at its INT3) written D before a sector is due:\n", (unsigned)P);
    printf("  every interrupt after the Setloc's write: INTn@cycles and, for INT1, its sector\n");
    for (int i = 0; i < nn; i++) {
        printf("  D %6u:", (unsigned)nrD[i]);
        for (int k = 0; k < nrN[i]; k++) {
            printf(" %d@%u", nr[i][k].flag, (unsigned)nr[i][k].t);
            if (nr[i][k].flag == 1) { printf("="); msf(nr[i][k].lba); }
        }
        printf("\n");
    }

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ncdrom/cd-head: Done.\n");
    for (;;) VSync(0);
}
