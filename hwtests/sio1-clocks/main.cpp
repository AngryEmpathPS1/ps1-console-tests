/*
 * sio1/sio1-clocks - EACH CONSOLE'S CLOCKS AGAINST AN OUTSIDE ONE (2026-11-07). A byte out of SIO1 at moments the console's own
 * clocks decide; the bridge Pico's relay (pico-serial/relay.c) stamps each one on ITS clock, the same clock for both consoles on
 * its two ports - so each console's CPU crystal, video crystal and CD drive come out against the Pico's and against each other's.
 *
 * WHY. Until now a console was only ever timed by itself: every hwtest reads the console's own timer. The emulator takes the
 * ratio of each console's video clock and drive to its CPU clock from those (VIDEO_RATIO_*, CD sector times), and the CPU's own
 * rate as nominal (33,868,800 Hz). An outside clock measures all three, unit by unit (CURRENT.md "two consoles and the
 * emulator", item 3).
 *
 * WHAT IT DOES. Under hwtests/runner (its serial port the test's: sio1-* runs unstoppable, the host sends nothing during it but
 * one '#'. Interrupts off, Timer 2 at the system clock. SIO1 left at the runner's 115,200 8N1, RTS and DTR asserted (in the
 * relay each console's CTS is the other's RTS). It waits for '#' from the PC (not 'G': that is the runner's own log command, should it come early) (60 s at most), then 1.5 s for the relay to start,
 * then three phases, each mark one byte:
 *   C  CPU: C1h every 1,048,576 CPU cycles (Timer 2), 1,024 marks (~31.7 s at 33.8688 MHz)
 *   V  video: B1h at each VBlank (I_STAT bit 0, polled and acknowledged), 1,800 marks (~30 s)
 *   D  the drive: Setmode 00h (single speed), Setloc 10:00:00, ReadN, D1h at each INT1, 2,250 sectors (~30 s) - when a disc
 *      reads; otherwise D0h once
 * then 8 s for the relay to end, the other console's bytes drained from the FIFO, and a summary printed (the runner sends it).
 * A mark is written the moment its event is seen (TX ready waited first: at 115,200 a byte is ~2,940 cycles, the shortest gap
 * here ~558,000). The Pico stamps a byte when its stop bit has arrived: a constant offset, so periods are exact.
 * tools/linkrun.js runs it on both consoles with the relay on; tools/relaylog.js reads the records.
 *
 * Run here first: `node hwtests/run.js sio1-clocks --disc` (with no '#' it waits 60 s, then runs).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define MS(n) ((uint32_t)(n) * 33869u)

#define SIO_DATA 0x1F801050u
#define SIO_STAT 0x1F801054u
#define SIO_CTRL 0x1F80105Au
#define TXEN 0x0001
#define DTR  0x0002
#define RXEN 0x0004
#define ACKE 0x0010
#define RTS  0x0020

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t p) { idx(0); *CD2 = p; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* THE END: the PC's '!' (tools/linkrun.js sends it once the relay has closed and the bridge is the PC's again). The first real
 * session (2026-11-07) idled 8 s here: the console that finished first printed into the relay, its runner's report never
 * reached the PC and the runner waited for ever. 240 s at most, longer than linkrun's 150-s relay. */
static void waitBang() {
    uint32_t acc = 0, secs = 0, p = now();                 /* 240 s in whole seconds, past Timer 2's 126-s wrap */
    while (secs < 240) {
        const uint32_t q = now(); acc += q - p; p = q;
        while (acc >= 33868800u) { acc -= 33868800u; secs++; }
        if ((read16(SIO_STAT) & 2) && read8(SIO_DATA) == '!') break;
    }
}
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint32_t txWaitMax = 0;
static void mark(uint8_t b) {
    const uint32_t t0 = now();
    while (!(read16(SIO_STAT) & 1)) { if (now() - t0 > MS(5)) break; }
    const uint32_t w = now() - t0; if (w > txWaitMax) txWaitMax = w;
    write8(SIO_DATA, b);
}
static uint32_t rxDrained = 0;
static void drainRx() {
    for (int i = 0; i < 4096 && (read16(SIO_STAT) & 2); i++) { (void)read8(SIO_DATA); rxDrained++; }
    write16(SIO_CTRL, read16(SIO_CTRL) | ACKE);   /* the error flags the other console's bytes may have left */
}

static uint8_t take(uint32_t limit, uint8_t *st) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    const uint8_t s = hasResp() ? resp() : 0;
    if (st) *st = s;
    drain();
    if (f == 1) { idx(0); *CD3 = 0x80; idx(0); *CD3 = 0x00; }   /* the sector taken and dropped (BFRD up and down) */
    ackAll();
    return f;
}
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t st = 0, f;
    const uint32_t t0 = now();
    do { f = take(MS(3000), &st); } while (f == 1 && now() - t0 < MS(3000));
    if (second && f == 3) { uint8_t s2; do { f = take(MS(10000), &s2); } while (f == 1 && now() - t0 < MS(10000)); }
    return f;
}

int main() {
    printf("\nsio1/sio1-clocks\nEach console's CPU, video and drive clocks marked on SIO1 for an outside clock (the bridge Pico's relay).\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    /* the runner's own port settings, set again (the same values, so a console's runner finds them as it left them): a program
     * sideloaded at the shell (tools/linkemu.js, hwtests/run.js) meets a port no one has set, MODE 0 and BAUD 0, which neither
     * sends nor receives - the first twin run sent no byte */
    write16(0x1F801058u, 0x004E);                             /* MODE: 8N1, x16 */
    write16(0x1F80105Eu, 0x0012);                             /* BAUD 18: Unirom's and the runner's 115,200 (hwtests/runner) */
    write16(SIO_CTRL, TXEN | RXEN | DTR | RTS);
    drainRx();

    /* '#' from the PC: the relay is about to start */
    uint32_t t0 = now(); bool go = false;
    while (now() - t0 < MS(60000)) {
        if (read16(SIO_STAT) & 2) { if (read8(SIO_DATA) == '#') { go = true; break; } }
    }
    idle(MS(1500));

    /* C: the CPU's own clock */
    uint32_t nC = 0, target = now() + 1048576u;
    for (; nC < 1024; nC++) { while ((int32_t)(now() - target) < 0) {} mark(0xC1); target += 1048576u; }

    /* V: the video clock */
    uint32_t nV = 0, vMissed = 0;
    write16(I_STAT, (uint16_t)~1u);
    const uint32_t vStart = now();
    uint32_t lastV = 0;
    while (nV < 1800 && now() - vStart < MS(40000)) {
        if (read16(I_STAT) & 1) {
            write16(I_STAT, (uint16_t)~1u);
            mark(0xB1); nV++;
            const uint32_t t = now(); if (lastV && t - lastV > 800000) vMissed++; lastV = t;   /* > 1.4 frames: one went by unseen */
        }
    }

    /* D: the drive, if a disc reads */
    uint32_t nD = 0; uint8_t st = 0;
    ackAll(); drain();
    cmd(0x0A, 0, 0, true);                                    /* Init */
    { const uint8_t m[1] = { 0x00 }; cmd(0x0E, m, 1, false); }
    { const uint8_t loc[3] = { 0x10, 0x02, 0x00 }; cmd(0x02, loc, 3, false); }   /* 10:02:00 = LBA 45,000 */
    command(0x06);
    const uint32_t dStart = now();
    bool reading = false;
    while (nD < 2250 && now() - dStart < MS(45000)) {
        const uint8_t f = take(MS(3000), &st);
        if (f == 1) { mark(0xD1); nD++; reading = true; }
        else if (f == 5 || f == 0) { if (!reading) break; }
    }
    if (!nD) mark(0xD0);
    cmd(0x09, 0, 0, true);                                    /* Pause */

    waitBang();                                               /* the relay ends; then the bridge is the PC's again, and its '!' */
    drainRx();
    ExitCriticalSection();
    printf("go %s; CPU marks %u (every 1048576 cycles); video marks %u (%u gaps over 1.4 frames); drive marks %u (last stat %02x); TX wait max %u cycles; RX drained %u\n",
           go ? "received" : "NOT received (ran after 60 s)", (unsigned)nC, (unsigned)nV, (unsigned)vMissed, (unsigned)nD, st, (unsigned)txWaitMax, (unsigned)rxDrained);
    printf("sio1/sio1-clocks: Done.\n");
    return 0;
}
