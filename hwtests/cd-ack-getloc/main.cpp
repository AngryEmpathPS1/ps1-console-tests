/*
 * cdrom/cd-ack-getloc - a held sector acknowledged with more buffered behind it, then a command k cycles later: is
 * the next sector announced first, and does the drive take the command? (2026-10-05, for the SCPH-9001)
 *
 * WHY. nocash's DISK DATA DURATION page (DIAG.A22 @@disk_data_duration, its third part) does exactly this:
 *   Setmode 00h (single speed: its `mov r1,0e00h ;+80h` sends parameter 00h, the +80h a comment - and "survives up to
 *   FIVE waitvblanks without causing buffer overrun" is 1x's arithmetic), Setloc 00:02:04 + ReadN, wait for sector
 *   4's INT1 and leave it, four VBlanks (~5 sectors buffered behind it, no overrun), the INT1
 *   acknowledged, three data requests, and GetlocL 1,711 cycles after the acknowledge (STATUS M241) - then it spins
 *   on BUSYSTS (1F801800h bit 7) until the drive takes the command, and takes the next interrupt as its answer.
 * The drive takes no command while an interrupt is unacknowledged (psx-spx "the mainloop sees a new command without
 * any old interrupt pending"), so if the next buffered sector's INT1 goes out before GetlocL is taken, the page spins
 * for ever. On nocash's console the page finishes. In this emulator it hangs on most CD timing draws: the model on 29
 * of 32 seed pairs (M366) and 32 of 32 (M367, the Pause a turn), and the drive's own program at most sector phases
 * (STATUS M282) - the buffered sector re-announced ~3,000 cycles after GetlocL's write, before the pickup.
 * psx-spx's own logs (nocash, "Sector Buffer VS GetlocL Response Tests") show the buffered INT1 BEFORE GetlocL's
 * INT3 when the sector was already waiting at the write; what they cannot show is how soon after an ACKNOWLEDGE the
 * next one is announced. That is this test's question.
 *
 * WHAT IT DOES. Interrupts off, the CD polled, Timer 2 at sysclock/8 unwrapped. Per trial: Setloc 00:02:04, ReadN,
 * sector 4's INT1 waited for and left; 2,257,920 cycles (four NTSC frames) plus j x 37,632 (j = 0..11, twelfths of a
 * 1x sector: the acknowledge swept across the sector clock); the INT1 acknowledged (index 1, 1Fh - the page's @@ack_irq)
 * and its header noted; three data requests (index 0, 80h to 1F801803h, the page's) or none; GetlocL written k cycles
 * after the acknowledge. Then, for up to ~0.2 s: when BUSYSTS clears, and the first two interrupts with their times
 * from the acknowledge. If an interrupt is pending with BUSYSTS still set after ~6 ms, the row says BLOCKED and the
 * interrupt is acknowledged, as the page never would - so the run goes on. Then Pause.
 * At 1x, the page's speed: k = 0, 1,711 (the page's), 5,000, 30,000 with the data requests, and 1,711 without; 12
 * phases each. First the CONTROL at 2x and at 1x: no command, the next INT1 after the acknowledge timed alone.
 *
 * READ IT LIKE THIS. A row: "k j: order  busy  first  second". order 31 = GetlocL's INT3 first, 13 = an INT1 first,
 * B = BLOCKED (an INT1 came first and the drive did not take the command until it was acknowledged); busy = cycles
 * from the acknowledge to BUSYSTS clearing; first / second = cycles from the acknowledge. The CONTROL rows give the
 * re-announce's delay alone. A console that reads 31 at k = 1,711 on every trial is nocash's; this emulator's rule
 * (the buffered sector re-announced at the next poll after the acknowledge) reads 13 and B there.
 *
 * THE RULE: run it here first. ON THIS CORE AT v0.367 (the model): the CONTROL's next INT1 comes at the next SECTOR
 * - 44,000-457,000 cycles after the acknowledge at 1x by phase (5,024 once at 2x) - so GetlocL is answered first on
 * 83 of 84 rows (B3 once, at k = 30,000 j = 11). ON THE SCPH-9001's OWN PROGRAM (PSX_CD_FIRMWARE=CDROM-firmware/
 * SC430944.S19, --settle=600): the buffered sector is re-announced 13,000-27,000 cycles after the acknowledge (53,000
 * and 85,000 once each), and GetlocL loses that race - B3, the drive holding the command while the INT1 waits, the
 * page's deadlock - on 4 of 12 phases at k = 1,711, 7 of 12 at 5,000 and 11 of 12 at 30,000 (2 of 12 at k = 0; 5 of
 * 12 at 1,711 without the data requests). A console that never reads B at k = 1,711 is nocash's, and says the
 * program's emulated decoder or loop runs a re-announce too soon; one that reads B is the page's own race.
 * `node hwtests/run.js cd-ack-getloc --disc --frames=20000 --quiet=1500`
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
static bool busy() { return (*CD0 & 0x80) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static void dataRequest() { idx(0); *CD3 = 0x80; }
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void waitUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) { } }

/* The next interrupt of any kind and when it was first seen; 0 after ~2 s. */
static uint8_t nextInt(uint32_t* at) {
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        const uint8_t f = irqFlag();
        if (f) { *at = t; return f; }
        if (t - t0 > 70000000) { *at = t; return 0; }
    }
}
/* Every wait for one KIND gives up after ~4 s of other kinds too (console day: a drive left reading fed a wait for
 * INT2 an INT1 every 13 ms for 13 minutes, interrupts off, the console deaf to the cable). */
static bool tooLong(uint32_t t0) { return now() - t0 > 140000000; }
static uint8_t waitInt(uint8_t want) {
    const uint32_t t0 = now();
    for (;;) {
        uint32_t t;
        const uint8_t f = nextInt(&t);
        if (f == want || f == 5 || f == 0) return f;
        drain(); ackAll();
        if (tooLong(t0)) return 0;
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }

/* Stop the read: Pause, its INT2, sectors still landing acknowledged (the page's flush). */
static void stop() {
    command(0x09);
    const uint32_t t0 = now();
    for (;;) {
        uint32_t t;
        const uint8_t f = nextInt(&t);
        drain(); ackAll();
        if (f == 2 || f == 5 || f == 0 || tooLong(t0)) break;
    }
}

struct Row { char order[3]; int32_t busyAt, firstAt, secondAt; uint8_t first, second; };

/* One trial. k < 0: the control, no command. */
static Row trial(int32_t k, int j, bool requests) {
    Row r; r.order[0] = '-'; r.order[1] = '-'; r.order[2] = 0; r.busyAt = r.firstAt = r.secondAt = -1; r.first = r.second = 0;
    param(0x00); param(0x02); param(0x04); simple(0x02);          /* Setloc 00:02:04 */
    command(0x06); waitInt(3); drain(); ackAll();                  /* ReadN */
    uint32_t t1;
    for (;;) { const uint8_t f = nextInt(&t1); if (f == 1 || f == 0 || f == 5) break; drain(); ackAll(); }   /* sector 4, left */
    waitUntil(t1 + 2257920 + (uint32_t)j * 37632);                    /* four frames, and the trial's phase */
    drain();
    const uint32_t ta = now();
    ackAll();                                                        /* the page's @@ack_irq */
    if (requests) { dataRequest(); dataRequest(); dataRequest(); }
    if (k >= 0) {
        waitUntil(ta + (uint32_t)k);
        command(0x10);                                               /* GetlocL */
    }
    /* Watch BUSYSTS and the interrupts for up to ~6 ms, then ~0.2 s for the rest. */
    bool blocked = false;
    for (;;) {
        const uint32_t t = now();
        if (k >= 0 && r.busyAt < 0 && !busy()) r.busyAt = (int32_t)(t - ta);
        const uint8_t f = irqFlag();
        if (f) {
            if (!r.first) { r.first = f; r.firstAt = (int32_t)(t - ta); }
            else { r.second = f; r.secondAt = (int32_t)(t - ta); }
            if (k >= 0 && r.second == 0 && f == 1 && busy()) {
                /* an INT1 came first: does the drive take the command while it is pending? (the page waits here) */
                while (busy() && now() - t < 200000) { }
                if (busy()) blocked = true;
            }
            drain(); ackAll();
            if (r.second || k < 0) break;
        }
        if (t - ta > 7000000) break;
    }
    if (k >= 0) { r.order[0] = '0' + r.first; r.order[1] = '0' + r.second; if (blocked) r.order[0] = 'B'; }
    else { r.order[0] = '0' + r.first; r.order[1] = 0; }
    stop();
    return r;
}

int main() {
    printf("\ncdrom/cd-ack-getloc\n");
    printf("A held INT1 acknowledged after four frames, then GetlocL k cycles later (nocash's DISK DATA DURATION).\n");
    printf("Row: k j: order (31 INT3 first, 13 INT1 first, B = an INT1 first and the command not taken until it was acknowledged),\n");
    printf("busy (BUSYSTS clear), first and second interrupt - cycles from the acknowledge. There are no assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    const uint16_t prev = initTimer(2, 2);          /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    for (int speed = 1; speed >= 0; speed--) {
        param(speed ? 0x80 : 0x00); simple(0x0E);                                   /* Setmode */
        printf("CONTROL at %s (no command), the next interrupt after the acknowledge:\n", speed ? "2x" : "1x");
        for (int j = 0; j < 12; j++) { const Row r = trial(-1, j, true); printf("  j=%2d: INT%d at %d\n", j, r.first, (int)r.firstAt); }
    }
    /* Setmode 00h is in force from here: the page's speed. */
    static const int32_t KS[4] = { 0, 1711, 5000, 30000 };
    for (int req = 1; req >= 0; req--) {
        printf("%s, at 1x:\n", req ? "WITH the three data requests (the page's)" : "WITHOUT data requests");
        for (int ki = 0; ki < 4; ki++) {
            if (!req && KS[ki] != 1711) continue;
            for (int j = 0; j < 12; j++) {
                const Row r = trial(KS[ki], j, req != 0);
                printf("  k=%5d j=%2d: %s  busy %7d  first INT%d %7d  second INT%d %7d\n", (int)KS[ki], j, r.order,
                       (int)r.busyAt, r.first, (int)r.firstAt, r.second, (int)r.secondAt);
            }
        }
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
