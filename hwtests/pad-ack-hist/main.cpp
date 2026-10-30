/*
 * sio/pad-ack-hist - the controller's /ACK delay as a distribution, not a range.
 *
 * WHY (2026-10-08, the consensus over console day's logs). ack-per-device on the SCPH-5501, 7501 and 9001 read a
 * controller's /ACK over a wide range after each byte's write - byte 1 +1,574..1,808, byte 3 +1,478..1,748 - where this
 * core acknowledges a fixed 338 cycles after the byte (JaCzekanski via DuckStation, "~338 ticks") and reads
 * +1,643..1,748 through the same loop. The same source gives the delay as "between 6.8us-13.7us" (230-464 cycles): a
 * range. Sixteen repetitions and a 37-cycle polling loop give only the extremes. The model needs the distribution.
 *
 * WHAT IT DOES. SIO0 by hand, interrupts off, 250 kHz (JOY_BAUD 88h), port 1's controller, the poll every game sends
 * (01 42 00 00 00). Each byte is timed by a loop that reads ONE thing - JOY_STAT until /ACK (bit 7) goes low, and
 * Timer 2 once after - so its resolution is one pass of a three-instruction loop. 256 polls for /ACK, then 256 for RX
 * (bit 1, the byte received) the same way. Per byte 1-4: a histogram in 8-cycle bins of the cycles from the byte's
 * write to /ACK and to RX, and the mean.
 *
 * READ IT LIKE THIS. Each "t:n" is n polls whose event came in [t, t+8). RX against /ACK per byte is the device's own
 * delay after the byte; a flat spread says the pad's controller answers at a random phase of its own clock, a few
 * spikes say a loop. Say which controller was plugged in (ack-per-device's console runs had a digital pad: "TYPE : 6
 * free button or flying-V form").
 *
 * ON THIS CORE AT v0.377.41 (a digital pad): /ACK byte 1 mean 1,718 (1,712-1,720 all but one), bytes 2-4 ~1,744 (1,728-
 * 1,752); RX byte 1 1,424, byte 2 split 1,336 / 1,464 (126 / 129: the free-running baud clock's bit edge), bytes 3-4
 * 1,464. The pad's own delay is one number here; a console's spread is the pad's.
 *
 * THE RULE: run it here first: `node hwtests/run.js pad-ack-hist`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define T2 0x1F801120
#define REPS 256
#define BIN 8
#define LO 1200
#define NBINS 128                                  /* 1,200 .. 2,224 */

static uint16_t hist[2][4][NBINS + 2];             /* [ack/rx][byte][bin]; NBINS = below, NBINS+1 = above */
static uint32_t sum[2][4], cnt[2][4];
static int none[2][4];

static inline uint16_t t2() { return read16(T2); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }

/* The time from the write to `bit` set in JOY_STAT, or -1: a tight loop on one register, then the timer. */
static int __attribute__((noinline)) until(uint32_t bit, uint16_t t0) {
    for (int i = 0; i < 2000; i++) if (read32(JOY_STAT) & bit) return (uint16_t)(t2() - t0);
    return -1;
}

static void poll(int kind) {                       /* kind 0: /ACK, 1: RX */
    static const uint8_t tx[5] = { 0x01, 0x42, 0x00, 0x00, 0x00 };
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003);                     /* TX enable, /JOY low on port 1 */
    wait(1500);
    for (int b = 0; b < 4; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        const int t = until(kind == 0 ? 0x80 : 0x02, t0);
        if (t < 0) { none[kind][b]++; break; }
        sum[kind][b] += t; cnt[kind][b]++;
        const int i = (t - LO) / BIN;
        hist[kind][b][t < LO ? NBINS : (i >= NBINS ? NBINS + 1 : i)]++;
        /* let the byte and its /ACK pulse finish before the next */
        if (kind == 1) until(0x80, t0);
        for (int k = 0; k < 4000 && (read32(JOY_STAT) & 0x80); k++) { }
        wait(400);
    }
    write16(JOY_CTRL, 0x0000);
}

int main() {
    printf("\nsio/pad-ack-hist\n");
    printf("Port 1's controller, 01 42 00 00: cycles from each byte's write to /ACK and to RX, %d polls each, %d-cycle bins.\n", REPS, BIN);
    printf("No assertions - compare against your emulator, and say which controller was in port 1.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    for (int r = 0; r < REPS; r++) poll(0);
    for (int r = 0; r < REPS; r++) poll(1);
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    static const char* names[2] = { "/ACK", "RX" };
    for (int k = 0; k < 2; k++) {
        for (int b = 0; b < 4; b++) {
            const uint32_t n = cnt[k][b];
            printf("%s byte %d: %d polls", names[k], b + 1, (int)n);
            if (n) printf(", mean %d.%02d", (int)(sum[k][b] / n), (int)((100 * (sum[k][b] % n)) / n));
            if (none[k][b]) printf(", %d never", none[k][b]);
            printf("\n ");
            int col = 0;
            if (hist[k][b][NBINS]) { printf(" <%d:%d", LO, hist[k][b][NBINS]); col++; }
            for (int i = 0; i < NBINS; i++) if (hist[k][b][i]) {
                printf(" %d:%d", LO + i * BIN, hist[k][b][i]);
                if (++col % 12 == 0) printf("\n ");
            }
            if (hist[k][b][NBINS + 1]) printf(" >=%d:%d", LO + NBINS * BIN, hist[k][b][NBINS + 1]);
            printf("\n");
        }
        printf("\n");
    }
    printf("sio/pad-ack-hist: Done.\n");
    for (;;) VSync(0);
}
