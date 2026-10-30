/*
 * cdrom/cd-init - how long does Init take, from each state the drive can be
 * in?
 *
 * WHY (ROAD A8 "Init's spread", dug 2026-09-27; ROAD B). Init answers twice:
 * INT3 when it has been taken and INT2 when the drive has settled. The first
 * is settled as a range (nocash's minimum 63,520 cycles; ps1-tests' console
 * 69,456-93,696). The second is not, and two consoles are 8x apart:
 *   ps1-tests cdrom/timing, 100 Inits back to back after libcd's CdInit:
 *     138,448 - 600,632 cycles (average 476,300 - this core's number). Its
 *     timer adds FFFFh once on an overflow, so only the minimum is exact.
 *   nugget tests/cdrom cdlinit.c, SCPH-9001, one Init early in its suite:
 *     50-150 ms, "around 120 ms" (~4.06M cycles, every emulator's ~4M),
 *     "may be a bit flaky ... depending on the motors status".
 * Different preceding states, different consoles, different timers. This
 * test runs Init from each state on one console with one wrap-safe clock.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled; Timer 2 at the system
 * clock, every 16-bit step added into 32 bits by the polling loops (no wrap
 * for 126 s). For every Init: the INT3 from the command write and the INT2
 * from the INT3 (as nugget measures), with each one's stat.
 *   FIRST    the first Init after the boot, whatever the BIOS left
 *   BACK     12 Inits back to back, each written when the last one's INT2 is
 *            acknowledged (ps1-tests' order)
 *   SETTLED  6 Inits each after 1 s idle (the motor on, nothing reading)
 *   STOPPED  3 times: Stop (08h) at single speed and its INT2, Stop again
 *            (the motor already stopped), 2 s idle, then Init - a spin-up
 *            inside Init's second response or not; and twice at DOUBLE speed
 *            (Setmode A0h, 1 s, Stop, 2 s, Init). psx-spx's table (measured on
 *            a PAL PSone) gives Stop's second response 0D38ACAh (0.41 s) at
 *            single speed, 18A6076h (0.76 s) at double, 1D7Bh when stopped
 *   MOTORON  twice: Stop, 2 s idle, MotorOn (07h) - whose second response
 *            no measurement gives (psx-spx: "spin-up time if the motor was
 *            off"; DuckStation answers after 400,000 cycles, uncited)
 *   READING  3 times: Setloc 00:02:16, ReadN, 8 sectors, then Init mid-read
 *   STOP MID-READ twice, the same with Stop: psx-spx says its first response
 *            carries the status "already with bit5 cleared" (02h, not 22h);
 *            DuckStation, mednafen and this core send the stat they found
 *   HELD     3 times (nugget's cdlInitDelayed): Init's INT3 left
 *            unacknowledged for 500 ms, then acknowledged - the INT2 time from
 *            the acknowledge, and whether any other flag appeared meanwhile
 *   STOP THEN INIT  twice (added 2026-09-28): a Stop mid-read at single speed,
 *            GetTN 133 ms (8 frames) later, and Init as soon as GetTN answers,
 *            with EVERY interrupt logged for 3 s from the Stop's write. That is
 *            NFL GameDay 2001's loader (and NCAA GameBreaker 2001's): libcd's
 *            Stop, then its own code's GetTN and Init. psx-spx gives the
 *            Stop's second response 0.41 s at single speed, so the Init lands
 *            inside it, where psx-spx says a new command's "results would be
 *            unknown, and probably totally unpredictable". DuckStation and
 *            this core cancel the Stop's INT2 (ClearCommandSecondResponse), so
 *            libcd never sees it complete, and the loader's later CdSync waits
 *            out its 960-frame timeout: 16 s still. A console shows whether
 *            the INT2 comes, and when: before GetTN's INT3, between, or never.
 * It needs a disc in the drive (any data disc). About 25 seconds.
 *
 * READ IT LIKE THIS. Cycles at 33.8688 MHz (1 ms = 33,869). A console that
 * reads ~4M in FIRST or STOPPED and ~0.5M in BACK has both consoles' numbers
 * in one run: the state decides, and this core (476,300 flat plus a seek)
 * needs a state term.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js cd-init --disc --frames=8000
 * --quiet=3000`, cputest's synthetic disc): Init's INT3 81,127-81,160 after
 * the write and its INT2 476,288-476,329 after that in FIRST, BACK, SETTLED
 * and STOPPED alike - M18's average of ps1-tests' log, flat, with no spin-up
 * anywhere. READING: INT3 22h (the stat it found, M238), INT2 1.82-1.93M (the
 * head parked from 00:02:24: a seek). After a DOUBLE-speed Stop, 23.7M: the
 * 0.7 s speed settle, charged with the motor stopped (DuckStation's
 * SoftReset charges it the same way). Stop's INT2: 13,863,588-13,863,641 at
 * single speed, 25,845,852-25,845,880 at double, 7,512-7,564 when stopped -
 * psx-spx's table since M259; at v0.258 every Stop read 7,636-7,707 (the
 * stopped row), which is how this test found it. Stop mid-read answers 22h.
 * MotorOn's INT2 7,620-7,672. HELD: no other flag while held; the INT2
 * 980-46,651 after the acknowledge over three runs (the sub-CPU loop's phase,
 * M122) - nugget's console, 100-1,000 us (3,387-33,869).
 * STOP THEN INIT at v0.260 (`--frames=9500`, the earlier rows unchanged): the
 * Stop's INT3 (22h) at 49,886 / 72,774; GetTN written at 4,504,810, its INT3
 * at 4,529,146; Init's INT3 at 4,610,730 and its INT2 at 6,456,578 (6,304,038)
 * - four interrupts, and NO Stop INT2 anywhere: the Init cancelled it.
 * THE DRIVE'S OWN PROGRAM (M281's CDROMLLE, 2026-09-30, `--frames=30000
 * --quiet=20000`, scph7001, NTSC; its times on src/cdservo.js's mechanics, which
 * are ASSUMPTIONS): BACK 4.36-4.39M (the 9001's "around 120 ms"), SETTLED
 * 1.69-1.71M, STOPPED 43.9-56.6M (a spin-up), MotorOn after a Stop 44.0-56.7M,
 * READING 0.91-0.95M; Stop's INT2 13.13-13.26M single, 25.40-25.49M double; Stop
 * mid-read answers its INT3 WITHOUT the read bit, as psx-spx says (so do Stop
 * while seeking or playing: every activity bit cleared); HELD INT2 8,215-64,020
 * after the acknowledge; STOP THEN INIT: four interrupts, NO Stop INT2 - the
 * Init cancels it, as DuckStation and this core do - and the Init's INT2 1.6 s
 * after it, the disc spun back up.
 * AT v0.282 THIS CORE FOLLOWS THE PROGRAM'S LOGIC there: Stop's INT3 02h from any
 * activity, the motor bit set until Stop's INT2, and Init or MotorOn from a
 * stopped (or braking) disc answering with the bit clear and completing after
 * CD_MOTOR_SPINUP (1 s): STOPPED 34.3M, STOP THEN INIT's Init INT2 at 40.2-40.4M.
 * ITS RUN, SINCE THEN (`node hwtests/run.js cd-init --disc --frames=8000 --quiet=6000`,
 * 2026-10-04, v0.355): it measures every section in silence and prints them all at
 * the end, and M282's spin-ups made that silence 3,134 frames (frame 125 to 3,259 on
 * BIOS 4.1, NTSC; the end at 3,270) - past the 3,000 quiet frames of the recipe
 * above, so tools/consolegrade.js's reference run stopped with the title alone from
 * 2026-09-30 to 2026-10-04 (found by the console-day rehearsal). The readings are
 * v0.282's, unchanged. A console's run is timed by --frames, not --quiet.
 * AT v0.377.9 (CD rebuild, piece 6: Init answers when the head reaches its home and holds the sector it found;
 * The X-Files Disc 1, the SCPH-7001 BIOS, --ntsc --settle=600): BACK 4.19M (consoles 3.71M-4.24M on the 7501 and
 * 9001), SETTLED 241k (231k-284k), READING 5.29M (5.10M-5.54M), STOPPED and MOTORON 68.0M (CD_SPINUP_CMD, the three
 * consoles pooled). FIRST 2.09M against the consoles' 25.4M-25.8M is the run's own start: sideloaded at the shell
 * here, after a BIOS that left its drive at double speed there.
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
        const uint8_t f = irqFlag();
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    if (hasResp()) g.stat = resp();
    drain();
    ackAll();
    return g;
}

struct InitRow { Got a, b; uint32_t t0; };
/* An Init: its INT3 from the write, its INT2 from the INT3. */
static InitRow init() {
    InitRow r;
    ackAll();
    r.t0 = now();
    command(0x0A);
    r.a = take();
    r.b = r.a.flag == 3 ? take() : Got{ 0, 0, 0 };
    return r;
}
/* Any other command: its INT3 (and, with `second`, its INT2). */
static InitRow other(uint8_t c, bool second) {
    InitRow r;
    ackAll();
    r.t0 = now();
    command(c);
    r.a = take();
    r.b = second && r.a.flag == 3 ? take(MS(6000)) : Got{ 0, 0, 0 };
    return r;
}

/* Every flag for `cycles`, each acknowledged: which interrupt, its first byte, when (from `t0`). */
struct Ev { uint8_t flag, b0; uint32_t t; };
static int logFor(Ev *out, int max, uint32_t t0, uint32_t cycles) {
    int n = 0;
    const uint32_t s = now();
    while (now() - s < cycles && n < max) {
        const uint8_t f = irqFlag();
        if (!f) continue;
        out[n].flag = f; out[n].t = now() - t0;
        out[n].b0 = hasResp() ? resp() : 0xEE;
        drain(); ackAll(); n++;
    }
    return n;
}

static InitRow first, back[12], settled[6], stopI[3], stopS[3], stopS2[3], stopD[2], stopDI[2], motorOn[2], readI[3], stopR[2];
static Ev siEv[2][16]; static int siN[2]; static uint32_t siGetTN[2], siInit[2];
static Got readSect[3];
static uint32_t heldAck[3], heldAfter[3]; static uint8_t heldFlag[3], heldOther[3], heldStat[3];

static void show(const char *name, const InitRow &r) {
    printf("  %-10s INT%d %8u (%02x)", name, r.a.flag, (unsigned)(r.a.t - r.t0), r.a.stat);
    if (r.b.flag) printf("   INT%d %9u after it (%02x)\n", r.b.flag, (unsigned)(r.b.t - r.a.t), r.b.stat);
    else printf("   no second response\n");
}

int main() {
    printf("\ncdrom/cd-init\n");
    printf("Init's two responses from each state the drive can be in. Needs a disc. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();

    first = init();
    for (int i = 0; i < 12; i++) back[i] = init();
    for (int i = 0; i < 6; i++) { idle(MS(1000)); settled[i] = init(); }
    for (int i = 0; i < 3; i++) {
        stopS[i] = other(0x08, true);
        stopS2[i] = other(0x08, true);                               /* again, the motor already stopped */
        idle(MS(2000));
        stopI[i] = init();
    }
    for (int i = 0; i < 2; i++) {
        param(0xA0); other(0x0E, false);                             /* Setmode A0h: double speed */
        idle(MS(1000));
        stopD[i] = other(0x08, true);
        idle(MS(2000));
        stopDI[i] = init();                                          /* Init puts the mode back to 20h */
    }
    for (int i = 0; i < 2; i++) {
        other(0x08, true);                                           /* Stop, and its INT2 */
        idle(MS(2000));
        motorOn[i] = other(0x07, true);                              /* MotorOn from a stopped disc */
        idle(MS(1000));
    }
    for (int i = 0; i < 3; i++) {
        param(0x00); param(0x02); param(0x16); other(0x02, false);   /* Setloc 00:02:16 */
        other(0x06, false);                                          /* ReadN */
        for (int k = 0; k < 8; k++) readSect[i] = take();            /* its INT1s, data left in the drive */
        readI[i] = init();
    }
    for (int i = 0; i < 2; i++) {
        param(0x00); param(0x02); param(0x16); other(0x02, false);   /* Setloc 00:02:16 */
        other(0x06, false);                                          /* ReadN */
        for (int k = 0; k < 8; k++) take();
        stopR[i] = other(0x08, true);                                /* Stop mid-read */
        idle(MS(500));
        init();                                                      /* the motor back on for what follows */
    }
    for (int i = 0; i < 3; i++) {
        ackAll();
        const uint32_t t0 = now();
        command(0x0A);
        uint8_t f;
        do { f = irqFlag(); } while (!f && now() - t0 < MS(3000));
        heldFlag[i] = f;
        heldAck[i] = now() - t0;
        heldOther[i] = 0;
        const uint32_t h0 = now();
        while (now() - h0 < MS(500)) { const uint8_t g = irqFlag(); if (g != f) heldOther[i] = g ? g : 0xFF; }
        heldStat[i] = hasResp() ? resp() : 0; drain();
        ackAll();
        const uint32_t a0 = now();
        const Got g = take();
        heldAfter[i] = g.flag == 2 ? g.t - a0 : 0;
    }
    for (int i = 0; i < 2; i++) {
        /* NFL GameDay 2001's loader: a Stop mid-read at single speed, GetTN 8 frames later, then Init at once. */
        idle(MS(1000));
        param(0x00); param(0x02); param(0x16); other(0x02, false);   /* Setloc 00:02:16 */
        other(0x06, false);                                          /* ReadN (Init left single speed) */
        for (int k = 0; k < 8; k++) take();
        ackAll();
        const uint32_t t0 = now();
        command(0x08);                                               /* Stop: everything from here is logged */
        int n = logFor(siEv[i], 16, t0, MS(133));
        siGetTN[i] = now() - t0;
        command(0x13);                                               /* GetTN */
        while (n < 16) {                                             /* up to its INT3 (anything before it is logged too) */
            const int m = logFor(siEv[i] + n, 1, t0, MS(500));
            if (!m) break;
            n += m;
            if (siEv[i][n - 1].flag == 3) break;
        }
        siInit[i] = now() - t0;
        command(0x0A);                                               /* Init */
        n += logFor(siEv[i] + n, 16 - n, t0, MS(3000));
        siN[i] = n;
        idle(MS(500));
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("Cycles at the system clock (1 ms = 33,869). INT3 from the command write, INT2 from the INT3.\n");
    printf("FIRST (after the boot):\n");
    show("Init", first);
    printf("BACK TO BACK:\n");
    for (int i = 0; i < 12; i++) show("Init", back[i]);
    printf("SETTLED (1 s idle before each):\n");
    for (int i = 0; i < 6; i++) show("Init", settled[i]);
    printf("STOPPED (Stop at single speed, Stop again, 2 s idle, then Init):\n");
    for (int i = 0; i < 3; i++) { show("Stop", stopS[i]); show("Stop again", stopS2[i]); show("Init", stopI[i]); }
    printf("STOPPED AT DOUBLE SPEED (Setmode A0h, 1 s, Stop, 2 s idle, then Init):\n");
    for (int i = 0; i < 2; i++) { show("Stop", stopD[i]); show("Init", stopDI[i]); }
    printf("MOTORON (Stop, 2 s idle, MotorOn):\n");
    for (int i = 0; i < 2; i++) show("MotorOn", motorOn[i]);
    printf("READING (Setloc 00:02:16, ReadN, 8 sectors, then Init):\n");
    for (int i = 0; i < 3; i++) {
        printf("  (the 8th INT1: flag %d, stat %02x)\n", readSect[i].flag, readSect[i].stat);
        show("Init", readI[i]);
    }
    printf("STOP MID-READ (psx-spx: its first response 'already with bit5 cleared'):\n");
    for (int i = 0; i < 2; i++) show("Stop", stopR[i]);
    printf("HELD (Init's INT3 left 500 ms unacknowledged):\n");
    for (int i = 0; i < 3; i++)
        printf("  Init       INT%d %8u (%02x)   another flag while held: %s   INT2 %u after the acknowledge\n",
               heldFlag[i], (unsigned)heldAck[i], heldStat[i], heldOther[i] ? "YES" : "no", (unsigned)heldAfter[i]);
    printf("STOP THEN INIT (Stop mid-read, GetTN 133 ms later, Init at its INT3; every interrupt, from the Stop's write):\n");
    for (int i = 0; i < 2; i++) {
        printf("  GetTN written at %u, Init at %u\n", (unsigned)siGetTN[i], (unsigned)siInit[i]);
        for (int k = 0; k < siN[i]; k++) printf("    INT%d %9u (%02x)\n", siEv[i][k].flag, (unsigned)siEv[i][k].t, siEv[i][k].b0);
        printf("    (%d interrupts; is there an INT2 between the Stop's INT3 and the GetTN's?)\n", siN[i]);
    }
    printf("\ncdrom/cd-init: Done.\n");
    for (;;) VSync(0);
}
