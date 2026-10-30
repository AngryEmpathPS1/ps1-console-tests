/*
 * cdrom/cd-spinup-state - is the hold's per-spin-up state set by the spin-up? (2026-10-19)
 *
 * WHY. cd-spinup-phase on the SCPH-7001, six runs: the hold's kick back sits two sectors early (^0 against ^2) on one
 * spin-up in five (12 of 60), steady through the spin-up's three seeks - a state set when the disc spins up. Neither this
 * emulator's drive model nor the drive's own program (SC430934 on its servo model) has any such state (STATUS, "The
 * SCPH-7001 at night"). If the state comes from the spin-up itself - where the spindle's lock lands - something about the
 * spin-up should go with it. This times every spin-up and puts it beside the state.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (Init's 12 s), Timer 2 at
 * sysclock/8 unwrapped. Sixteen times: Stop (and its INT2), 1 s, Init - its INT2 timed from the write (the spin-up) -
 * Demute, Setmode 00h; then a SeekL to 10:00 timed to its INT2, GetlocP / GetlocL at once and GetlocP every quarter sector
 * for 20 sectors (^K, the highest before the hold's kick back); the same at 29:01.
 *
 * READ IT LIKE THIS. Per spin-up: Init's INT2 in cycles; then per seek its INT2 time, P/L (GetlocP / GetlocL less the
 * target, E an error) and ^K. Put the ^0 spin-ups beside the others. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-spinup-state --disc=xfiles --frames=90000 --quiet=10000`.
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
    const uint8_t r = waitInt(2); const uint32_t t = now() - t0; drain(); ackAll();
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
    printf("\ncdrom/cd-spinup-state\n");
    printf("Sixteen spin-ups (Stop, 1 s, Init): Init's INT2 (the spin-up), then at 10:00 and 29:01 the SeekL's INT2, P/L at once\n");
    printf("and ^K (GetlocP's highest before the hold's kick back), all less the target. Cycles at the system clock. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2, LIMIT_INIT); drain(); ackAll();
    static const uint32_t TG[2] = { 44850, 130462 };
    for (int run = 0; run < 16; run++) {
        command(0x08); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();          /* Stop */
        { const uint32_t tw = now(); while (now() - tw < 33868800u) {} }                    /* 1 s */
        const uint32_t ti = now();
        command(0x0A); waitInt(3); drain(); ackAll();
        const uint8_t i2 = waitInt(2, LIMIT_INIT); const uint32_t tInit = now() - ti; drain(); ackAll();   /* Init */
        simple(0x0C); setmode(0x00);
        printf("spin-up %2d: Init %9u%s", run + 1, (unsigned)tInit, i2 == 2 ? "" : " (no INT2)");
        for (int k = 0; k < 2; k++) {
            const uint32_t ts = seekTo(TG[k]);
            const int32_t P = getlocP(); uint8_t lf; const int32_t L = getlocL(&lf);
            int32_t top = P, last = P;
            const uint32_t t0 = now();
            while (now() - t0 < 20u * 451584u) {
                { const uint32_t tw = now(); while (now() - tw < 112896u) {} }
                const int32_t q = getlocP();
                if (q < last - 3) break;
                if (q > top) top = q;
                last = q;
            }
            printf("  | %u %d/", (unsigned)ts, (int)(P - (int32_t)TG[k]));
            if (L == -99999) printf("E"); else printf("%d", (int)(L - (int32_t)TG[k]));
            printf(" ^%d", (int)(top - (int32_t)TG[k]));
        }
        printf("\n");
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-spinup-state: Done.\n");
    return 0;
}
