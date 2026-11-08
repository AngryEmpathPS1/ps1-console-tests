/*
 * sio1/sio1-gun - A STARTING GUN: both consoles start the same job at A's signal, and each says when it has finished (2026-11-07).
 *
 * WHY. Two consoles compared until now ran the same test at different times, each against its own clock. With a gun, the two
 * start together (A at its write of the gun byte, B the moment SR.1 shows it - the byte's ten bits and B's receive later, ~3,150
 * cycles on the emulated pair, measured by sio1-pingpong) and the bridge Pico's relay stamps both finishes on one clock: the
 * difference is the two units' difference, trial by trial (CURRENT.md "two consoles and the emulator", item 3). The CPU job is the control - two consoles counting the same cycles must finish the gun's delay apart,
 * give or take their crystals; the CD seek is the measurement - how two drives' seeks of the same distance compare.
 *
 * WHAT IT DOES. Under hwtests/runner (sio1-*: unstoppable), the port as the runner sets it (MODE 4Eh, BAUD 18; TX, RX, DTR,
 * RTS). '#' and a role letter from the PC (60 s): 'A' fires, 'B' follows (no letter: B). 24 trials, 400 ms apart: A sends F0h
 * (the gun) and starts; B starts when it sees F0h. Even trials: the CPU job, 1,000,000 cycles on Timer 2. Odd trials: the CD job
 * - the drive first sent to 05:00:00 (SeekL to its INT2) BEFORE the gun, then at the gun Setloc 60:00:00 and SeekL, to its
 * INT2 (or INT5, or 8 s). At the end each sends D0h | trial (low 4 bits) and notes its own cycles from start to end.
 * Then 8 s for the relay to end; each prints its table (trial, job, its cycles, the CD answer). A sends 'Q' at the end.
 * tools/linkrun.js --test=sio1-gun on two consoles (port 1 A, port 2 B); tools/linkemu.js on two emulated ones.
 *
 * Run here first: `node hwtests/run.js sio1-gun --disc` (alone it waits 60 s, then runs as B with no gun: nothing).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)
#define SIO_DATA 0x1F801050u
#define SIO_STAT 0x1F801054u
#define SIO_MODE 0x1F801058u
#define SIO_CTRL 0x1F80105Au
#define SIO_BAUD 0x1F80105Eu

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
static void idle(uint32_t c) { const uint32_t t = now(); while (now() - t < c) {} }
static bool txReady() { return (read16(SIO_STAT) & 1) != 0; }
static bool rxReady() { return (read16(SIO_STAT) & 2) != 0; }
static void put(uint8_t b) { const uint32_t t = now(); while (!txReady() && now() - t < MS(5)) {} write8(SIO_DATA, b); }

/* the next CD flag (0: none in `limit`), its first response byte into *st, acknowledged; a first 1 or 4 read again */
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
    if (f == 1) { idx(0); *CD3 = 0x80; idx(0); *CD3 = 0x00; }
    ackAll();
    return f;
}
/* Setloc mm:ss:00 then SeekL: the flag that ended it (2 done, 5 an error, 0 none in 8 s) */
static uint8_t seek(uint8_t mm, uint8_t ss, uint8_t *st) {
    const uint8_t loc[3] = { mm, ss, 0x00 };
    for (int i = 0; i < 3; i++) param(loc[i]);
    command(0x02); take(MS(3000), st);
    command(0x15);
    uint8_t f = take(MS(3000), st);                                  /* INT3 */
    if (f == 3) f = take(MS(8000), st);                              /* INT2 or INT5 */
    return f;
}

struct Trial { uint8_t job, flag, st; uint32_t cycles; };
static Trial trials[24];

int main() {
    printf("\nsio1/sio1-gun\nA starting gun: both consoles start the same job at A's signal; each says when it has finished.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    write16(SIO_MODE, 0x004E); write16(SIO_BAUD, 0x0012); write16(SIO_CTRL, 0x0027);   /* the runner's settings */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    ackAll(); drain();

    char role = 'B'; bool go = false;
    const uint32_t t0 = now();
    while (now() - t0 < MS(60000)) {
        if (!rxReady()) continue;
        const uint8_t c = read8(SIO_DATA);
        if (!go) { if (c == '#') go = true; continue; }
        if (c == 'A' || c == 'B') role = (char)c;
        break;
    }
    idle(MS(1500));                                                     /* the relay starts */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    command(0x0A); { uint8_t st; if (take(MS(3000), &st) == 3) take(MS(10000), &st); }   /* Init: the drive spinning */

    int n = 0;
    for (; n < 24; n++) {
        Trial &t = trials[n];
        t.job = n & 1; t.flag = 0; t.st = 0;
        if (t.job) { uint8_t st; seek(0x05, 0x00, &st); }               /* the CD job starts from 05:00 on both */
        uint32_t start;
        if (role == 'A') { idle(MS(400)); while (!txReady()) {} start = now(); write8(SIO_DATA, 0xF0); }
        else {
            const uint32_t w = now(); bool got = false;
            while (now() - w < MS(3000)) { if (rxReady()) { start = now(); if (read8(SIO_DATA) == 0xF0) { got = true; break; } } }
            if (!got) break;
        }
        if (!t.job) idle(1000000);
        else t.flag = seek(0x60, 0x00, &t.st);
        t.cycles = now() - start;
        put((uint8_t)(0xD0 | (n & 15)));
    }
    if (role == 'A') put('Q');
    waitBang();                                                         /* the relay ends, then the PC's '!' */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);
    ExitCriticalSection();
    printf("role %c: %d trials (cycles from its start to its end; CD: the SeekL 05:00 -> 60:00 that ended it)\n", role, n);
    for (int i = 0; i < n; i++) {
        const Trial &t = trials[i];
        if (!t.job) printf("%2d CPU  %8u\n", i, (unsigned)t.cycles);
        else printf("%2d CD   %8u  INT%d %02x\n", i, (unsigned)t.cycles, t.flag, t.st);
    }
    printf("go %s, role %c\nsio1/sio1-gun: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
