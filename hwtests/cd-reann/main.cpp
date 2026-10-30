/*
 * cdrom/cd-reann - the buffered sector after an acknowledged INT1, then Pause: nugget's simpleReadingWithoutAck, 24
 * times across the sector clock (v0.377.54).
 *
 * WHY. After the host acknowledges an INT1 with sectors landed behind it, the drive announces the newest one from its
 * loop 13,000-97,000 cycles later (hwtests/cd-ack-getloc's CONTROL, three consoles; src/psx.js reannounceSector).
 * nugget's tests/cdrom reading.c simpleReadingWithoutAck does exactly that and then sends Pause right after the second
 * INT1's acknowledge: the SCPH-9001 answered INT1, INT1, INT3, INT2 on its run. This core's model of the drive's pass
 * order (piece 8, from the drive programs) announces a NEW sector first when its decode began more than 8,000 cycles
 * before the command was written - and with the console's re-announce time the Pause lands in such a decode on ~1 run
 * in 4-5 (2 of nugget's 6 CD timing draws here), where with DuckStation's 5,000 it never did. Is the silicon test a
 * draw too? This counts it.
 *
 * WHAT IT DOES. Interrupts off, the CD polled, Timer 2 at sysclock/8 unwrapped. Per trial: Setmode 80h (double speed),
 * SeekL 20:00:00, ReadN and its INT3 acknowledged, ~500 ms plus j x 9,408 cycles (j = 0..23, twenty-fourths of a
 * double-speed sector), then nugget's own order: the pending INT1 acknowledged and its response read; the next
 * interrupt, acknowledged and read; Pause written at once; then every interrupt until the Pause's INT2 (up to four).
 *
 * READ IT LIKE THIS. A row: "j: second INTc at d  then INTc d, INTc d ..." - d in cycles, the second from the first's
 * acknowledge, the rest from the Pause's write. nugget's pass is "INT1 ... then INT3, INT2"; an INT1 before the INT3 is
 * a sector the drive announced ahead of the Pause. Count the rows that read it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first. ON THIS CORE AT v0.377.54: the second INT1 16,120-98,280 cycles after the acknowledge,
 * then INT3 at 62,000-102,000 and INT2 at ~3.0M on all 24 rows - no INT1 ahead of the Pause (nugget's suite met one on 2
 * of its 6 draws). v0.377.53 (DuckStation's 5,000): the second INT1 at ~4,950 on 18 rows, 23,000-51,000 on the six whose
 * acknowledge fell in the next sector's decode window; the same order after.
 * `node hwtests/run.js cd-reann --disc --frames=8000 --quiet=1500`
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
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */

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

static void trial(int j) {
    param(0x80); simple(0x0E);                                       /* Setmode 80h */
    param(0x20); param(0x00); param(0x00); simple(0x02);             /* Setloc 20:00:00 */
    command(0x15); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* SeekL */
    command(0x06); waitInt(3); drain(); ackAll();                    /* ReadN */
    const uint32_t t0 = now();
    waitUntil(t0 + 16934400 + (uint32_t)j * 9408);                  /* ~500 ms, and the trial's phase */
    uint32_t t1, t2;
    const uint8_t c1 = nextInt(&t1);                                 /* the pending INT1 */
    ackAll(); drain();
    const uint32_t ta = now();
    const uint8_t c2 = nextInt(&t2);                                 /* the next one */
    ackAll(); drain();
    command(0x09);                                                   /* Pause, at once (nugget's order) */
    const uint32_t tp = now();
    printf("  j=%2d: INT%d, second INT%d at %6d  then", j, c1, c2, (int)(t2 - ta));
    for (int n = 0; n < 4; n++) {
        uint32_t t;
        const uint8_t c = nextInt(&t);
        ackAll(); drain();
        printf(" INT%d %7d", c, (int)(t - tp));
        if (c == 2 || c == 5 || c == 0) break;
    }
    printf("\n");
}

int main() {
    printf("\ncdrom/cd-reann\n");
    printf("nugget's simpleReadingWithoutAck 24 times: an INT1 left ~500 ms, acknowledged, the next interrupt, Pause.\n");
    printf("Row: j: the second interrupt (cycles from the first's acknowledge), then each after the Pause's write.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    const uint16_t prev = initTimer(2, 2);          /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    for (int j = 0; j < 24; j++) trial(j);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
