/*
 * cdrom/cd-lid-sweep2 - the drive's own disc detect after the lid shuts, timed from inside: its RAM polled (2026-10-19)
 *
 * WHY. hwtests/cd-lid on the SCPH-7501 (three runs): after the lid shuts the drive answers "lid open" (11h 80h) to 1.64 s,
 * then "not ready" (13h 80h) to ~6.5 s, then the disc. The 7501's own program (SC430940) on this emulator's servo:
 * 1.53 s (with v0.377.145's focus search), then ready at ~3.7 s - 2.9 s short. Traced on the servo (scratch_lidtrace.js),
 * the program's detect after the spindle locks: a FOCUS BIAS SWEEP (1.52-2.07 s: 248 manual quality reads, CX(8120), one
 * per main-loop tick, FBIAS stepped by 6 until the figure stops falling - its result at RAM 1CFh, psx-spx "always in
 * steps of 6", E8h on licensed discs), the sled out and back to the inner stop (2.08-2.50 s), a relock, a fixed 500 ms
 * wait, a gain loop of ~20 steps (3.28-3.56 s), the TOC. The sweep's length is set by the quality figure, which the servo
 * ASSUMES (lowest at E8h, rising 4 a register step): a real disc's flatter or noisier figure could make the sweep five
 * times longer - or the time could be anywhere else. psx-spx's test command 19h,60h reads a byte of the drive's RAM while
 * it works; on the servo the phases show there: [49h] 21h -> 19h at the lock (the sweep on, bit 3), -> A1h at its end,
 * -> 71h at the gain loop; [100h..107h] the sweep's scratch; [48h] and [6Bh] the program's state.
 *
 * WHAT IT DOES. Init and Demute, then "OPEN THE LID", wait for the door switch (Test 19h,21h bit 1), then "SHUT THE
 * LID" (the same disc), and from the moment the switch reads shut, every 20 ms: 19h,60h on 49h, 48h, 6Bh, 1CDh, 1CFh and
 * 100h in turn until [49h] bit 3 has set and cleared (every change kept with its time, printed after) - then GetlocP every
 * 10 ms, as cd-lid, until it answers a position: test commands during the TOC search restart the program's seek (on the servo
 * [6Bh] flips 1 / 13 at every poll and the disc is never read). Interrupts off, every wait bounded, Timer 2 at sysclock/8
 * unwrapped (cd-lid's framework).
 *
 * THE RULE: run it here first (the runner plays the person at the lid):
 *   `node hwtests/run.js cd-lid-sweep --disc --lid --frames=6000 --quiet=2000`
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

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static const uint32_t SECOND = 33868800;

/* Wait for a given INT, acknowledging others; returns it (5 = error). */
static uint8_t waitInt(uint8_t want) {
    const uint32_t c0 = clk8;                                   /* rule 1: 0 after 10 s, never a deaf console */
    for (;;) {
        tick();
        uint8_t f = irqFlag() & 7;
        if (!f) { if (clk8 - c0 > 42336000u) return 0; continue; }
        if (f == want || f == 5) return f;
        /* An old board's flag can read 1 before 3 (psx-spx "Unstable IRQ Flag polling"; the SCPH-1001's cd-irq-glitch: once
         * in 3,000 Getstats): read it again before taking it for another interrupt, or the INT3 is acknowledged here and
         * waited for for ever - cd-seek-map hung the 1001 twice this way (2026-10-09). The normal path is unchanged. */
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }

/*
 * THE DOOR'S OWN INTERRUPT. Opening the lid makes the drive send INT5 with 08h unasked (psx-spx: "These values
 * appear even if no command was sent"). Taken as a command's answer it would pair every later answer with the
 * command before it - this test's first version read the door "shut" 1.5 s before it was, on this core. So it is
 * noted (doorInts) and skipped wherever it turns up.
 */
static int doorInts;
static uint8_t doorStat;
static bool takeDoor(uint8_t f, uint8_t b1, uint8_t b0) { if (f == 5 && b1 == 0x08) { doorInts++; doorStat = b0; return true; } return false; }

/* One command's first answer as INT << 16 | byte0 << 8 | byte1 (0 = none in 50 ms). */
static uint32_t ask(uint8_t cmd, int n, uint8_t p0) {
    for (int i = 0; i < 4 && (irqFlag() & 7); i++) {          /* nothing left over: the door's INT5, or a sector */
        uint8_t f = irqFlag() & 7, b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
        drain(); ackAll(); takeDoor(f, b1, b0);
    }
    if (n) param(p0);
    command(cmd);
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = irqFlag() & 7;
        if (f) {
            uint8_t b0 = hasResp() ? resp() : 0xFF;
            uint8_t b1 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            if (takeDoor(f, b1, b0)) continue;
            return ((uint32_t)f << 16) | ((uint32_t)b0 << 8) | b1;
        }
        if (now() - t0 > SECOND / 20) return 0;
    }
}
/* A second response a command may still send (ReadN's sectors, an INT2): taken and dropped for 50 ms. */
static void settle() { const uint32_t t0 = now(); while (now() - t0 < SECOND / 20) { if (irqFlag() & 7) { drain(); ackAll(); } } }
static bool doorOpen() { uint32_t r = ask(0x19, 1, 0x21); return (r >> 16) == 3 && (r & 0x0200); }
static void show(uint32_t t, const char *what, uint32_t r) {
    uint32_t ms = t / 33869;                         /* cycles a millisecond, to 0.004% (no 64-bit math here) */
    if (!r) printf("+%d.%03d s  %s  no answer\n", (int)(ms / 1000), (int)(ms % 1000), what);
    else printf("+%d.%03d s  %s  INT%d %02x %02x\n", (int)(ms / 1000), (int)(ms % 1000), what, (int)(r >> 16), (int)((r >> 8) & 0xFF), (int)(r & 0xFF));
}
static void wait10ms() { const uint32_t t0 = now(); while (now() - t0 < SECOND / 100) { } }


/* 19h,60h,lo,hi: a byte of the drive's RAM; -1 when no answer in 50 ms, -1 - the flag on another interrupt. */
static int peek(uint16_t addr) {
    for (int i = 0; i < 4 && (irqFlag() & 7); i++) {
        uint8_t f = irqFlag() & 7, b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
        drain(); ackAll(); takeDoor(f, b1, b0);
    }
    param(0x60); param(addr & 0xFF); param(addr >> 8);
    command(0x19);
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = irqFlag() & 7;
        if (f) {
            uint8_t b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            if (takeDoor(f, b1, b0)) continue;
            return f == 3 ? b0 : -1 - f;
        }
        if (now() - t0 > SECOND / 20) return -1;
    }
}

#define NA 10
/* cd-lid-sweep2 (2026-10-23): the sweep's own figures - 102h each sample (CX(812000)'s readback, S_46D4), 100h / 107h the
 * running sum and count, 106h the 16-sample group, 230h the lowest average kept (S_45DC), 46h the step flags, 105h the
 * table index - beside 49h / 48h / 1CFh. cd-lid-sweep on the 7501 (x3): the program sat 2.0 s in 49h = 39h, 48h = 40h
 * without moving the bias, where the servo here leaves after one average (STATUS). */
static const uint16_t ADDR[NA] = { 0x049, 0x048, 0x1CF, 0x102, 0x100, 0x107, 0x106, 0x230, 0x046, 0x105 };
#define NL 2400
#ifndef POLL_MS
#define POLL_MS 20
#endif
static uint32_t logT[NL]; static uint16_t logA[NL]; static int16_t logV[NL]; static int nl;

int main() {
    printf("\ncdrom/cd-lid-sweep2\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    initTimer(2, 2);                                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                   /* Demute */
    int last[NA];
    for (int a = 0; a < NA; a++) last[a] = peek(ADDR[a]);
    printf("before:");
    for (int a = 0; a < NA; a++) printf(" [%xh] %02x", (unsigned)ADDR[a], last[a] & 0xFF);
    printf("\n");
    printf("OPEN THE LID now, and leave it open until asked to shut it.\n");
    while (!doorOpen()) wait10ms();
    { const uint32_t t0 = now(); while (now() - t0 < 2 * SECOND) wait10ms(); }
    printf("SHUT THE LID now, with the same disc in.\n");
    while (doorOpen()) wait10ms();
    const uint32_t shut = now();
    nl = 0;
    for (int a = 0; a < NA; a++) last[a] = 0x7FFF;
    /* THE SWEEP, polled through drive RAM until [49h] bit 3 (the sweep on, on the servo) has been set and cleared - not
     * after: from the TOC search on, every test command restarts the program's seek (on the servo, [6Bh] flipping 1 / 13 at
     * each poll and the disc never read until the polling stopped, 50 ms apart or back to back). */
    int sweepOn = 0; uint32_t sweepFrom = 0, sweepTo = 0;
    while (now() - shut < 10 * SECOND && !sweepTo) {
        const uint32_t round = now();
        for (int a = 0; a < NA; a++) {
            const int v = peek(ADDR[a]);
            if (v != last[a] && nl < NL) { logT[nl] = now() - shut; logA[nl] = ADDR[a]; logV[nl] = (int16_t)v; nl++; }
            last[a] = v;
            if (a == 0 && v >= 0) {
                if ((v & 0x08) && !sweepOn) { sweepOn = 1; sweepFrom = now() - shut; }
                else if (!(v & 0x08) && sweepOn) sweepTo = now() - shut;
            }
        }
        while (now() - round < SECOND / 1000 * POLL_MS) { }
    }
    /* THEN THE DISC, as cd-lid times it: GetlocP every 10 ms until it answers a position (20 s at most). */
    uint32_t ready = 0;
    while (!ready && now() - shut < 20 * SECOND) {
        const uint32_t r = ask(0x11, 0, 0);
        if ((r >> 16) == 3) ready = now() - shut;
        else wait10ms();
    }
    /* Interrupts back on before printing and parking (cd-lid's lesson: Unirom's handler hears the cable through them). */
    ExitCriticalSection();
    printf("after the shut, every change (time: [address] value; -1 no answer, -2.. another interrupt):\n");
    for (int i = 0; i < nl; i++) {
        const uint32_t ms = logT[i] / 33869;
        printf("  +%d.%03d s [%xh] %d%s", (int)(ms / 1000), (int)(ms % 1000), (unsigned)logA[i], (int)logV[i], (i % 4 == 3) ? "\n" : "");
    }
    const uint32_t rms = ready / 33869, fms = sweepFrom / 33869, tms = sweepTo / 33869;
    printf("\n%d changes%s; [49h] bit 3 (the sweep, on the servo) from +%d.%03d s to +%d.%03d s\n", nl, nl >= NL ? " (the log full)" : "",
           (int)(fms / 1000), (int)(fms % 1000), (int)(tms / 1000), (int)(tms % 1000));
    printf("GetlocP answered a position at +%d.%03d s%s\n", (int)(rms / 1000), (int)(rms % 1000), ready ? "" : " (never: the time is 0)");
    printf("the door's own INT5 seen %d time(s)\n", doorInts);
    printf("Done.\n");
    for (;;) { }
}
