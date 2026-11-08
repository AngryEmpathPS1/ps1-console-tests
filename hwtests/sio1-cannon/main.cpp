/*
 * sio1/sio1-cannon - BURSTS INTO A RECEIVER THAT IS NOT READING: how many bytes a console's SIO1 keeps, which ones, and its flags
 * (2026-11-07).
 *
 * WHY. psx-spx gives SIO1 an 8-byte receive FIFO with an overrun flag (SIO_STAT bit 4) and this emulator models that
 * (SerialPort's receiver); which byte an overrun loses (the newest, or the last one overwritten), when the flag sets, and what
 * SR.1 and the FIFO read after it were never measured - one console's loopback plug could not send faster than it read.
 * Between two consoles one sends while the other does not read (CURRENT.md "two consoles and the emulator", item 7).
 *
 * WHAT IT DOES. Under hwtests/runner (sio1-*: unstoppable), the port as the runner sets it (MODE 4Eh, BAUD 18: 115,200; TX, RX,
 * DTR, RTS). '#' and a role letter from the PC (60 s): 'A' fires, 'B' receives (no letter: B).
 *   For each burst size N of 1, 4, 7, 8, 9, 12, 16 (each three times): A sends AAh (sync), waits 20 ms, then N bytes back to back
 *   (each written as soon as TX is ready: 10i+1, 10i+2 ... for burst i), then waits 60 ms. B waits for AAh (reading as it comes),
 *   then reads NOTHING for 40 ms (the burst lands meanwhile), then notes SIO_STAT, drains the FIFO byte by byte noting each
 *   value and SIO_STAT after it, acknowledges the error flags (CTRL bit 4), and goes back to waiting.
 *   At the end A sends 'Q'.
 * Nothing prints until the end; then B's table: per burst N, the bytes it read (in order), STAT before the drain and after,
 * whether the overrun flag (bit 4) was set. A prints its own count. tools/linkrun.js --test=sio1-cannon on two consoles (port 1
 * A, port 2 B); tools/linkemu.js on two emulated ones.
 *
 * Run here first: `node hwtests/run.js sio1-cannon` (alone it waits 60 s and receives nothing: it needs a peer).
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
static void waitMs(uint32_t ms) { const uint32_t t = now(); while (now() - t < MS(ms)) {} }
static bool txReady() { return (read16(SIO_STAT) & 1) != 0; }
static bool rxReady() { return (read16(SIO_STAT) & 2) != 0; }
static void put(uint8_t b) { const uint32_t t = now(); while (!txReady() && now() - t < MS(5)) {} write8(SIO_DATA, b); }

static const int SIZES[7] = { 1, 4, 7, 8, 9, 12, 16 };
struct Burst { int n; uint16_t before, after; int got; uint8_t v[20]; uint16_t st[20]; };
static Burst bursts[21];

int main() {
    printf("\nsio1/sio1-cannon\nBursts into a receiver that is not reading: what SIO1 keeps, which bytes, and its flags.\n");
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
    waitMs(1500);                                                          /* the relay starts */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);

    int nb = 0, sent = 0;
    if (role == 'A') {
        for (int s = 0; s < 7; s++) for (int r = 0; r < 3; r++) {
            put(0xAA); waitMs(20);
            for (int k = 1; k <= SIZES[s]; k++) { put((uint8_t)(10 * (s * 3 + r) + k)); sent++; }
            waitMs(60);
        }
        put('Q');
    } else {
        uint32_t idle = now();
        while (nb < 21 && now() - idle < MS(30000)) {
            if (!rxReady()) continue;
            const uint8_t c = read8(SIO_DATA);
            if (c == 'Q') break;
            if (c != 0xAA) continue;
            Burst &b = bursts[nb++];
            b.n = SIZES[(nb - 1) / 3];
            waitMs(40);                                                    /* not reading: the burst lands */
            b.before = read16(SIO_STAT);
            b.got = 0;
            while (rxReady() && b.got < 20) { b.v[b.got] = read8(SIO_DATA); b.st[b.got] = read16(SIO_STAT); b.got++; }
            b.after = read16(SIO_STAT);
            write16(SIO_CTRL, 0x0027 | 0x0010);                            /* the error flags acknowledged */
            idle = now();
        }
    }
    waitBang();                                                            /* the relay ends, then the PC's '!' */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);
    ExitCriticalSection();
    if (role == 'A') printf("role A: %d bytes fired in 21 bursts\n", sent);
    else {
        printf("role B: %d bursts (sent N back to back while not reading; STAT bits: 1 RX ready, 3 parity, 4 OVERRUN, 5 framing)\n", nb);
        for (int i = 0; i < nb; i++) {
            const Burst &b = bursts[i];
            printf("N %2d: STAT %04x -> got %2d:", b.n, b.before, b.got);
            for (int k = 0; k < b.got; k++) printf(" %02x", b.v[k]);
            printf(" | after %04x%s\n", b.after, (b.before & 0x10) ? " OVERRUN" : "");
        }
    }
    printf("go %s, role %c\nsio1/sio1-cannon: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
