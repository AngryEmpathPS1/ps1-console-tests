/*
 * cdrom/cd-speedpause - a Pause sent while the disc speeds up to double speed,
 * then a read: when does the first sector come?
 *
 * WHY (2026-09-30). Worms Armageddon (Europe) (Fr,Nl,Sv,No,Da) starts with
 * Setmode 80h, then 3 frames later Pause, Setmode A0h, Setloc 00:02:16 and
 * ReadN, then a Getstat every frame until the sector comes. On the SCPH-9001's
 * own program run as this emulator's drive (CDROMLLE) the sector never comes at
 * two of eight DRAM refresh phases (the game reads at the other six):
 *   - Init leaves a settle countdown ($61) during which the program does not
 *     look for a speed change (22D1h runs only when it is 0); meanwhile the idle
 *     hover's one-track jump (1BDBh) applies whatever speed the mode byte ($BB)
 *     holds, so the Setmode's double speed reaches the servo (CX 9F1000h) while
 *     the servo state ($6C) stays 0Ah;
 *   - the disc speeding up loses GFS (frame sync) in that state, and the
 *     program's lost-GFS recovery starts (2642h): 16 checks later it nudges the
 *     sled (CX 26h), 100 later it turns tracking OFF (CX 22h), and 200 more
 *     before it turns it back on;
 *   - when the countdown ends, the program enters its double-speed state (0Bh),
 *     where that recovery does not run, and that state waits for GFS (230Bh) -
 *     with tracking still off;
 *   - this emulator's servo model gives no GFS with tracking off, so the drive
 *     waits for ever: Getstat 02h, never 42h, no INT1.
 * So a real drive's GFS pin with tracking off is the open question. A spot left
 * at one radius still crosses tracks (one a revolution from the spiral, more from
 * the disc's runout) and can read frames near them. A model giving GFS for half
 * of each revolution freed Worms, but the program holds with tracking off in
 * ordinary running too, and that model moved nugget's SCPH-9001 race result at
 * two of four refresh phases and nine of 65 US discs on the program. Nothing
 * landed.
 * THIS TEST DOES NOT REACH THAT STATE here: neither its scans nor the game's own
 * command sequence (GAME rows) stall the program on The X-Files Disc 1, so the
 * stall needs the hover's jump at a narrower phase than these rows hit. What it
 * does measure is the sequence's cost on a console - a speed change with a Pause
 * inside it, then a read (REFERENCES 68: the speed change's real time is open) -
 * and a console row with no INT1, or a much later one, would show the real
 * drive meeting the same state.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled; Timer 2 at the system
 * clock, every 16-bit step added into 32 bits (as cd-readtoc). GAME rows: the
 * game's way there - a single-speed read of eight sectors and Pause, ReadTOC and
 * GetID as LoadExec sends them, Getstat, Init, Demute, a command a frame
 * (Getstat, Getstat, GetTN, Getstat, GetTN, GetTD 0, GetTD 1), then Setmode 80h
 * G ms after Init's INT2 (G = 79..119 in 5 ms steps; the game's was 99) and
 * Pause 55 ms after it. POLL rows: Init, Demute, Setmode 00h, 500 ms, Init
 * again, then Setmode 80h G ms after its INT2 (G = 0..400 in 20 ms steps) and
 * Pause 50 ms after. Each then: Setmode A0h, Setloc 00:02:16, ReadN, a Getstat
 * every 16.7 ms as the game sends them, up to 5 s for the first INT1, then
 * Pause. Any data disc; about 30 s, longer on a drive that stalls.
 *
 * READ IT LIKE THIS. Cycles at 33.8688 MHz (1 s = 33,868,800). The Pause's INT3
 * is timed from the Setmode, the first INT1 from the ReadN, and the stat after
 * the ReadN tells the drive's state (22h reading, 42h seeking, 02h idle - a read
 * that never started). "no INT1 in 5 s" is the stall above.
 *
 * ON THIS CORE AT v0.284, on The X-Files (USA) Disc 1 (the console run's disc;
 * add `--disc=<its image> --ntsc` to the run line below):
 *   THE MODEL: every row reads; the first INT1 0.599-0.605 s after the ReadN in
 *   the GAME rows and 0.603-0.611 s in the POLL rows (its 0.6 s speed change),
 *   stat 22h; the Pause's INT3 1.89-1.99M cycles after the Setmode.
 *   THE DRIVE'S OWN PROGRAM (PSX_CD_FIRMWARE=CDROM-firmware/SC430944.S19): every
 *   row reads; 0.606-0.617 s (GAME) and 0.570-0.631 s (POLL), stat 22h.
 * And on cputest's synthetic disc (`node hwtests/run.js cd-speedpause --disc
 * --frames=40000 --quiet=6000`): the model the same; the program 0.559-0.578 s
 * (GAME) and 0.519-0.599 s (POLL) - its spin-up follows the disc's geometry.
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
static uint8_t bcd(uint32_t v) { return ((v / 10) << 4) | (v % 10); }

/* Wrap-safe: every loop that waits calls now(), far more often than once per 65,536 cycles. */
static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

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
static Got simple(uint8_t c, bool second = false) {
    ackAll();
    command(c);
    Got a = take();
    if (second && a.flag == 3) return take(MS(8000));
    return a;
}
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}

struct Row { uint32_t g, d; bool poll, replica; Got pause3, pause2, readAck, first; uint32_t tSet, tRead, polls; uint8_t lastStat; };
static Row rows[64];

/* The game's own way there (Worms Armageddon (Europe) (Fr,Nl,Sv,No,Da) on v3.0, its CD log): the BIOS reads the EXE
 * at single speed and pauses, LoadExec sends ReadTOC and GetID, then the game: Getstat, Init, Demute, a command a
 * frame (Getstat, Getstat, GetTN, Getstat, GetTN, GetTD 0, GetTD 1), and Setmode 80h 99 ms after Init's INT2. */
static uint32_t replicaPreamble() {
    param(0x00); simple(0x0E);
    setloc(16); simple(0x15, true);                  /* SeekL 00:02:16 */
    ackAll(); command(0x06); take();                 /* ReadN */
    for (int i = 0; i < 8; i++) take();              /* eight sectors, acknowledged */
    simple(0x09, true);                              /* Pause */
    ackAll(); command(0x1E); if (take().flag == 3) take(MS(10000));   /* ReadTOC, as LoadExec sends it */
    simple(0x1A, true);                              /* GetID */
    simple(0x01);                                    /* Getstat */
    const Got init2 = simple(0x0A, true);            /* Init, and its INT2 */
    simple(0x0C);                                    /* Demute */
    idle(MS(17)); simple(0x01); idle(MS(17)); simple(0x01); idle(MS(17)); simple(0x13);
    idle(MS(17)); simple(0x01); idle(MS(17)); simple(0x13);
    param(0); simple(0x14); param(1); simple(0x14);   /* GetTD 0, GetTD 1 */
    return init2.t;
}

static void run(Row &r) {
    if (r.replica) {
        const uint32_t t2 = replicaPreamble();
        while (now() - t2 < MS(r.g)) {}
    } else {
        simple(0x0A, true);                          /* Init, and its INT2 */
        simple(0x0C);                                /* Demute */
        param(0x00); simple(0x0E);                   /* Setmode 00h: single speed */
        idle(MS(500));
        simple(0x0A, true);                          /* Init again: the game's Setmode comes G ms after one */
        idle(MS(r.g));
    }
    param(0x80); ackAll(); r.tSet = now(); command(0x0E); take();   /* Setmode 80h: the speed change */
    while (now() - r.tSet < MS(r.d)) {}
    ackAll(); command(0x09);                         /* Pause */
    r.pause3 = take();
    r.pause2 = r.pause3.flag == 3 ? take(MS(3000)) : Got{ 0, 0, 0 };
    param(0xA0); simple(0x0E);                       /* Setmode A0h */
    setloc(16);                                      /* 00:02:16 */
    ackAll(); r.tRead = now(); command(0x06);        /* ReadN */
    r.readAck = take();
    r.polls = 0; r.lastStat = r.readAck.stat; r.first = Got{ 0, 0, 0 };
    while (now() - r.tRead < MS(5000)) {
        const uint32_t tw = now();
        Got g = Got{ 0, 0, 0 };
        while (now() - tw < MS(16) + 23000) {          /* a frame: 16.7 ms */
            const uint8_t f = irqFlag();
            if (f) { g.flag = f; g.t = now(); if (hasResp()) g.stat = resp(); drain(); ackAll(); break; }
        }
        if (g.flag == 1) { r.first = g; break; }
        if (r.poll && !g.flag) {
            ackAll(); command(0x01);                 /* Getstat, as the game sends it */
            Got s = take(MS(100));
            r.polls++;
            if (s.flag == 1) { r.first = s; break; }
            if (s.flag == 3) r.lastStat = s.stat;
        }
    }
    simple(0x09, true);                              /* Pause, and its INT2 */
}

int main() {
    printf("\ncdrom/cd-speedpause\n");
    printf("A Pause while the disc speeds up to double speed, then a read: the first sector's time. Needs a disc. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();

    int n = 0;
    for (uint32_t g = 79; g <= 119; g += 5) { Row &r = rows[n++]; r.g = g; r.d = 55; r.poll = true; r.replica = true; run(r); }
    for (uint32_t g = 0; g <= 400; g += 20) { Row &r = rows[n++]; r.g = g; r.d = 50; r.poll = true; r.replica = false; run(r); }
    param(0x00); simple(0x0E);

    write16(T2_MODE, m2);
    ExitCriticalSection();

    for (int i = 0; i < n; i++) {
        const Row &r = rows[i];
        printf("%s G=%3u D=%3u ms: Pause INT%d %8u", r.replica ? "GAME " : r.poll ? "POLL " : "QUIET", (unsigned)r.g, (unsigned)r.d, r.pause3.flag, (unsigned)(r.pause3.t - r.tSet));
        if (r.pause2.flag) printf(" INT%d %8u after (%02x)", r.pause2.flag, (unsigned)(r.pause2.t - r.pause3.t), r.pause2.stat);
        printf("; ReadN INT%d (%02x)", r.readAck.flag, r.readAck.stat);
        if (r.first.flag == 1) printf("; first INT1 %9u after it (%02x)", (unsigned)(r.first.t - r.tRead), r.first.stat);
        else printf("; no INT1 in 5 s (stat %02x after %u Getstats)", r.lastStat, (unsigned)r.polls);
        printf("\n");
    }
    printf("Done.\n");
    for (;;) {}
}
