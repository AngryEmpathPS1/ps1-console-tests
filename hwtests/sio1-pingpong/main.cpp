/*
 * sio1/sio1-pingpong - A BYTE THERE AND BACK BETWEEN TWO CONSOLES, timed on each one's own clock (2026-11-07).
 *
 * WHY. How long the serial chip takes from a write to TX_DATA to the start bit, and from the stop bit to SR.1 (RX ready), sets
 * every link game's pace and this emulator's SIO1 timing (SerialPort: the start after the write, v0.377.83; the receive lag,
 * SIO1_RX_LAG) - measured until now only by one console talking to itself through a loopback plug. Between two consoles the
 * round trip is A's own clock end to end, B's turnaround its own clock, and the bridge Pico's relay stamps both bytes on a
 * third (CURRENT.md "two consoles and the emulator", item 2-3).
 *
 * WHAT IT DOES. Under hwtests/runner (sio1-*: unstoppable), the port set as the runner sets it (MODE 4Eh, BAUD 18: 115,200;
 * TX, RX, DTR, RTS). It waits for '#' and a role letter from the PC (60 s): 'A' sends, 'B' answers (no letter: B).
 *   A: 1,000 rounds - 5 ms (Timer 2), then byte 80h | (k & 7Fh) written (never 'Q', 51h: the first version sent k, and round 81 quit B) to TX_DATA the moment TX is ready, the time
 *      noted; SR.1 polled for the answer (20 ms at most), its time noted - the round trip in A's cycles. 'Q' to B at the end.
 *   B: SR.1 polled in a tight loop; on a byte, its time noted, the byte read, its complement written to TX_DATA as soon as TX is
 *      ready, that time noted - the turnaround in B's cycles; until 'Q', or 30 s with nothing.
 * Nothing prints until the end; then each side's numbers: count, minimum, median, maximum (A: the round trip; B: from SR.1
 * seen to the reply written), and A's lost answers. tools/linkrun.js --test=sio1-pingpong runs it on two consoles (port 1 A,
 * port 2 B); tools/linkemu.js on two emulated ones.
 *
 * Run here first: `node hwtests/run.js sio1-pingpong` (alone it waits 60 s and answers nothing: it needs a peer).
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

static uint32_t samples[1024]; static int ns = 0;
static void sortS() { for (int i = 1; i < ns; i++) { uint32_t v = samples[i]; int j = i - 1; while (j >= 0 && samples[j] > v) { samples[j + 1] = samples[j]; j--; } samples[j + 1] = v; } }
static bool txReady() { return (read16(SIO_STAT) & 1) != 0; }
static bool rxReady() { return (read16(SIO_STAT) & 2) != 0; }

int main() {
    printf("\nsio1/sio1-pingpong\nA byte there and back between two consoles on the link, each end timed on its own clock.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    write16(SIO_MODE, 0x004E); write16(SIO_BAUD, 0x0012); write16(SIO_CTRL, 0x0027);   /* the runner's settings (hwtests/runner) */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);

    /* '#' and the role */
    char role = 'B'; bool go = false;
    const uint32_t t0 = now();
    while (now() - t0 < MS(60000)) {
        if (!rxReady()) continue;
        const uint8_t c = read8(SIO_DATA);
        if (!go) { if (c == '#') go = true; continue; }
        if (c == 'A' || c == 'B') role = (char)c;
        break;
    }
    const uint32_t settle = now(); while (now() - settle < MS(1500)) {}   /* the relay starts */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);

    int lost = 0;
    if (role == 'A') {
        for (int k = 0; k < 1000; k++) {
            const uint32_t w = now(); while (now() - w < MS(5)) {}
            while (!txReady()) {}
            const uint32_t ts = now();
            write8(SIO_DATA, (uint8_t)(0x80 | (k & 0x7F)));
            bool got = false; uint32_t tr = 0;
            while (now() - ts < MS(20)) { if (rxReady()) { tr = now(); (void)read8(SIO_DATA); got = true; break; } }
            if (got) { if (ns < 1024) samples[ns++] = tr - ts; } else lost++;
        }
        while (!txReady()) {}
        write8(SIO_DATA, 'Q');
    } else {
        uint32_t last = now();
        for (;;) {
            if (rxReady()) {
                const uint32_t tr = now();
                const uint8_t c = read8(SIO_DATA);
                if (c == 'Q') break;
                while (!txReady()) {}
                write8(SIO_DATA, (uint8_t)~c);
                const uint32_t tw = now();
                if (ns < 1024) samples[ns++] = tw - tr;
                last = tr;
            }
            if (now() - last > MS(30000)) break;
        }
    }
    waitBang();                                                          /* the relay ends, then the PC's '!' */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, read16(SIO_CTRL) | 0x0010);                        /* error flags acknowledged */
    ExitCriticalSection();
    sortS();
    if (role == 'A') printf("role A: %d round trips, %d answers lost; round trip min %u, median %u, max %u cycles\n",
                            ns, lost, ns ? (unsigned)samples[0] : 0, ns ? (unsigned)samples[ns / 2] : 0, ns ? (unsigned)samples[ns - 1] : 0);
    else printf("role B: %d bytes answered; SR.1 seen to the reply written min %u, median %u, max %u cycles\n",
                ns, ns ? (unsigned)samples[0] : 0, ns ? (unsigned)samples[ns / 2] : 0, ns ? (unsigned)samples[ns - 1] : 0);
    printf("go %s, role %c\nsio1/sio1-pingpong: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
