/*
 * cdrom/cd-jumpclass - which turn a short seek back lands on, at many margins (2026-11-16). A pressed data disc whose data reaches
 * 01:38 (Doom (USA), The X-Files Disc 1). Drive timing only on a pressed disc.
 *
 * WHY. Doom's level read 9 (7,202 -> 4,942 at double speed, from the Pause after a 45-sector read) takes 5.8M cycles on the second
 * SCPH-5501 24 times in 36 and 10.0-10.2M 12 times; this emulator's drive program (SC430930 on src/cdservo.js) takes 8.0M. Its seek
 * ends in one-track corrections, and after the last one the decoder's stale in-flight sector (STATUS v0.377.155) arrives ~4 ms after
 * the jump - through the program's GFS gate or not, by the jump's settle (cdservo.js JUMP_SETTLE_S, 1 ms, an assumption). One settle
 * does not fit the console: 3 ms gives read 9 and loses reads 4 and 1, and the console shows two classes on most reads, run to run, on
 * one unit (experiments/doomload/NOTES.md 18). A settle drawn per jump gives the splits. The host cannot time one jump (every answer it
 * gets comes on the subcode's sector grid, 6.7 ms at double speed); what it can see is the CLASS - which turn the read lands on - and
 * the class flips where a jump's settle crosses a knife edge. Each target here is another edge, so the share of each class per target,
 * over many repeats, is what the settle's spread must reproduce.
 *
 * WHAT IT DOES. cd-speed-seek3's helpers (interrupts off, the flags polled, every wait bounded, Timer 2 at sysclock/8 unwrapped). Init,
 * Demute, GetTD (no disc: it says so and stops). Double speed. Then per target, NREP times: Setloc 7,157, ReadN, its 45 INT1s, Pause
 * to its INT2; 5.29M cycles (the game's spacing between that INT2 and its Setloc, the second 5501's in-game probe); Setloc <target>,
 * ReadN, the time from its write to its first INT1; Pause to its INT2. The targets: 4,900 to 4,990 by 6 (16), Doom's 4,942 among them.
 *
 * READ IT LIKE THIS. A row per target: "T <lba>:" then each repeat's write-to-first-INT1 in hundredths of a M cycle (581 = 5.81M), 0
 * for a read that failed. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-jumpclass --disc=doom --ntsc --frames=12000 --quiet=11000 --settle=600`.
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
static uint8_t irqFlag() { idx(1); return *CD3 & 0x1F; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
static uint8_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static const uint32_t LIMIT = 67737600u;   /* 2 seconds */
static int timeouts;
/* The interrupt wanted (or 5), 0 if none came within LIMIT. A first read that is neither is read again before it is
 * taken for another interrupt (psx-spx "Unstable IRQ Flag polling"; CONSOLE RULE 1). */
static uint8_t waitInt(uint8_t want) {
    const uint32_t t0 = now();
    for (;;) {
        if (now() - t0 > LIMIT) { timeouts++; return 0; }
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f == want || f == 5) return f;
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static uint8_t simple(uint8_t cmd) { command(cmd); const uint8_t r = waitInt(3); drain(); ackAll(); return r; }
static void setmode(uint8_t m) { param(m); simple(0x0E); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}
/* Pause to its INT2 (0 on none). */
static uint8_t pauseHold() { if (simple(0x09) != 3) return 0; const uint8_t r = waitInt(2); drain(); ackAll(); return r; }
/* ReadN at the Setloc: its write to the first INT1 in cycles (0 on an error), then n-1 INT1s more taken. */
static uint32_t readN(int n) {
    const uint32_t t0 = now();
    command(0x06);
    if (waitInt(3) != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    if (waitInt(1) != 1) { drain(); ackAll(); return 0; }
    const uint32_t t = now() - t0; drain(); ackAll();
    for (int i = 1; i < n; i++) { if (waitInt(1) != 1) { drain(); ackAll(); return 0; } drain(); ackAll(); }
    return t;
}

#define NT 16
#define NREP 10
static uint16_t res[NT][NREP];

int main() {
    printf("\ncdrom/cd-jumpclass\n");
    printf("A short seek back at double speed after a 45-sector read and its Pause (Doom's level read 9), at 16 targets: the turn it lands on.\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                   /* Demute */
    param(0); command(0x14);                        /* GetTD(0): the disc's length, or no disc */
    uint32_t total = 0;
    if (waitInt(3) == 3) { resp(); const uint8_t mm = unbcd(resp()), ss = unbcd(resp()); total = (mm * 60 + ss) * 75; }
    drain(); ackAll();
    if (total < 7400) {
        restoreTimer(2, prev); ExitCriticalSection();
        printf("disc: %s - nothing to time\n", total ? "too short (needs data to 01:38)" : "none (GetTD gave nothing)");
        printf("\nDone.\n");
        for (;;) {}
    }
    printf("disc: %d sectors\n", total);
    setmode(0x80);
    for (int r = 0; r < NREP; r++)
        for (int k = 0; k < NT; k++) {
            setloc(7157);
            uint32_t ok = readN(45);
            if (ok) ok = pauseHold();
            uint32_t t = 0;
            if (ok) {
                idle(5290000u);
                setloc(4900 + 6 * k);
                t = readN(1);
                pauseHold();
            }
            res[k][r] = (uint16_t)(t / 10000 > 65535 ? 65535 : t / 10000);
        }
    restoreTimer(2, prev);
    ExitCriticalSection();
    for (int k = 0; k < NT; k++) {
        printf("T %4d:", 4900 + 6 * k);
        for (int r = 0; r < NREP; r++) printf(" %d", res[k][r]);
        printf("\n");
    }
    printf("waits that ran out: %d\n", timeouts);
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05). */
    for (;;) {}
}
