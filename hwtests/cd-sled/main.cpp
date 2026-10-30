/*
 * cdrom/cd-sled - long seeks, timed, to fit each console's sled (2026-10-19)
 *
 * WHY. The seek planner's sled (CD_PLAN sledBase / sledTps) is one fit across every console, from the first two consoles'
 * cd-seek medians. On cd-seek over five boards the newer drives' longest seeks read ~0.74 of this core's time (the 7001,
 * 7501, 9001) and the older ones' 1.15-1.19 (the 1001, 5501), and cd-spinup-state's 10:00 -> 29:01 on the 7001 reads 0.80
 * - but cd-seek has three rows over 25M a console, too few to fit two numbers per console. This is long seeks only.
 *
 * WHAT IT DOES. Interrupts off, the CD polled psx-spx's way, every wait bounded, Timer 2 at sysclock/8 unwrapped. At
 * single speed, then double: for each far point 05:00, 10:00, 20:00, 30:00, 40:00, 50:00, 55:00 (The X-Files Disc 1's data
 * runs to 57:59), three times: a SeekL to 00:02, 0.1 s, a SeekL out to the far point (timed to its INT2) and GetlocP; 0.1 s,
 * a SeekL back to 00:02 (timed) and GetlocP.
 *
 * READ IT LIKE THIS. A row: the far point, then per repeat the out seek's INT2 (cycles from its write) with GetlocP less
 * the target, and the in seek's with GetlocP's sector. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-sled --disc=xfiles --frames=20000 --quiet=10000`.
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

static const uint32_t LIMIT = 67737600u;   /* 2 seconds */
static const uint32_t LIMIT_INIT = 406425600u; /* 12 seconds: a stopped disc's Init (cd-init-far: 65-78M on the 7001) */
static int timeouts;
/* The interrupt wanted (or 5), 0 if none came within LIMIT. A first read that is neither is read again before it is
 * taken for another interrupt (psx-spx "Unstable IRQ Flag polling"; CONSOLE RULE 1). */
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
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setmode(uint8_t m) { param(m); simple(0x0E); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}
/* SeekL to lba and wait for it to finish; command write -> INT2 in cycles, 0 on an error or a wait that ran out. */
static uint32_t seekTo(uint32_t lba) {
    setloc(lba);
    const uint32_t t0 = now();
    command(0x15);
    const uint8_t a = waitInt(3);
    if (a != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    const uint8_t r = waitInt(2, LIMIT_INIT); const uint32_t t = now() - t0; drain(); ackAll();
    return r == 2 ? t : 0;
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

static int32_t getlocL(uint8_t *flag) {
    command(0x10);
    const uint8_t f = waitInt(3); *flag = f;
    uint8_t r[8]; int n = 0;
    while (hasResp() && n < 8) r[n++] = resp();
    drain(); ackAll();
    if (f != 3 || n < 8) return -99999;
    return (int32_t)((unbcd(r[0]) * 60 + unbcd(r[1])) * 75 + unbcd(r[2])) - 150;
}

int main() {
    printf("\ncdrom/cd-sled\n");
    printf("Long seeks, timed: at single and double speed, SeekL from 00:02 out to 05:00 ... 55:00 and back in, three times\n");
    printf("each; the SeekL's INT2 from its write, in system-clock cycles (1 ms = 33,869), and GetlocP after. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2, LIMIT_INIT); drain(); ackAll();
    simple(0x0C);
    static const uint32_t FAR[7] = { 22350, 44850, 89850, 134850, 179850, 224850, 247350 };   /* 05:00 10:00 20:00 ... 50:00 55:00 */
    for (int sp = 0; sp < 2; sp++) {
        setmode(sp ? 0x80 : 0x00);
        { const uint32_t tw = now(); while (now() - tw < 33868800u) {} }   /* the speed change settled (1 s) */
        printf("%s\n", sp ? "DOUBLE SPEED" : "SINGLE SPEED");
        for (int k = 0; k < 7; k++) {
            const uint32_t a2 = (FAR[k] + 150) / 75;
            printf("  00:02 <-> %02d:%02d :", (int)(a2 / 60), (int)(a2 % 60));
            for (int rep = 0; rep < 3; rep++) {
                seekTo(0);
                { const uint32_t tw = now(); while (now() - tw < 3386880u) {} }   /* 0.1 s in the hold */
                const uint32_t out = seekTo(FAR[k]);
                const int32_t po = getlocP();
                { const uint32_t tw = now(); while (now() - tw < 3386880u) {} }
                const uint32_t in = seekTo(0);
                const int32_t pi = getlocP();
                printf("  out %9u (%d) in %9u (%d)", (unsigned)out, (int)(po - (int32_t)FAR[k]), (unsigned)in, (int)pi);
            }
            printf("\n");
        }
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-sled: Done.\n");
    return 0;
}
