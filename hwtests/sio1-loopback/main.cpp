/*
 * sio/sio1-loopback - SIO1, the serial port, timed from the inside (M252).
 *
 * The emulator this was written for models SIO1 as a UART from psx-spx (nocash's
 * DIAG measurements) and Sony's Link Cable Library chapter, because no emulator
 * models it (DuckStation: registers only; Redux: bytes over TCP; MAME: no start
 * or stop bits). Two consoles on a cable pass Sony's libcomb samples and Doom's
 * deathmatch there, which says the model is good enough for software - not
 * that its cycles are right. This measures them, on ONE console.
 *
 * RUN IT WITH A LOOPBACK PLUG in the serial port (the original PlayStation's
 * 8-pin socket; the PSone has none): pin 4 TXD to pin 1 RXD, pin 6 DTR to
 * pin 3 DSR, pin 8 RTS to pin 5 CTS (psx-spx "Pinouts - SIO Pinouts"). Without
 * a plug the first two sections still run, and the RX rows say "no RX".
 * There are no assertions - compare against your emulator.
 *
 * WHAT IT DECIDES:
 *  - the rate: MAX((BAUD * factor) AND NOT 1, factor) cycles a bit (psx-spx);
 *  - when SR.0 and SR.2 set after a TX_DATA write: after the start bit, and
 *    after the last stop bit (the live psx-spx page; the vendored one says
 *    "cleared" for SR.0);
 *  - whether a start waits for the baud timer, and for a half-bit or a whole
 *    one: the TX_DATA write is made 0, 1/4, 1/2 and 3/4 of a bit after the
 *    BAUD write that reloaded the timer;
 *  - with the plug: when the byte is in the receiver (the model: 9.5 bits, the
 *    middle of the stop bit), the FIFO's overrun and empty-read measurements
 *    (psx-spx), 16/32-bit reads, and the interrupt re-trigger on acknowledge;
 *  - with nothing plugged in: DSR and CTS (Sony: 1 when unconnected; psx-spx
 *    says the PSone pulls them DOWN at the CPU - a PSone may read 0).
 *
 * ON THIS CORE AT v0.253 (2026-09-27, `node hwtests/run.js sio1-loopback
 * --loopback --press-start`: the emulated port cabled to itself, START pressed
 * at each prompt): every row is the model's own prediction, so the test reads
 * what it claims.
 *   8N1 at 576 cycles a bit: SR.0 763, SR.2 5,953, RX 5,655 - a start ~190
 *   after the write (the next half-bit reload), then 1, 10 and 9.5 bits; 8N2
 *   and 8N1.5 end at 11 and 10.5 bits, parity adds one, 5N1 is 7 bits and
 *   reads 1Ah for 5Ah; MUL1 BAUD 0101h runs as 0100h (AND NOT 1); the 9,600
 *   row (BAUD D8h, libsio's) ends at 36,216.
 *   THE START: writes 33 / 165 / 309 / 453 / 741 after the BAUD write give
 *   SR.0 899 / 877 / 1,166 / 1,165 / 1,453 after it - half-bit (288) steps. A
 *   console that starts on whole bits reads ~1,152 for the first four and
 *   ~1,728; one that starts at once reads write + 576.
 *   FIFO: nine bytes read 01..07 09 then 00s with SR.4 set; five and a sixth
 *   read 11..16 16 16 00 00; a 16-bit read a2a1 and a 32-bit a5a4a3a2 leave 1.
 *   IRQ: RX 1 (SR.9 1), acknowledged with a byte left 1 again, with none 0;
 *   TX while idle 1; DSR with DTR off 0, on 1. Baud timer 2,042 then 1,512.
 *   Without the plug: DSR 1 CTS 1 either way, and the same SR.0/SR.2.
 * The 16-cycle row is finer than the poll loop (~30 cycles an iteration):
 * read it as "the byte got through", not as a timing.
 *
 * THE CABLE SWAP. Unirom talks to the PC over this same port, so the test
 * prints its header and its instructions, then goes by itself: the TV turns
 * BLUE (put the loopback plug in), YELLOW once it sees the plug (measuring,
 * the results held in RAM), GREEN (put the serial cable back); once it sees
 * the plug gone it puts Unirom's port settings back and prints. Nothing is
 * pressed (2026-10-12: the first version's two START presses on pad 1 failed
 * on the SCPH-9001 - see plugIn). Run here with `--loopback-auto`, which
 * plugs the emulated loopback in and takes it out again.
 */
#include <common.h>
#include <timer.h>
#include <psxapi.h>
#include <psxpad.h>
#include <stdio.h>
#include <stdarg.h>

#define SIO_DATA 0x1f801050
#define SIO_STAT 0x1f801054
#define SIO_MODE 0x1f801058
#define SIO_CTRL 0x1f80105a
#define SIO_BAUD 0x1f80105e
#define I_STAT   0x1f801070

#define TXEN  0x0001
#define DTR   0x0002
#define RXEN  0x0004
#define ACK   0x0010
#define RTS   0x0020
#define RESET 0x0040
#define TXIEN 0x0400
#define RXIEN 0x0800
#define DSRIEN 0x1000
#define ON (TXEN | DTR | RXEN | RTS)

static bool plugged = false;   // DSR/CTS follow DTR/RTS: a loopback plug is in

/*
 * The serial port is Unirom's link to the PC AND the port under test, so the
 * results wait in RAM while the loopback plug is in, and go out once the cable
 * is back (see plugIn).
 */
static char report[16384];
static int reportLen = 0;
static void out(const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(tmp, fmt, ap);
    va_end(ap);
    for (const char* q = tmp; *q && reportLen < (int)sizeof(report) - 1; q++) report[reportLen++] = *q;
    report[reportLen] = 0;
}

/* A long string through the kernel's printf in 60-character pieces: its %s cuts one off at ~88 (MEASURED on the
 * emulator this was written for, which runs the real BIOS: a 94-character prompt arrived as 89). */
static void say(const char* text) {
    char piece[61];
    for (int i = 0; text[i]; i += 60) {
        int n = 0;
        while (n < 60 && text[i + n]) { piece[n] = text[i + n]; n++; }
        piece[n] = 0;
        printf("%s", piece);
        if (n < 60) break;
    }
}
static void port(uint16_t mode, uint16_t baud, uint16_t ctrl) {
    write16(SIO_CTRL, RESET);
    write16(SIO_MODE, mode);
    write16(SIO_BAUD, baud);          // reloads the baud timer
    write16(SIO_CTRL, ctrl);
}
static inline uint16_t sr() { return read16(SIO_STAT); }
/* Timer 2 at the system clock, its mode put back after (a counted loop ran 2.2 times long here). */
static void waitMs(uint32_t ms) {
    const uint16_t m = read16(0x1f801124);
    write16(0x1f801124, 0);
    uint16_t last = read16(0x1f801120);
    uint32_t acc = 0;
    const uint32_t want = ms * 33869u;
    while (acc < want) { const uint16_t t = read16(0x1f801120); acc += (uint16_t)(t - last); last = t; }
    write16(0x1f801124, m);
}

/*
 * THE TV TELLS THE PERSON WHAT TO DO, AND NOTHING IS PRESSED (2026-10-12). The first version asked for START on pad
 * 1 twice, with the cable out between: on the SCPH-9001 "press START" read as Enter on the PC, and the console waited
 * for a START that never came. Now the screen is BLUE while the console waits for the loopback plug, YELLOW while it
 * measures, GREEN when the serial cable can go back - and the console sees for itself which is in (plugIn): the plug
 * makes DSR/CTS follow DTR/RTS and sends every byte back; the serial cable has only TX, RX and ground (its handshake
 * inputs read high, as an empty port's do) and the PC sends nothing back. Nothing reaches the PC while the plug is in;
 * the results wait in RAM until the cable is back.
 */
#define GP0 0x1f801810
#define GP1 0x1f801814
#define BLUE   0xff0000
#define YELLOW 0x00ffff
#define GREEN  0x00ff00
/* GPUSTAT bit 26 (ready for a command word) only before the command: it is clear while the GPU collects a command's
 * parameters, so waiting on it before each of them hangs at the second word (it did, here, the first try). */
static void gp0cmd(uint32_t v) { while (!(read32(GP1) & 0x04000000)) {} write32(GP0, v); }
static void gp0(uint32_t v) { write32(GP0, v); }
static void screen(uint32_t bgr) {
    write32(GP1, 0x08000001);                          // 320 wide, NTSC, 15-bit
    write32(GP1, 0x05000000);                          // the picture from VRAM (0,0)
    write32(GP1, 0x06000000 | 0x260 | (0xc60 << 12));  // horizontal range
    write32(GP1, 0x07000000 | 0x10 | (0x100 << 10));   // vertical range
    gp0cmd(0x02000000 | bgr);                          // fill rectangle
    gp0(0x00000000);                                   //   at (0,0)
    gp0((240 << 16) | 320);                            //   320 x 240
    write32(GP1, 0x03000000);                          // display on
}
static bool linesFollow() {
    write16(SIO_CTRL, 0);
    waitMs(2);
    const uint16_t off = sr();
    write16(SIO_CTRL, DTR | RTS);
    waitMs(2);
    const uint16_t on = sr();
    return ((off >> 7) & 3) != ((on >> 7) & 3);
}
static bool echoes() {
    port(0x4e, 0x0012, TXEN | RXEN | DTR | RTS);       // 8N1 at ~115,200, Unirom's rate
    while (sr() & 2) read8(SIO_DATA);
    static const uint8_t pat[3] = {0xa5, 0x5a, 0xc3};
    for (int i = 0; i < 3; i++) {
        write8(SIO_DATA, pat[i]);
        bool got = false;
        for (int k = 0; k < 20000 && !got; k++) if (sr() & 2) { if (read8(SIO_DATA) != pat[i]) return false; got = true; }
        if (!got) return false;
    }
    return true;
}
/* 1 the plug, seen by its lines; 2 by its echo (the handshake wires not joined); 0 none in two minutes. The echo is
 * tried only after 20 s of lines that do not follow, so a serial cable not yet pulled gets no bytes it did not ask for. */
static int plugIn() {
    int run = 0;
    for (int t = 0; t < 2400; t++) {                   // ~50 ms a look, two minutes
        if (linesFollow()) { if (++run >= 10) return 1; } else run = 0;
        if (t >= 400 && t % 20 == 0 && echoes() && echoes()) return 2;
        waitMs(50);
    }
    return 0;
}
/* At most two minutes, then print anyway: on the SCPH-9001 (2026-10-12, the fifth try) the console sat on GREEN with the
 * serial cable back - something on that cable's spare handshake wires still made the lines follow. */
static void plugOut(int how) {
    int run = 0;
    for (int t = 0; t < 2400 && run < (how == 1 ? 20 : 5); t++) {
        if (how == 1 ? !linesFollow() : !echoes()) run++; else run = 0;
        waitMs(50);
    }
}

/* Spin until SR has `mask` (or not), at most `limit` polls; the Timer2 count then, or -1. */
static int32_t until(uint16_t mask, bool set, uint32_t limit) {
    for (uint32_t i = 0; i < limit; i++) {
        uint16_t s = sr();
        if (((s & mask) != 0) == set) return readTimer(2) + (timerDidOverflow(2) ? 0xffff : 0);
    }
    return -1;
}

/* One byte out: cycles from the TX_DATA write to SR.0, to SR.2 and (with a plug) to SR.1. */
static void txRow(const char* name, uint16_t mode, uint16_t baud, uint32_t bitCycles) {
    EnterCriticalSection();
    uint16_t prev = initTimer(2, 0);
    port(mode, baud, ON);
    while (sr() & 2) read8(SIO_DATA);                // empty the receiver
    resetTimer(2);
    write8(SIO_DATA, 0x5a);
    int32_t w = readTimer(2);
    // One loop watches all three bits, so no bit waits behind another's poll; it ends when SR.2 is set and,
    // with a plug, the byte is in (or when Timer2 wraps: 65,536 cycles, past the slowest row).
    int32_t t0 = -1, t1 = -1, t2 = -1, last = w;
    for (;;) {
        uint16_t s = sr();
        int32_t t = readTimer(2);
        if (t < last) break;
        last = t;
        if (t0 < 0 && (s & 1)) t0 = t;
        if (t1 < 0 && (s & 2)) t1 = t;
        if (t2 < 0 && (s & 4)) t2 = t;
        if (t2 >= 0 && (t1 >= 0 || !plugged)) break;
    }
    uint8_t got = (sr() & 2) ? read8(SIO_DATA) : 0;
    restoreTimer(2, prev);
    ExitCriticalSection();
    out("  %-26s bit %5u  SR.0 %6d  SR.2 %6d  RX %6d (%02x)\n", name, (unsigned)bitCycles,
           t0 < 0 ? -1 : t0 - w, t2 < 0 ? -1 : t2 - w, t1 < 0 ? -1 : t1 - w, got);
}

/* Where a start lands: TX_DATA written `after` cycles after the BAUD write. */
static void phaseRow(uint32_t after, uint32_t bitCycles) {
    EnterCriticalSection();
    uint16_t prev = initTimer(2, 0);
    write16(SIO_CTRL, RESET);
    write16(SIO_MODE, 0x4e);
    write16(SIO_CTRL, ON);
    resetTimer(2);
    write16(SIO_BAUD, 0x0024);                      // 576 cycles a bit at MUL16
    int32_t b = readTimer(2);
    while ((int32_t)readTimer(2) < b + (int32_t)after) {}
    int32_t w = readTimer(2);
    write8(SIO_DATA, 0xa5);
    int32_t t0 = until(1, true, 200000);
    until(4, true, 200000);
    while (sr() & 2) read8(SIO_DATA);
    restoreTimer(2, prev);
    ExitCriticalSection();
    out("  write %4d after BAUD      SR.0 %5d after the write (%5d after BAUD)\n",
           (int)(w - b), t0 < 0 ? -1 : t0 - w, t0 < 0 ? -1 : t0 - b);
    (void)bitCycles;
}

/* Send bytes one after another, each when TX_DATA is free (SR.0). */
static void sendAll(const uint8_t* bytes, int n) {
    for (int i = 0; i < n; i++) {
        until(1, true, 200000);
        write8(SIO_DATA, bytes[i]);
    }
    until(4, true, 200000);
    for (volatile int k = 0; k < 2000; k++) {}      // the last byte's stop bit into the receiver
}

/* Timer 2 at the system clock, unwrapped: read at least every 65,536 cycles (the polling loops below do). */
static uint32_t clk2; static uint16_t last2;
static void t2start() { write16(0x1f801124, 0); last2 = read16(0x1f801120); clk2 = 0; }
static uint32_t t2now() { const uint16_t t = read16(0x1f801120); clk2 += (uint16_t)(t - last2); last2 = t; return clk2; }
static void flushRx() { waitMs(1); while (sr() & 2) read8(SIO_DATA); }
static const char* ago(int32_t v, char* buf) { if (v < 0) return "never"; sprintf(buf, "%d", (int)v); return buf; }

/*
 * CTS AND THE TRANSMITTER (2026-10-12).
 * psx-spx: "CTS required for TX". With the plug, RTS drives CTS, so the console can ask it of itself: is a byte written
 * with CTS off held, and does it go when CTS comes on; does CTS off in the middle of a byte stop it; does CTS off hold
 * a second byte queued behind one already going. Runs only when LINES saw CTS follow RTS.
 */
static void ctsSection() {
    out("\nCTS AND THE TRANSMITTER (RTS drives CTS through the plug; 8N1 MUL16 BAUD 0024h, a bit is 576; one byte ~6,300)\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1f801124);
    char b1[12], b2[12], b3[12], b4[12], b5[12], b6[12];
    uint32_t t;
    uint16_t s;
    {   // 1. written with CTS off; CTS on 40,000 cycles later
        port(0x4e, 0x0024, TXEN | RXEN | DTR);
        flushRx();
        const int ctsBefore = (sr() >> 8) & 1;
        t2start();
        write8(SIO_DATA, 0xa5);
        int32_t s0 = -1, s2 = -1, rx = -1;
        while ((t = t2now()) < 40000) { s = sr(); if (s0 < 0 && (s & 1)) s0 = t; if (s2 < 0 && (s & 4)) s2 = t; if (rx < 0 && (s & 2)) rx = t; }
        write16(SIO_CTRL, TXEN | RXEN | DTR | RTS);
        const uint32_t on = t2now();
        int32_t a0 = -1, a2 = -1, ar = -1;
        while ((t = t2now()) < on + 40000) { s = sr(); if (a0 < 0 && (s & 1)) a0 = t - on; if (a2 < 0 && (s & 4)) a2 = t - on; if (ar < 0 && (s & 2)) ar = t - on; if (a2 >= 0 && ar >= 0) break; }
        const uint8_t got = (sr() & 2) ? read8(SIO_DATA) : 0;
        out("  1. a byte written with CTS %s: in 40,000 cycles SR.0 %s SR.2 %s RX %s\n", ctsBefore ? "ON (the plug's RTS-CTS pair is not working)" : "off",
            ago(s0, b1), ago(s2, b2), ago(rx, b3));
        out("     then CTS on: SR.0 +%s SR.2 +%s RX +%s (%02x)\n", ago(a0, b4), ago(a2, b5), ago(ar, b6), got);
    }
    {   // 2. CTS off three bits into a byte
        port(0x4e, 0x0024, ON);
        flushRx();
        t2start();
        write8(SIO_DATA, 0x3c);
        while (t2now() < 576 * 3) {}
        write16(SIO_CTRL, TXEN | RXEN | DTR);
        const uint32_t d = t2now();
        int32_t e2 = -1, er = -1;
        while ((t = t2now()) < d + 40000) { s = sr(); if (e2 < 0 && (s & 4)) e2 = t; if (er < 0 && (s & 2)) er = t; if (e2 >= 0 && er >= 0) break; }
        const uint8_t got = (sr() & 2) ? read8(SIO_DATA) : 0;
        out("  2. CTS off %u cycles after the write (3 bits into the byte): SR.2 at %s, RX at %s (%02x)\n", (unsigned)d, ago(e2, b1), ago(er, b2), got);
    }
    {   // 3. a second byte queued behind one going, CTS off as it is queued; on 40,000 cycles later
        port(0x4e, 0x0024, ON);
        flushRx();
        t2start();
        write8(SIO_DATA, 0x11);
        while (!(sr() & 1) && t2now() < 20000) {}          // the first in the shift register: TX_DATA free
        const uint32_t q = t2now();
        write8(SIO_DATA, 0x22);
        write16(SIO_CTRL, TXEN | RXEN | DTR);
        int32_t r1 = -1, r2 = -1; uint8_t v1 = 0, v2 = 0;
        while ((t = t2now()) < q + 40000) { if (sr() & 2) { const uint8_t v = read8(SIO_DATA); if (r1 < 0) { r1 = t; v1 = v; } else if (r2 < 0) { r2 = t; v2 = v; } } }
        write16(SIO_CTRL, TXEN | RXEN | DTR | RTS);
        const uint32_t on = t2now();
        int32_t r3 = -1; uint8_t v3 = 0;
        while ((t = t2now()) < on + 40000) { if (sr() & 2) { const uint8_t v = read8(SIO_DATA); if (r1 < 0) { r1 = t; v1 = v; } else if (r2 < 0) { r2 = t; v2 = v; } else if (r3 < 0) { r3 = t - on; v3 = v; } } if (r2 >= 0 && t > on + 15000) break; }
        out("  3. second byte queued at %u, CTS off with it: RX %s (%02x), %s (%02x) - the second at +%s after CTS on if it waited (then %s %02x)\n",
            (unsigned)q, ago(r1, b1), v1, ago(r2, b2), v2, r2 >= 0 && (uint32_t)r2 > on ? ago((int32_t)(r2 - on), b3) : "-", ago(r3, b4), v3);
    }
    write16(0x1f801124, m2);
    port(0x4e, 0x0001, ON);
    ExitCriticalSection();
}

static void measure(int how);

int main() {
    printf("\nsio/sio1-loopback\n");
    printf("SIO1 timed from the inside: rate, SR.0/SR.2, the start's alignment, and with a loopback plug\n");
    printf("(4-1 TXD-RXD, 6-3 DTR-DSR, 8-5 RTS-CTS) the receiver, the FIFO and the interrupts. No assertions.\n\n");
    const uint16_t uMode = read16(SIO_MODE), uBaud = read16(SIO_BAUD), uCtrl = read16(SIO_CTRL);   // Unirom's port
    say(">>> NO BUTTONS IN THIS TEST - WATCH THE TV. When it turns BLUE: take the serial cable out of the console and put the loopback plug in. YELLOW: it is measuring (a second or two). GREEN: take the loopback plug out and put the serial cable back - no hurry: the results come 20 s after the plug is out.");
    printf("\n");
    waitMs(3000);                                      // the message out over the cable before it is pulled
    screen(BLUE);
    const int how = plugIn();
    waitMs(1500);                                      // the plug pushed fully home
    screen(YELLOW);
    measure(how);
    waitMs(3000);                                      // yellow long enough to be seen (it was over in about a second)
    screen(GREEN);
    if (how) plugOut(how);
    waitMs(20000);   // time to get the serial cable in: the plug's lines go quiet as it comes OUT, and 2.5 s here sent the
                     // results into an empty socket on the 9001's fifth and sixth tries (2026-10-12) - lost, the test done
    write16(SIO_MODE, uMode);                          // Unirom's port back before a byte is printed: the
    write16(SIO_BAUD, uBaud);                          // measurements leave it at their own rates
    write16(SIO_CTRL, uCtrl);
    say(report);
    printf("\nDone.\n");
    for (;;) {}
}

static void measure(int how) {
    out("THE PLUG: %s\n\n", how == 1 ? "seen by its lines" : how == 2 ? "seen by its echo only (DSR/CTS did not follow DTR/RTS)" : "not seen in two minutes - measured as an empty port");
    /*
     * Each output on its own, and what each input does: the SCPH-9001's first try with the new flow (2026-10-12) read
     * DSR following RTS and CTS stuck at 1 - one handshake pair joined crossed, the other not making contact - and the
     * old two-state test called that "crossed" and the plug absent, though every data byte came back.
     */
    out("LINES (SR.7 DSR, SR.8 CTS; each output on its own)\n");
    port(0x4e, 0x00dc, 0);
    uint16_t s[4];
    static const uint16_t outs[4] = {0, DTR, RTS, DTR | RTS};
    for (int i = 0; i < 4; i++) { write16(SIO_CTRL, outs[i]); waitMs(2); s[i] = sr(); }
    out("  off: DSR %d CTS %d   DTR: DSR %d CTS %d   RTS: DSR %d CTS %d   both: DSR %d CTS %d\n",
        (s[0] >> 7) & 1, (s[0] >> 8) & 1, (s[1] >> 7) & 1, (s[1] >> 8) & 1, (s[2] >> 7) & 1, (s[2] >> 8) & 1, (s[3] >> 7) & 1, (s[3] >> 8) & 1);
    auto source = [&](int bit) -> const char* {
        const int a = (s[0] >> bit) & 1, d = (s[1] >> bit) & 1, r = (s[2] >> bit) & 1, b = (s[3] >> bit) & 1;
        if (!a && d && !r && b) return "DTR";
        if (!a && !d && r && b) return "RTS";
        if (a == d && d == r && r == b) return a ? "nothing (reads 1)" : "nothing (reads 0)";
        return "both or neither in turn (unclear)";
    };
    const char* dsrSrc = source(7);
    const char* ctsSrc = source(8);
    out("  DSR follows %s; CTS follows %s", dsrSrc, ctsSrc);
    const bool dsrOk = dsrSrc[0] == 'D' && dsrSrc[1] == 'T', ctsOk = ctsSrc[0] == 'R' && ctsSrc[1] == 'T';
    out("%s\n", dsrOk && ctsOk ? " - joined as they should be"
        : (dsrSrc[0] == 'R' || ctsSrc[0] == 'D') ? " - CROSSED: the wire on DTR belongs with the one on DSR, RTS's with CTS's" : "");
    plugged = echoes();                                // the receiver rows need only the data pair
    out("  data comes back (TXD to RXD): %s\n\n", plugged ? "yes" : "no (the receiver rows below will say no RX)");

    out("ONE BYTE: cycles from the TX_DATA write (Timer2, CPU clock)\n");
    txRow("8N1 MUL16 BAUD 0024h", 0x4e, 0x0024, 576);
    txRow("8N2 MUL16 BAUD 0024h", 0xce, 0x0024, 576);
    txRow("8N1.5 MUL16 BAUD 0024h", 0x8e, 0x0024, 576);
    txRow("8O1 MUL16 BAUD 0024h", 0x5e, 0x0024, 576);
    txRow("8E1 MUL16 BAUD 0024h", 0x7e, 0x0024, 576);
    txRow("5N1 MUL16 BAUD 0024h", 0x42, 0x0024, 576);
    txRow("8N1 MUL1 BAUD 0100h", 0x4d, 0x0100, 256);
    txRow("8N1 MUL1 BAUD 0101h (odd)", 0x4d, 0x0101, 256);
    txRow("8N1 MUL64 BAUD 0010h", 0x4f, 0x0010, 1024);
    txRow("8N1 MUL16 BAUD 0001h", 0x4e, 0x0001, 16);
    txRow("8N1 MUL16 BAUD 00D8h", 0x4e, 0x00d8, 3456);

    out("\nTHE START'S ALIGNMENT (8N1 MUL16 BAUD 0024h, a bit is 576)\n");
    phaseRow(0, 576);
    phaseRow(144, 576);
    phaseRow(288, 576);
    phaseRow(432, 576);
    phaseRow(720, 576);

    if (!plugged) {
        out("\nNo loopback plug: the receiver sections are skipped.\n");
        return;
    }

    out("\nTHE FIFO (8N1 MUL16 BAUD 0001h)\n");
    {
        port(0x4e, 0x0001, ON);
        uint8_t nine[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        sendAll(nine, 9);
        uint16_t s = sr();
        out("  nine bytes, none read: SR.1 %d SR.4 (overrun) %d; reads:", (s >> 1) & 1, (s >> 4) & 1);
        for (int i = 0; i < 12; i++) out(" %02x", read8(SIO_DATA));
        out("  (SR.1 now %d)\n", (sr() >> 1) & 1);
        port(0x4e, 0x0001, ON);
        uint8_t five[5] = {0x11, 0x12, 0x13, 0x14, 0x15}, six = 0x16;
        sendAll(five, 5);
        sendAll(&six, 1);
        out("  five, then a sixth: reads");
        for (int i = 0; i < 10; i++) out(" %02x", read8(SIO_DATA));
        out("  (psx-spx: the sixth is stored thrice; after six reads it reads twice more, then 00h)\n");
        port(0x4e, 0x0001, ON);
        uint8_t w6[6] = {0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6};
        sendAll(w6, 6);
        uint16_t h = read16(SIO_DATA);
        uint32_t w = read32(SIO_DATA);
        int left = 0;
        while ((sr() & 2) && left < 10) { read8(SIO_DATA); left++; }
        out("  six, then a 16-bit read %04x and a 32-bit read %08x: %d left (a 16-bit read takes one, a 32-bit four)\n", h, (unsigned)w, left);
    }

    out("\nINTERRUPTS (I_STAT.8, polled; interrupts stay off)\n");
    /* The CPU's interrupts off and SIO's masked for the section: on the SCPH-9001 (2026-10-12) every row here read 0 -
     * Unirom runs its own SIO handler for the PC link and took each IRQ (I_STAT.8 and SR.9 acknowledged) before the
     * poll saw it. */
    EnterCriticalSection();
    const uint16_t imask = read16(0x1f801074);
    write16(0x1f801074, imask & (uint16_t)~0x100);
    {
        port(0x4e, 0x0001, ON | RXIEN);                 // RX IRQ at 1 byte
        write16(I_STAT, (uint16_t)~0x100);
        uint8_t one = 0x42;
        sendAll(&one, 1);
        int first = (read16(I_STAT) >> 8) & 1, sr9 = (sr() >> 9) & 1;
        write16(I_STAT, (uint16_t)~0x100);
        write16(SIO_CTRL, ON | RXIEN | ACK);            // acknowledge with the byte still there
        for (volatile int k = 0; k < 100; k++) {}
        int again = (read16(I_STAT) >> 8) & 1;
        read8(SIO_DATA);
        write16(I_STAT, (uint16_t)~0x100);
        write16(SIO_CTRL, ON | RXIEN | ACK);            // and with it read
        for (volatile int k = 0; k < 100; k++) {}
        int quiet = (read16(I_STAT) >> 8) & 1, sr9b = (sr() >> 9) & 1;
        out("  RX: I_STAT.8 %d SR.9 %d; ack with a byte left: I_STAT.8 %d; ack with none: I_STAT.8 %d SR.9 %d\n",
               first, sr9, again, quiet, sr9b);
        port(0x4e, 0x0001, ON);
        write16(I_STAT, (uint16_t)~0x100);
        write16(SIO_CTRL, ON | TXIEN);
        for (volatile int k = 0; k < 100; k++) {}
        int tx = (read16(I_STAT) >> 8) & 1;
        port(0x4e, 0x0001, TXEN | RXEN | RTS);           // DTR off
        write16(I_STAT, (uint16_t)~0x100);
        write16(SIO_CTRL, TXEN | RXEN | RTS | DSRIEN);
        for (volatile int k = 0; k < 100; k++) {}
        int dsrOff = (read16(I_STAT) >> 8) & 1;
        write16(SIO_CTRL, TXEN | RXEN | RTS | DSRIEN | DTR);
        for (volatile int k = 0; k < 100; k++) {}
        int dsrOn = (read16(I_STAT) >> 8) & 1;
        out("  TX IRQ while idle: I_STAT.8 %d; DSR IRQ with DTR off %d, then DTR on %d (meaningful only if LINES says DSR follows DTR)\n", tx, dsrOff, dsrOn);
    }
    port(0x4e, 0x0001, ON);                             // no SIO IRQ enabled when the mask goes back
    write16(I_STAT, (uint16_t)~0x100);
    write16(0x1f801074, imask);
    ExitCriticalSection();

    out("\nTHE BAUD TIMER (SR bits 11-31; 8N1 MUL16 BAUD 0100h: reloads with 2048)\n");
    {
        EnterCriticalSection();
        uint16_t prev = initTimer(2, 0);
        port(0x4e, 0x0100, ON);
        resetTimer(2);
        write16(SIO_BAUD, 0x0100);
        uint32_t a = read32(SIO_STAT); int32_t ta = readTimer(2);
        while ((int32_t)readTimer(2) < ta + 500) {}
        uint32_t b = read32(SIO_STAT); int32_t tb = readTimer(2);
        restoreTimer(2, prev);
        ExitCriticalSection();
        out("  %5u at %d cycles after the BAUD write, %5u at %d\n", (unsigned)(a >> 11), (int)ta, (unsigned)(b >> 11), (int)tb);
    }

    if (ctsOk) ctsSection();
    else out("\nCTS AND THE TRANSMITTER: skipped - LINES did not see CTS follow RTS\n");
}
