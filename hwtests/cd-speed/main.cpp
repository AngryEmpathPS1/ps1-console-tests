/*
 * cdrom/cd-speed - how long does the drive take to change speed, and what does
 * it do while it changes?
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, CD row C32; ROAD B). Setmode's bit 7
 * picks single or double speed, and the spindle has to change its speed to
 * match: the disc turns at a constant linear velocity, a sector every 75th or
 * 150th of a second wherever the head is. This core charges the change to the
 * next head move alone. A SeekL, SeekP, ReadN, ReadS or Play that starts
 * before the change is done pays what is left of 0.6 s (to double speed) or
 * 0.7 s (to single), and a flip back that catches a change less than three
 * quarters done cancels it. Those are DuckStation's GetTicksForSpeedChange and
 * its Setmode case, constants with no source given in its code. M25 took them
 * from DuckStation's log of FF7's boot, which prints DuckStation's own model.
 * DuckStation set them by games (REFERENCES 68): 1.5 s in 2020 ("Approximate
 * time for the motor to change speed?"), 0.1 s, 0.8 / 1.0 s in 2021, 0.7 /
 * 0.8 s in 2024 for World Cup 98's music, 0.6 / 0.7 s three days later for
 * "leaky reverb". MiSTer keeps the 2021 pair; mednafen and Redux charge none.
 * A read already under way changes its sector rate at once here, where
 * DuckStation holds its next sector back by the same 0.6 or 0.7 s. The two
 * disagree, and nothing has measured either.
 *
 * Sony says something else again, twice, in LibOver 4.7:
 *   - CD/Streaming Library, "Warnings Regarding Changing the Motor Speed": a
 *     head move (SeekL/P, ReadN/S, Play) issued right after a speed change can
 *     lose control of the head, which then runs to its mechanical stopper - no
 *     damage, Sony says, but "a strange sound". "If a command to move the CD
 *     head is to be issued after a change in CD transfer speed, always leave
 *     an interval of at least three vsyncs."
 *   - Extended CD-ROM Library, "Transfer Speed Change": "it will be impossible
 *     to execute commands for approximately three frames", and libds holds
 *     every command for 3 VSyncs after one.
 * Three frames is 50 ms, a twelfth of 0.6 s.
 *
 * A console's boot tells them apart with no program at all: BIOS 4.1 changes
 * speed three times before a game runs, and FF7's licence screen lasts 6.89 s
 * under this core's rule and 5.71 s under three frames (tools/bootscreens.js).
 *
 * ps1-tests cdrom/timing makes a speed change before every double-speed read
 * (Setmode 80h, Setloc, ReadN), so its log has the answer in it - but its
 * timer adds FFFFh once on an overflow, and past 1,048,560 cycles it reads
 * modulo 524,288. Its first double-speed "First Sector", 553,960, is 29,680 +
 * 524,288k cycles, and DuckStation's 0.6 s (k = 40) and Sony's three frames
 * (k = 5) fit it alike. Its other four double-speed runs follow a flip to
 * single speed and straight back, which DuckStation's cancel rule makes free.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at the system
 * clock, unwrapped in software as cd-init does. Every head move here comes at
 * least 50 ms (three frames) after the last Setmode, as Sony asks. The head
 * parks at 00:02:16 and moves 300 sectors on (00:06:16), so any data disc
 * does. About five minutes, and nothing prints until the end.
 *   CONTROL  SeekL 00:02:16 -> 00:06:16 with no speed change, three times at
 *            each speed: the seek alone.
 *   SEEK     the drive settled 1.5 s at one speed, the head parked; Setmode
 *            flips the speed; D ms after its INT3, SeekL 00:06:16, timed from
 *            the write to its INT2. D = 50 ... 1,500 ms, both directions,
 *            twice each. Sony's reading is the seek alone at every D. This
 *            core's and DuckStation's is the seek plus 0.6 s - D, until D
 *            passes 0.6 s.
 *   READ     the same with ReadN and its first INT1, D = 50, 200, 400, 700.
 *   STAT     Setmode flips the speed, then Getstat back to back for 1 s: each
 *            one's INT3 time and stat byte, printed as runs of the same stat.
 *            libds's three frames would show as slow acknowledges, a spindle
 *            "in spin-up phase" as stat bit 1 clear (psx-spx: "0=Motor off, or
 *            in spin-up phase").
 *   CANCEL   flip, wait F, flip back, wait 100 ms, SeekL: F = 2 ms (the pair
 *            cdrom/timing makes between its double-speed runs), 50, 150, 300
 *            and 500 ms, both ways round. This core charges nothing while the
 *            first change is less than three quarters done (the second flip
 *            cancels it), and the whole reverse change after that. DuckStation
 *            lets the first change run out there instead, and charges what is
 *            left of it.
 *   INIT     Init from double speed with the motor on, settled 1.5 s, and
 *            from single speed as the control: Init's INT3 from its write and
 *            INT2 from the INT3. Init puts the drive back at single speed, and
 *            this core charges the 0.7 s change there (DuckStation's
 *            SoftReset): 23.7M of FF7's first Init. cd-init times Init from
 *            double speed with the motor stopped.
 *   DURING   ReadN at one speed (whole sectors, Setmode 20h or A0h), and after
 *            10 sectors a Setmode that flips the speed under the read; every
 *            interrupt for 2 s, each INT1 with its sector's header, printed as
 *            runs of consecutive sectors at an even spacing, so a gap, a
 *            repeat or a skip shows. Both directions, twice. It is last: Sony's
 *            note is about head moves, and this is not one, but it changes the
 *            speed under a head that is reading.
 *
 * READ IT LIKE THIS. Cycles at 33,868,800 a second (1 ms = 33,869; a frame
 * 564,480). A SEEK row that falls by D until D reaches some S and is flat
 * after says the change takes S. A flat SEEK table says it takes no longer
 * than 50 ms, or that the drive waits for it in its own time.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js cd-speed --disc --frames=26000
 * --quiet=24000`, cputest's synthetic disc; 3 min 44 s here):
 *   CONTROL  1.78-2.00M, the 55 ms seek and its landing draw.
 *   SEEK     to double 20.3M at D 50, falling 1.69M every 50 ms, the control
 *            from D 600 on; to single 23.7M at D 50, the control from D 700.
 *   READ     the same plus a sector: 20.6M / 24.2M at D 50, 2.1M / 2.4M at 700.
 *   STAT     every Getstat 02h, acknowledged in 24,114-136,754.
 *   CANCEL   out and back within 300 ms costs nothing. Single -> double ->
 *            single at F 500 pays the whole 0.7 s back (22.1M); double ->
 *            single -> double at F 500 is still cancelled (a quarter of the
 *            0.6 s change was left). DuckStation's model at F 500, single
 *            first: about the seek alone, as the first change runs out when
 *            the SeekL is sent.
 *   INIT     from double, INT2 23.7M after its INT3 (25.7M when the head was
 *            away from the start and seeks back); from single 476,312.
 *   DURING   the rate changes at the next sector, with no gap, repeat or skip:
 *            435-470k apart before and 202-251k after, to double; 213-241k
 *            and then 427-475k, to single.
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

/* Wrap-safe: every loop that waits calls now(), far more often than once per 65,536 cycles. */
static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }
static void until(uint32_t t0, uint32_t cycles) { while (now() - t0 < cycles) {} }

struct Got { uint8_t flag, stat; uint32_t t; };
/* Wait for any flag (up to `limit` cycles), note when, read its first response byte, drain, acknowledge. */
static Got take(uint32_t limit = MS(3000)) {
    Got g = { 0, 0, 0 };
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
    if (hasResp()) g.stat = resp();
    drain();
    ackAll();
    return g;
}
/* The next INT of kind `want` (an INT5 ends the wait too), acknowledging anything else on the way. */
static Got takeKind(uint8_t kind, uint32_t limit) {
    const uint32_t t0 = now();
    for (;;) {
        const Got g = take(limit);
        if (!g.flag || g.flag == kind || g.flag == 5) return g;
        if (now() - t0 > limit) return Got{ 0, 0, now() };
    }
}

static uint8_t curMode = 0x20;
static Got setmode(uint8_t m) { ackAll(); param(m); command(0x0E); curMode = m; return take(); }
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); command(0x02); take();
}
/* The mode `m` with the spindle settled at its speed: a flip waits 1.5 s. */
static void settleAt(uint8_t m) {
    const bool flip = ((m ^ curMode) & 0x80) != 0;
    if (m != curMode) setmode(m);
    if (flip) idle(MS(1500));
}

struct Row { uint32_t t; uint8_t flag, stat; };
/* SeekL to the Setloc'd target: the write -> its INT2 (flag 5 for an error, 0 for a timeout). */
static Row seekL() {
    ackAll();
    const uint32_t t0 = now();
    command(0x15);
    const Got a = take();
    if (a.flag != 3) return Row{ a.t - t0, a.flag, a.stat };
    const Got b = takeKind(2, MS(5000));
    return Row{ b.t - t0, b.flag, b.stat };
}
/* ReadN at the Setloc'd target: the write -> its first INT1; then Pause, and its INT2. */
static Row readN() {
    ackAll();
    const uint32_t t0 = now();
    command(0x06);
    const Got a = take();
    Row r;
    if (a.flag != 3) r = Row{ a.t - t0, a.flag, a.stat };
    else { const Got b = takeKind(1, MS(5000)); r = Row{ b.t - t0, b.flag, b.stat }; }
    ackAll(); command(0x09);
    for (int i = 0; i < 16; i++) { const Got g = take(MS(2000)); if (!g.flag || g.flag == 2) break; }
    idle(MS(2)); ackAll(); drain();
    return r;
}

static const uint32_t ORIGIN = 16, TARGET = 316;       /* 00:02:16 and 00:06:16 */
/* The drive settled at `m`, the head parked at ORIGIN, Setloc on TARGET. */
static void ready(uint8_t m) {
    settleAt(m);
    setloc(ORIGIN); seekL();
    idle(MS(300));
    setloc(TARGET);
}

static const uint32_t SEEK_D[] = { 50, 100, 150, 200, 300, 400, 500, 600, 700, 800, 1000, 1500 };
static const uint32_t READ_D[] = { 50, 200, 400, 700 };
static const uint32_t CANCEL_F[] = { 2, 50, 150, 300, 500 };
#define NSEEK (sizeof(SEEK_D) / sizeof(SEEK_D[0]))
#define NREAD (sizeof(READ_D) / sizeof(READ_D[0]))
#define NCANCEL (sizeof(CANCEL_F) / sizeof(CANCEL_F[0]))

static Row control[2][3];
static Row seekRows[2][NSEEK][2], readRows[2][NREAD][2], cancelRows[2][NCANCEL][2];
static Got initA[2][2], initB[2][2]; static uint32_t initT0[2][2];

struct StatEv { uint32_t t, ack; uint8_t stat; };
#define NSTAT 900
static StatEv sev[4][NSTAT]; static int sevN[4];

struct Ev { uint32_t t; uint8_t flag, b0, m, s, f; };
#define NEV 360
static Ev dev[4][NEV]; static int devN[4]; static uint32_t devFlip[4];

/* Every interrupt from `t0` for `cycles`: INT1s with their sector's header (BFRD set, three bytes, dropped). */
static int logRead(Ev *out, int n, int max, uint32_t t0, uint32_t cycles) {
    const uint32_t s = now();
    while (now() - s < cycles && n < max) {
        const uint8_t f = irqFlag();
        if (!f) continue;
        Ev &e = out[n++];
        e.t = now() - t0; e.flag = f; e.m = e.s = e.f = 0;
        e.b0 = hasResp() ? resp() : 0xEE;
        if (f == 1) { want(true); e.m = data(); e.s = data(); e.f = data(); want(false); }
        drain(); ackAll();
    }
    return n;
}

static void showRow(const Row &r) {
    if (r.flag == 2 || r.flag == 1) printf(" %9u", (unsigned)r.t);
    else printf("   INT%d/%02x", r.flag, r.stat);
}

/* A signed cycle count as text: the kernel's printf is not known to take the '+' flag. */
static const char *sgn(int32_t v, char *buf) {
    uint32_t a = v < 0 ? (uint32_t)-v : (uint32_t)v;
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + a % 10); a /= 10; } while (a);
    int k = 0;
    buf[k++] = v < 0 ? '-' : '+';
    while (n) buf[k++] = tmp[--n];
    buf[k] = 0;
    return buf;
}

static int lbaOf(const Ev &e) { return (int)((unbcd(e.m) * 60 + unbcd(e.s)) * 75 + unbcd(e.f)) - 150; }
static void showRead(int k) {
    const Ev *ev = dev[k];
    const int n = devN[k];
    const int32_t flip = (int32_t)devFlip[k];
    int i = 0;
    while (i < n) {
        if (ev[i].flag != 1) {
            char b1[16];
            printf("    INT%d (%02x) at %s\n", ev[i].flag, ev[i].b0, sgn((int32_t)ev[i].t - flip, b1));
            i++;
            continue;
        }
        /* A run: consecutive sectors, each spacing within a quarter of the run's first. */
        int j = i + 1;
        uint32_t lo = 0xFFFFFFFFu, hi = 0, first = 0;
        while (j < n && ev[j].flag == 1 && lbaOf(ev[j]) == lbaOf(ev[j - 1]) + 1) {
            const uint32_t sp = ev[j].t - ev[j - 1].t;
            if (j == i + 1) first = sp;
            else if (sp * 4 < first * 3 || sp * 4 > first * 5) break;
            lo = sp < lo ? sp : lo; hi = sp > hi ? sp : hi;
            j++;
        }
        char b1[16], b2[16];
        printf("    INT1 x%d %02x:%02x:%02x..%02x:%02x:%02x at %s..%s", j - i,
               ev[i].m, ev[i].s, ev[i].f, ev[j - 1].m, ev[j - 1].s, ev[j - 1].f,
               sgn((int32_t)ev[i].t - flip, b1), sgn((int32_t)ev[j - 1].t - flip, b2));
        if (j - i > 1) printf("  every %u..%u", (unsigned)lo, (unsigned)hi);
        printf("\n");
        i = j;
    }
}

static void showStat(int k) {
    const StatEv *e = sev[k];
    const int n = sevN[k];
    int i = 0, slow = 0;
    while (i < n) {
        int j = i;
        uint32_t lo = 0xFFFFFFFFu, hi = 0;
        while (j < n && e[j].stat == e[i].stat) { lo = e[j].ack < lo ? e[j].ack : lo; hi = e[j].ack > hi ? e[j].ack : hi; j++; }
        printf("    %9u..%9u  %3d x stat %02x, acknowledged in %u..%u\n", (unsigned)e[i].t, (unsigned)e[j - 1].t, j - i, e[i].stat, (unsigned)lo, (unsigned)hi);
        i = j;
    }
    for (i = 0; i < n && slow < 8; i++)
        if (e[i].ack > MS(5)) { printf("    slow: the Getstat written at %u acknowledged after %u\n", (unsigned)e[i].t, (unsigned)e[i].ack); slow++; }
}

int main() {
    printf("\ncdrom/cd-speed\n");
    printf("The CD drive's speed change. Needs a disc. About five minutes; nothing prints until the end.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    ackAll(); command(0x0A); take(); takeKind(2, MS(5000));     /* Init: single speed, mode 20h */
    curMode = 0x20;
    ackAll(); command(0x0C); take();                            /* Demute */
    idle(MS(1000));

    for (int sp = 0; sp < 2; sp++) {                            /* CONTROL */
        const uint8_t m = sp ? 0x80 : 0x00;
        for (int k = 0; k < 3; k++) { ready(m); control[sp][k] = seekL(); }
    }
    for (int dir = 0; dir < 2; dir++) {                         /* SEEK: 0 = to double, 1 = to single */
        const uint8_t from = dir ? 0x80 : 0x00, to = dir ? 0x00 : 0x80;
        for (unsigned d = 0; d < NSEEK; d++)
            for (int k = 0; k < 2; k++) {
                ready(from);
                const Got g = setmode(to);
                until(g.t, MS(SEEK_D[d]));
                seekRows[dir][d][k] = seekL();
            }
    }
    for (int dir = 0; dir < 2; dir++) {                         /* READ */
        const uint8_t from = dir ? 0x80 : 0x00, to = dir ? 0x00 : 0x80;
        for (unsigned d = 0; d < NREAD; d++)
            for (int k = 0; k < 2; k++) {
                ready(from);
                const Got g = setmode(to);
                until(g.t, MS(READ_D[d]));
                readRows[dir][d][k] = readN();
            }
    }
    for (int k = 0; k < 4; k++) {                               /* STAT: to double, to single, twice */
        const uint8_t from = (k & 1) ? 0x80 : 0x00, to = (k & 1) ? 0x00 : 0x80;
        settleAt(from);
        idle(MS(1500));
        const Got g = setmode(to);
        int n = 0;
        while (now() - g.t < MS(1000) && n < NSTAT) {
            ackAll();
            const uint32_t w = now();
            command(0x01);
            const Got a = take(MS(500));
            sev[k][n].t = w - g.t;
            sev[k][n].ack = a.flag ? a.t - w : 0xFFFFFFFFu;
            sev[k][n].stat = a.flag == 3 ? a.stat : (uint8_t)(0xE0 | a.flag);
            n++;
        }
        sevN[k] = n;
    }
    for (int dir = 0; dir < 2; dir++) {                         /* CANCEL: out and back */
        const uint8_t a = dir ? 0x80 : 0x00, b = dir ? 0x00 : 0x80;
        for (unsigned f = 0; f < NCANCEL; f++)
            for (int k = 0; k < 2; k++) {
                ready(a);
                const Got g = setmode(b);
                until(g.t, MS(CANCEL_F[f]));
                const Got h = setmode(a);
                until(h.t, MS(100));
                cancelRows[dir][f][k] = seekL();
            }
    }
    for (int dbl = 1; dbl >= 0; dbl--)                          /* INIT: from double, then single */
        for (int k = 0; k < 2; k++) {
            settleAt(dbl ? 0x80 : 0x00);
            idle(MS(1500));
            ackAll();
            initT0[dbl][k] = now();
            command(0x0A);
            initA[dbl][k] = take();
            initB[dbl][k] = initA[dbl][k].flag == 3 ? takeKind(2, MS(5000)) : Got{ 0, 0, 0 };
            curMode = 0x20;
        }
    for (int k = 0; k < 4; k++) {                               /* DURING: to double, to single, twice */
        const uint8_t from = (k & 1) ? 0xA0 : 0x20, to = (k & 1) ? 0x20 : 0xA0;
        settleAt(from);
        idle(MS(1500));
        setloc(ORIGIN);
        ackAll();
        const uint32_t t0 = now();
        command(0x06);                                          /* ReadN */
        int n = 0, ints = 0;
        while (ints < 10 && n < NEV) {                          /* up to its 10th INT1 */
            const int m = logRead(dev[k], n, n + 1, t0, MS(3000));
            if (!m || m == n) break;
            n = m;
            if (dev[k][n - 1].flag == 1) ints++;
        }
        ackAll();
        devFlip[k] = now() - t0;
        param(to); command(0x0E); curMode = to;                 /* Setmode under the read */
        n = logRead(dev[k], n, NEV, t0, MS(2000));
        devN[k] = n;
        ackAll(); command(0x09);                                /* Pause */
        for (int i = 0; i < 16; i++) { const Got g = take(MS(2000)); if (!g.flag || g.flag == 2) break; }
        idle(MS(2)); ackAll(); drain();
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("Cycles at the system clock (1 ms = 33,869). Head moves from 00:02:16 to 00:06:16.\n");
    printf("CONTROL, no speed change, SeekL write -> INT2:\n");
    for (int sp = 0; sp < 2; sp++) {
        printf("  %s", sp ? "double" : "single");
        for (int k = 0; k < 3; k++) showRow(control[sp][k]);
        printf("\n");
    }
    for (int dir = 0; dir < 2; dir++) {
        printf("SEEK %s: Setmode, D ms after its INT3 SeekL, write -> INT2 (twice):\n", dir ? "DOUBLE -> SINGLE" : "SINGLE -> DOUBLE");
        for (unsigned d = 0; d < NSEEK; d++) {
            printf("  D %4u", (unsigned)SEEK_D[d]);
            showRow(seekRows[dir][d][0]); showRow(seekRows[dir][d][1]);
            printf("\n");
        }
    }
    for (int dir = 0; dir < 2; dir++) {
        printf("READ %s: Setmode, D ms after its INT3 ReadN, write -> first INT1 (twice):\n", dir ? "DOUBLE -> SINGLE" : "SINGLE -> DOUBLE");
        for (unsigned d = 0; d < NREAD; d++) {
            printf("  D %4u", (unsigned)READ_D[d]);
            showRow(readRows[dir][d][0]); showRow(readRows[dir][d][1]);
            printf("\n");
        }
    }
    printf("STAT: Getstat back to back for 1 s after a Setmode's INT3 (times from that INT3):\n");
    for (int k = 0; k < 4; k++) {
        printf("  %s:\n", (k & 1) ? "to single" : "to double");
        showStat(k);
    }
    for (int dir = 0; dir < 2; dir++) {
        printf("CANCEL %s: Setmode, F ms, Setmode back, 100 ms, SeekL write -> INT2 (twice):\n", dir ? "DOUBLE -> SINGLE -> DOUBLE" : "SINGLE -> DOUBLE -> SINGLE");
        for (unsigned f = 0; f < NCANCEL; f++) {
            printf("  F %4u", (unsigned)CANCEL_F[f]);
            showRow(cancelRows[dir][f][0]); showRow(cancelRows[dir][f][1]);
            printf("\n");
        }
    }
    printf("INIT, 1.5 s settled (INT3 from the write, INT2 from the INT3):\n");
    for (int dbl = 1; dbl >= 0; dbl--)
        for (int k = 0; k < 2; k++) {
            const Got &a = initA[dbl][k], &b = initB[dbl][k];
            printf("  from %s  INT%d %8u (%02x)", dbl ? "double" : "single", a.flag, (unsigned)(a.t - initT0[dbl][k]), a.stat);
            if (b.flag) printf("   INT%d %9u after it (%02x)\n", b.flag, (unsigned)(b.t - a.t), b.stat);
            else printf("   no second response\n");
        }
    printf("DURING: ReadN of whole sectors from 00:02:16, Setmode after the 10th INT1 (times from its write):\n");
    for (int k = 0; k < 4; k++) {
        printf("  %s:\n", (k & 1) ? "double -> single" : "single -> double");
        showRead(k);
    }
    printf("\ncdrom/cd-speed: Done.\n");
    for (;;) VSync(0);
}
