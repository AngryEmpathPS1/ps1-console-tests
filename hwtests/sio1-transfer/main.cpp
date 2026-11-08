/*
 * sio1/sio1-transfer - 16 KB FROM ONE CONSOLE TO THE OTHER WITH HARDWARE FLOW CONTROL: what crosses, how fast, and how the
 * transmitter answers the receiver's RTS (2026-11-07).
 *
 * WHY. SIO1 starts a byte only with CTS on (psx-spx; this emulator's SerialPort.tryStart), and on the link cable a console's CTS is
 * the other's RTS: a receiver that drops RTS stops the sender. How many bytes still arrive after RTS drops (the one in flight,
 * one waiting in TX_DATA?), how soon the sender resumes when RTS returns, and whether a block crosses whole are what a game or
 * a transfer tool meets (CURRENT.md "two consoles and the emulator": shipping anything over the link).
 *
 * WHAT IT DOES. Under hwtests/runner (sio1-*: unstoppable), the port as the runner sets it (MODE 4Eh, BAUD 18: 115,200; TX, RX,
 * DTR, RTS). '#' and a role letter from the PC (60 s): 'A' sends, 'B' receives (no letter: B). A: 16,384 bytes of a known
 * sequence (an LFSR, x = x * 1103515245 + 12345, the top byte) written as soon as TX is ready, the longest wait for TX noted.
 * B: every byte checked against the same sequence; after each 1,024 it drops RTS (CTRL bit 5), notes how many more bytes still
 * arrive in the next 20 ms, raises RTS, and notes the cycles from that write to the next byte's SR.1. 30 s with nothing ends it.
 * Then 8 s for the relay to end; each prints its numbers. tools/linkrun.js --test=sio1-transfer on two consoles (port 1 A,
 * port 2 B); tools/linkemu.js on two emulated ones.
 *
 * Run here first: `node hwtests/run.js sio1-transfer` (alone it waits 60 s and receives nothing: it needs a peer).
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
#define N_BYTES 16384

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
static uint32_t lfsr = 0x1234567u;
static uint8_t nextByte() { lfsr = lfsr * 1103515245u + 12345u; return (uint8_t)(lfsr >> 24); }

static uint8_t afterStop[16]; static uint32_t resume[16];

int main() {
    printf("\nsio1/sio1-transfer\n16 KB from one console to the other with hardware flow control.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    write16(SIO_MODE, 0x004E); write16(SIO_BAUD, 0x0012); write16(SIO_CTRL, 0x0027);   /* the runner's settings */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);

    char role = 'B'; bool go = false;
    const uint32_t t0 = now();
    while (now() - t0 < MS(60000)) {
        if (!rxReady()) continue;
        const uint8_t c = read8(SIO_DATA);
        if (!go) { if (c == '#') go = true; continue; }
        if (c == 'A' || c == 'B') role = (char)c;
        break;
    }
    idle(MS(1500));                                                      /* the relay starts */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);

    uint32_t total = 0, waitMax = 0; int got = 0, bad = 0, stops = 0, overruns = 0;
    if (role == 'A') {
        const uint32_t s = now();
        for (int i = 0; i < N_BYTES; i++) {
            const uint32_t w = now();
            while (!txReady()) { if (now() - w > MS(2000)) break; }
            const uint32_t d = now() - w; if (d > waitMax) waitMax = d;
            write8(SIO_DATA, nextByte());
        }
        while (!(read16(SIO_STAT) & 4)) { if (now() - s > MS(60000)) break; }   /* the last byte out (TX idle, SR.2) */
        total = now() - s;
    } else {
        const uint32_t s = now(); uint32_t last = now();
        while (got < N_BYTES && now() - last < MS(30000)) {
            if (!rxReady()) continue;
            const uint16_t st = read16(SIO_STAT);
            if (st & 0x10) overruns++;
            const uint8_t c = read8(SIO_DATA);
            if (c != nextByte()) bad++;
            got++; last = now();
            if ((got & 1023) == 0 && got < N_BYTES && stops < 16) {
                write16(SIO_CTRL, 0x0007);                                   /* RTS off: the sender's CTS drops */
                const uint32_t p = now(); int more = 0;
                while (now() - p < MS(20)) { if (rxReady()) { const uint8_t c2 = read8(SIO_DATA); if (c2 != nextByte()) bad++; got++; more++; } }
                afterStop[stops] = (uint8_t)more;
                write16(SIO_CTRL, 0x0027);                                   /* RTS on */
                const uint32_t r = now();
                while (!rxReady() && now() - r < MS(100)) {}
                resume[stops] = now() - r;
                stops++;
                last = now();
            }
        }
        total = now() - s;
    }
    waitBang();                                                          /* the relay ends, then the PC's '!' */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);
    ExitCriticalSection();
    if (role == 'A') printf("role A: %d bytes sent in %u cycles (%u a byte), the longest wait for TX ready %u cycles\n",
                            N_BYTES, (unsigned)total, (unsigned)(total / N_BYTES), (unsigned)waitMax);
    else {
        printf("role B: %d bytes received, %d not the sequence's, %d reads with the overrun flag, in %u cycles\n", got, bad, overruns, (unsigned)total);
        printf("RTS dropped %d times; bytes that still came in the 20 ms after:", stops);
        for (int i = 0; i < stops; i++) printf(" %d", afterStop[i]);
        printf("\nRTS raised: cycles to the next byte's SR.1:");
        for (int i = 0; i < stops; i++) printf(" %u", (unsigned)resume[i]);
        printf("\n");
    }
    printf("go %s, role %c\nsio1/sio1-transfer: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
