/*
 * cdrom/cd-spinup-trend - does a drive spin up faster cold? (2026-10-19)
 *
 * WHY. Pooled over every run (v0.377.134), a console's FIRST run of a day spins up fast: cd-init's first run on the
 * SCPH-7001 read 52.8M-59.6M where every later run read 58.2M-77.6M; the 7501's two console-day runs 61.7M / 63.6M
 * against 71.7M-76.6M in its six since; one of the 9001's first two 63.3M. If the spindle motor (or its driver) works
 * differently cold, the spin-up should climb across the first minutes after a cold power-on and then level off.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (MotorOn's 12 s), Timer 2 at
 * sysclock/8 unwrapped. Init, then twenty-four times: Stop (its INT2 timed: the spin-down), 2 s idle, MotorOn (its INT2
 * timed from the write: the spin-up), with the seconds since the first beside each. About two minutes.
 *
 * RUN IT FIRST AFTER A COLD POWER-ON (the console off an hour or more), then again warm, and compare the two columns.
 *
 * READ IT LIKE THIS. A row: its number, seconds since the first, MotorOn's INT2 and Stop's, in system-clock cycles
 * (1 ms = 33,869). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-spinup-trend --disc=xfiles --frames=12000 --quiet=10000`.
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

int main() {
    printf("\ncdrom/cd-spinup-trend\n");
    printf("Twenty-four spin-ups in a row (Stop, 2 s idle, MotorOn): seconds since the first, MotorOn's INT2 from its write\n");
    printf("(the spin-up), and Stop's INT2 (the spin-down). Run it FIRST after a cold power-on. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2, LIMIT_INIT); drain(); ackAll();   /* Init */
    simple(0x0C); setmode(0x00);
    uint32_t secs = 0, lastClk = now();
    for (int k = 0; k < 24; k++) {
        const uint32_t ts = now();
        command(0x08); waitInt(3); drain(); ackAll();
        const uint8_t s2 = waitInt(2); const uint32_t tStop = now() - ts; drain(); ackAll();   /* Stop */
        { const uint32_t tw = now(); while (now() - tw < 67737600u) {} }                    /* 2 s */
        const uint32_t tm = now();
        command(0x07); waitInt(3); drain(); ackAll();
        const uint8_t m2 = waitInt(2, LIMIT_INIT); const uint32_t tMotor = now() - tm; drain(); ackAll();   /* MotorOn */
        const uint32_t c = now(); secs += (c - lastClk) / 33868800u; lastClk = c - (c - lastClk) % 33868800u;
        printf("%2d  %4us  MotorOn INT2 %9u%s   Stop INT2 %9u%s\n", k + 1, (unsigned)secs, (unsigned)tMotor, m2 == 2 ? "" : " (none)",
               (unsigned)tStop, s2 == 2 ? "" : " (none)");
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-spinup-trend: Done.\n");
    return 0;
}
