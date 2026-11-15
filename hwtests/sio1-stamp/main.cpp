/*
 * sio1/sio1-stamp - ONE CONSOLE AS THE OTHER'S STOPWATCH (2026-11-15; the six-console tests' "make them talk" item, the console
 * fuzzer session). sio1-clocks has the bridge Pico's relay stamp each console's marks on the Pico's microseconds (34 CPU cycles a
 * tick). Here each console stamps the OTHER's marks on its own Timer 2 - one CPU cycle a tick - while sending its own: console B's
 * events timed by console A's clock, with no timer read on B's side beyond the one that schedules its mark. The fit of A's arrival
 * stamps against B's own send stamps gives the two crystals' ratio to ~0.02 ppm in 30 s, and B's events (here a fixed period; a CD
 * INT1 or an SPU IRQ in a variant) placed on A's cycle grid - and as the crystals drift, B's edges sweep A's sampling phase, so the
 * mean of many is finer than A's poll.
 *
 * WHAT IT DOES. As sio1-clocks: under hwtests/runner, '#' from the PC (tools/linkrun.js), 1.5 s, then SIO1 to 2,116,800 baud (MODE x1,
 * reload 16: a bit is 16 CPU cycles, a frame 160 - the relay mirrors TXD in PIO at any rate). For 30 s: a byte A5h every 262,144 CPU
 * cycles (its send time kept), and every received byte's arrival stamped on Timer 2 (the poll ~12 cycles a turn). Then back to the
 * runner's 115,200 8N1 x16 before the relay closes, '!' from the PC, and the stamps printed:
 *   T <n> <first> <deltas...>   this console's send times (Timer 2 cycles, extended past its wrap)
 *   R <n> <first> <deltas...>   its receive stamps of the other console's bytes
 * tools/linkrun.js --test=sio1-stamp runs it on both and fits them (--analyse).
 *
 * Run here first: `node hwtests/run.js sio1-stamp` (with no '#' it waits 60 s, then runs; alone it receives nothing).
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
#define PERIOD 262144u
#define NMAX 4096

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t TX[NMAX], RX[NMAX];

static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }
static uint32_t rxDrained = 0;
static void drainRx() {
    for (int i = 0; i < 4096 && (read16(SIO_STAT) & 2); i++) { (void)read8(SIO_DATA); rxDrained++; }
    write16(SIO_CTRL, read16(SIO_CTRL) | ACKE);
}
static void slow() { write16(SIO_MODE, 0x004E); write16(SIO_BAUD, 0x0012); write16(SIO_CTRL, TXEN | RXEN | DTR | RTS); }
static void waitBang() {
    uint32_t acc = 0, secs = 0, p = now();
    while (secs < 240) {
        const uint32_t q = now(); acc += q - p; p = q;
        while (acc >= 33868800u) { acc -= 33868800u; secs++; }
        if ((read16(SIO_STAT) & 2) && read8(SIO_DATA) == '!') break;
    }
}
static void dump(char tag, const uint32_t *v, uint32_t n) {
    printf("%c %u %u", tag, (unsigned)n, (unsigned)(n ? v[0] : 0));
    for (uint32_t i = 1; i < n; i++) { printf(" %u", (unsigned)(v[i] - v[i - 1])); if ((i & 31) == 0) printf("\n%c+", tag); }
    printf("\n");
}

int main() {
    printf("\nsio1/sio1-stamp\nEach console stamps the other's SIO1 marks on its own Timer 2 (the other's stopwatch).\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    slow();
    drainRx();
    uint32_t t0 = now(); bool go = false;
    while (now() - t0 < MS(60000)) { if (read16(SIO_STAT) & 2) { if (read8(SIO_DATA) == '#') { go = true; break; } } }
    idle(MS(1500));

    write16(SIO_MODE, 0x004D);                                /* 8N1, x1 */
    write16(SIO_BAUD, 16);                                    /* 33,868,800 / 16 = 2,116,800 baud */
    write16(SIO_CTRL, TXEN | RXEN | DTR | RTS);
    drainRx();
    uint32_t nT = 0, nR = 0, lost = 0, errs = 0, txMiss = 0;
    const uint32_t start = now(), end = start + MS(30000);
    uint32_t next = start + PERIOD;
    for (;;) {
        const uint32_t t = now();
        if ((int32_t)(t - end) >= 0) break;
        const uint16_t s = read16(SIO_STAT);
        if (s & 2) { const uint32_t r = now(); (void)read8(SIO_DATA); if (nR < NMAX) RX[nR++] = r; else lost++; if (s & 0x38) errs++; }
        if ((int32_t)(t - next) >= 0) {
            if (s & 1) { write8(SIO_DATA, 0xA5); if (nT < NMAX) TX[nT++] = now(); }
            else txMiss++;
            next += PERIOD;
        }
    }
    idle(MS(20));
    drainRx();
    slow();                                                   /* back to the runner's rate before the relay closes */
    drainRx();
    waitBang();
    drainRx();
    ExitCriticalSection();
    printf("go %s; sent %u (every %u cycles, %u missed at TX busy), received %u (%u past %u kept, %u with error bits); RX drained %u\n",
           go ? "received" : "NOT received (ran after 60 s)", (unsigned)nT, (unsigned)PERIOD, (unsigned)txMiss, (unsigned)nR, (unsigned)lost, NMAX, (unsigned)errs, (unsigned)rxDrained);
    dump('T', TX, nT);
    dump('R', RX, nR);
    printf("sio1/sio1-stamp: Done.\n");
    return 0;
}
