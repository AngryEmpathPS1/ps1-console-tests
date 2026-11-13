/*
 * sio1/sio1-speed - HOW FAST THE SERIAL LINK BETWEEN A CONSOLE AND THE PC GOES (2026-11-13). A 32 KB block each way at each of
 * SIO1's rates from Unirom's 115,200 baud up to 2,116,800, every byte checked, through the bridge Pico and the PC's serial bridge.
 *
 * WHY. PS1-DOS's serial drive (Windows 3.1/tools/pcdrive.js) runs at 115,200 baud: ~11 KB a second, and DOOM's start reads
 * ~4.9 MB of DOOM.WAD (its sprites and wall patches, measured from the WAD's directory 2026-11-13) - seven minutes and more.
 * Asked for on 2026-11-13: how much faster can it go? SIO1 itself is known to run at 2,116,800 baud (BAUD 1, x16):
 * Duke Nukem: Total Meltdown and Formula 1 link at it, and the bridge Pico's UART was set up to match it (pico-serial/main.c).
 * What no one has measured is a long block both ways at each rate through the whole path: console - Pico UART (receive by
 * interrupt into a 4 KB ring) - USB full speed - tools/serialbridge.ps1 - TCP.
 *
 * WHAT IT DOES. Under hwtests/runner (sio1-* runs unstoppable: the port is the test's), interrupts off, Timer 2 at the system
 * clock. The port set to the runner's own 115,200 8N1 (MODE 4Eh, BAUD 18 - the same values, as sio1-clocks sets them), RTS and
 * DTR asserted. It waits for '#' from the PC (60 s at most), then for each BAUD reload r in 18 9 6 4 3 2 1 (2,116,800 / r baud):
 *   at 115,200  A5h r                    the rate asked for; then the port moves to r (after the stop bit has gone)
 *   at the rate 5Ah from the PC (3 s)    the PC's sync, sent every 20 ms once its bridge is at the rate; answered C3h
 *               7Eh + 32,768 bytes       the PC's block (an LCG from 1000h + r), each byte checked as it comes; ended by the
 *                                        last byte or 500 ms with none
 *               7Eh + 32,768 bytes       this console's block (the LCG from 2000h + r), sent as fast as the port takes them
 *               7Eh + 12 bytes           the PC's count of that block: bytes, bad bytes, the first bad one (3 s)
 *   back at 115,200, 150 ms for the PC's bridge to follow, the receive FIFO emptied.
 * Then A5h 00h (the end), the runner's settings put back, a line per rate printed (the runner sends the log).
 * The LCG is s = s * 1664525 + 1013904223, the byte s >> 24 (tools/sio1speed.js the same).
 * A rate whose sync fails is printed as such and the next tried: both ends go back to 115,200 on their own clocks.
 *
 * tools/sio1speed.js is the PC's side: `node tools/sio1speed.js --link=127.0.0.1:3335` on a console, `--emu` on this emulator.
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
#define TXEN 0x0001
#define DTR  0x0002
#define RXEN 0x0004
#define ACKE 0x0010
#define RTS  0x0020
#define ST_TXRDY   0x01   /* psx-spx SIO_STAT: bit 0 TX ready for a byte, 1 RX not empty, 2 TX idle (FIFO and shifter), */
#define ST_RXRDY   0x02   /* 3 RX parity error, 4 RX FIFO overrun, 5 RX bad stop bit - the three latched until CTRL bit 4 */
#define ST_TXIDLE  0x04
#define ST_ERRS    0x38

#define BASE  18          /* the runner's and Unirom's 115,200 (2,116,800 / 18 = 117,600 on the console's crystal) */
#define BLOCK 32768u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static void drainRx() {
    for (int i = 0; i < 65536 && (read16(SIO_STAT) & ST_RXRDY); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, read16(SIO_CTRL) | ACKE);           /* the error flags a rate change leaves */
}
static bool waitTxIdle(uint32_t limit) {
    const uint32_t t0 = now();
    while (!(read16(SIO_STAT) & ST_TXIDLE)) { if (now() - t0 > limit) return false; }
    return true;
}
static bool put(uint8_t b) {
    const uint32_t t0 = now();
    while (!(read16(SIO_STAT) & ST_TXRDY)) { if (now() - t0 > MS(200)) return false; }
    write8(SIO_DATA, b);
    return true;
}
static int get(uint32_t limit) {
    const uint32_t t0 = now();
    while (!(read16(SIO_STAT) & ST_RXRDY)) { if (now() - t0 > limit) return -1; }
    return read8(SIO_DATA);
}
static void setBaud(uint16_t r) { waitTxIdle(MS(100)); write16(SIO_BAUD, r); }
static inline uint32_t lcg(uint32_t s) { return s * 1664525u + 1013904223u; }

struct Row {
    uint16_t r; bool sync, gotHdr, report;
    uint32_t inGot, inBad, inFirstBad, inFlags, inCycles;        /* the PC's block here */
    uint32_t outSent, outCycles;                                 /* this console's block */
    uint32_t pcGot, pcBad, pcFirstBad;                           /* ... as the PC counted it */
};
static Row rows[8];

static void recvBlock(Row &w) {
    /* its 7Eh: the sync's later 5Ah (the PC sends one every 20 ms until it has the C3h) skipped */
    const uint32_t t0 = now();
    w.gotHdr = false;
    for (;;) {
        if (read16(SIO_STAT) & ST_RXRDY) { if (read8(SIO_DATA) == 0x7E) { w.gotHdr = true; break; } }
        if (now() - t0 > MS(3000)) return;
    }
    uint32_t s = 0x1000u + w.r, got = 0, bad = 0, firstBad = 0, flags = 0, first = 0, last = now();
    while (got < BLOCK) {
        const uint16_t st = read16(SIO_STAT);
        const uint32_t t = now();
        if (st & ST_RXRDY) {
            const uint8_t b = read8(SIO_DATA);
            s = lcg(s);
            if (b != (uint8_t)(s >> 24)) { if (!bad) firstBad = got; bad++; }
            if (!got) first = t;
            got++; last = t;
            flags |= st & ST_ERRS;
        } else if (t - last > MS(500)) break;
    }
    w.inGot = got; w.inBad = bad; w.inFirstBad = firstBad; w.inFlags = flags | (read16(SIO_STAT) & ST_ERRS);
    w.inCycles = last - first;
    write16(SIO_CTRL, read16(SIO_CTRL) | ACKE);
}

static void sendBlock(Row &w) {
    put(0x7E);
    uint32_t s = 0x2000u + w.r, i = 0;
    const uint32_t t0 = now();
    for (; i < BLOCK; i++) {
        s = lcg(s);
        const uint32_t w0 = now();
        bool ok = true;
        while (!(read16(SIO_STAT) & ST_TXRDY)) { if (now() - w0 > MS(500)) { ok = false; break; } }   /* CTS held off, say */
        if (!ok) break;
        write8(SIO_DATA, (uint8_t)(s >> 24));
    }
    waitTxIdle(MS(100));
    w.outSent = i; w.outCycles = now() - t0;
}

static uint32_t word() {
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) { const int b = get(MS(200)); if (b < 0) return 0xFFFFFFFFu; v |= (uint32_t)b << (8 * k); }
    return v;
}

/* bytes a second from bytes and CPU cycles, in 32 bits: bytes per million cycles first (the cycles >= 1,000 x 1,000 here) */
static uint32_t perSecond(uint32_t bytes, uint32_t cycles) {
    if (cycles < 1000000u) return 0;
    const uint32_t bpm = bytes * 1000u / (cycles / 1000u);
    return bpm * 33869u / 1000u;
}

int main() {
    printf("\nsio1/sio1-speed\nA 32 KB block each way between this console and the PC at each SIO1 rate (2,116,800 / BAUD), every byte checked.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    const uint16_t mode0 = read16(SIO_MODE), ctrl0 = read16(SIO_CTRL), baud0 = read16(SIO_BAUD);
    write16(SIO_MODE, 0x004E);                             /* 8N1, x16 */
    write16(SIO_BAUD, BASE);
    write16(SIO_CTRL, TXEN | RXEN | DTR | RTS);
    drainRx();

    uint32_t t0 = now(); bool go = false;
    while (now() - t0 < MS(60000)) {
        if (read16(SIO_STAT) & ST_RXRDY) { if (read8(SIO_DATA) == '#') { go = true; break; } }
    }

    static const uint16_t RELOADS[] = { 18, 9, 6, 4, 3, 2, 1 };
    const int n = sizeof(RELOADS) / sizeof(RELOADS[0]);
    for (int k = 0; k < n && go; k++) {
        Row &w = rows[k];
        w.r = RELOADS[k];
        put(0xA5); put((uint8_t)w.r);
        setBaud(w.r);
        drainRx();
        /* the PC's sync at the new rate */
        w.sync = false;
        t0 = now();
        while (now() - t0 < MS(3000)) {
            if (read16(SIO_STAT) & ST_RXRDY) { if (read8(SIO_DATA) == 0x5A) { w.sync = true; break; } }
        }
        if (w.sync) {
            put(0xC3);
            recvBlock(w);
            idle(MS(30));                                   /* the PC turns to listen */
            sendBlock(w);
            /* the PC's count of it */
            w.report = false;
            t0 = now();
            while (now() - t0 < MS(3000)) {
                if (read16(SIO_STAT) & ST_RXRDY) { if (read8(SIO_DATA) == 0x7E) { w.report = true; break; } }
            }
            if (w.report) { w.pcGot = word(); w.pcBad = word(); w.pcFirstBad = word(); }
        }
        setBaud(BASE);
        idle(MS(150));
        drainRx();
    }
    put(0xA5); put(0x00);
    waitTxIdle(MS(100));
    write16(SIO_MODE, mode0); write16(SIO_BAUD, baud0); write16(SIO_CTRL, ctrl0);
    ExitCriticalSection();

    if (!go) printf("No '#' from the PC in 60 s: nothing measured.\n");
    printf("BAUD  rate       sync | PC->PS1 got   bad first  flags  B/s     | PS1->PC sent  B/s     | PC got  bad first\n");
    for (int k = 0; k < n && go; k++) {
        const Row &w = rows[k];
        const uint32_t rate = 2116800u / w.r;
        if (!w.sync) { printf("%4u  %7u  no sync\n", w.r, rate); continue; }
        printf("%4u  %7u  ok   | %5u %5u %5u  %02x   %6u  | %5u %6u  | ",
            w.r, rate, w.gotHdr ? w.inGot : 0, w.inBad, w.inBad ? w.inFirstBad : 0, w.inFlags, perSecond(w.inGot, w.inCycles),
            w.outSent, perSecond(w.outSent, w.outCycles));
        if (w.report) printf("%5u %5u %5u\n", w.pcGot, w.pcBad, w.pcBad ? w.pcFirstBad : 0);
        else printf("no report\n");
    }
    printf("Done.\n");
    return 0;
}
