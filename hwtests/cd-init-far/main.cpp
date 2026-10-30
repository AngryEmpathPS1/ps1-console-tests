/*
 * cdrom/cd-init-far - Init from a STOPPED disc with the head far out, against Init from a turning one (2026-10-18 night)
 *
 * WHY. cd-spinup-phase on the SCPH-7001, six runs: 8-9 of its 2-second waits ran out every run, where this core and the
 * drive's own program (SC430934) run out none. Each of its spin-ups ends with the head at 50:00 (a SeekL there), then
 * Stop, 1 s, Init - and the Init's INT2 is the wait that can run out: hwtests/cd-init's STOPPED rows (Stop near the
 * start, 2 s, Init) read 52.8M-58.0M on the 7001 (1.6-1.7 s), inside 2 s. This core charges a stopped disc's Init the
 * spin-up alone, "with the way home inside it" (src/psx.js Init, CD_SPINUP_CMD); a console that walks home after the
 * spin-up, or more slowly than it spins up, answers later the further out the head is.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way (a first read that is not the interrupt wanted is
 * read again; CONSOLE RULE 1), every wait bounded - 12 s for an Init's INT2, 3 s for the rest - Timer 2 at sysclock/8
 * unwrapped in software. For each place 00:02, 10:00, 20:00, 30:00, 40:00, 50:00 (in, then out again: twice each):
 *   S  SeekL there (single speed), Stop and its INT2, 1 s idle, Init: its INT3 and INT2 from the write, the stats;
 *   T  SeekL there, Init with the disc turning: the same.
 * After each Init, GetlocP (where the head came to rest) and Setmode 00h.
 *
 * READ IT LIKE THIS. Per row the place, S or T, Init's INT3 and INT2 in cycles from its write (1 ms = 33,869) with their
 * stats, and GetlocP's absolute sector after. A stopped Init that grows with the place is a walk home outside the
 * spin-up. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-init-far --disc=xfiles --frames=90000 --quiet=10000`.
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
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static const uint32_t LIMIT = 101606400u;      /* 3 seconds */
static const uint32_t LIMIT_INIT = 406425600u; /* 12 seconds */
static int timeouts;
/* The interrupt wanted (or 5), 0 if none came within lim. A first read that is neither is read again before it is
 * taken for another interrupt (psx-spx "Unstable IRQ Flag polling"). */
static uint8_t waitInt(uint8_t want, uint32_t lim = LIMIT) {
    const uint32_t t0 = now();
    for (;;) {
        if (now() - t0 > lim) { timeouts++; return 0; }
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f == want || f == 5) return f;
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static uint8_t stat1() { uint8_t s = 0xEE; if (hasResp()) s = resp(); drain(); ackAll(); return s; }
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setmode(uint8_t m) { param(m); simple(0x0E); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}
static void seekTo(uint32_t lba) {
    setloc(lba);
    command(0x15);
    if (waitInt(3) != 3) { drain(); ackAll(); return; }
    drain(); ackAll();
    waitInt(2); drain(); ackAll();
}
/* GetlocP's absolute sector, or -1. */
static int32_t getlocP() {
    command(0x11);
    if (waitInt(3) != 3) { drain(); ackAll(); return -1; }
    uint8_t b[8]; int n = 0;
    while (hasResp() && n < 8) b[n++] = resp();
    drain(); ackAll();
    if (n < 8) return -1;
    return (int32_t)((unbcd(b[5]) * 60 + unbcd(b[6])) * 75 + unbcd(b[7])) - 150;
}

static void row(uint32_t lba, bool stopped) {
    seekTo(lba);
    if (stopped) {
        command(0x08); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Stop */
        idle(33868800u);                                                               /* 1 s */
    }
    const uint32_t t0 = now();
    command(0x0A);
    const uint8_t a = waitInt(3); const uint32_t ta = now() - t0; const uint8_t sa = stat1();
    uint8_t b = 0; uint32_t tb = 0; uint8_t sb = 0xEE;
    if (a == 3) { b = waitInt(2, LIMIT_INIT); tb = now() - t0; sb = stat1(); }
    const int32_t p = getlocP();
    setmode(0x00);
    const uint32_t a2 = (lba + 150) / 75;
    printf("%02d:%02d %s  Init INT%d %9u (%02x)  INT%d %10u (%02x)  then GetlocP %d\n", (int)(a2 / 60), (int)(a2 % 60),
           stopped ? "S" : "T", (int)a, ta, sa, (int)b, tb, sb, (int)p);
}

int main() {
    printf("\ncdrom/cd-init-far\n");
    printf("Init from a stopped disc (S: SeekL, Stop, 1 s) and a turning one (T: SeekL) at each place, in then out.\n");
    printf("Cycles from Init's write (1 ms = 33,869), the stats, and GetlocP's sector after. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2, LIMIT_INIT); drain(); ackAll();
    simple(0x0C); setmode(0x00);
    static const uint32_t PLACE[6] = { 0, 44850, 89850, 134850, 179850, 224850 };
    for (int k = 0; k < 6; k++) { row(PLACE[k], true); row(PLACE[k], false); }
    for (int k = 5; k >= 0; k--) { row(PLACE[k], true); row(PLACE[k], false); }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-init-far: Done.\n");
    return 0;
}
