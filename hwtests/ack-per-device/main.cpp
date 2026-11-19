/*
 * sio/ack-per-device - when does each device's /ACK come after its byte, and
 * how long is the pulse?
 *
 * WHY (ROAD B: "ack-per-device" and "the card /ACK pulse"; the controller
 * memo's 1.4 and 14.4). This emulator acknowledges a controller's byte 338
 * cycles after it ends - JaCzekanski's measurement, quoted in DuckStation:
 * "ACK delay is between 6.8us-13.7us, or ~338 ticks" - and a memory card's
 * 170 after, which is DuckStation's own number and nobody's measurement. The
 * pulse lasts 96 cycles for both ("~96 ticks", the same source). A game that
 * polls /ACK tightly, or a driver racing RX against the interrupt (the
 * kernel's own does: STATUS, SIO.ackPulse), meets these numbers directly.
 *
 * WHAT IT DOES. SIO0 driven by hand, interrupts off, 250 kHz (JOY_BAUD 88h),
 * for whatever is in each port:
 *   the controller: 01 42 00 00 00 (the poll every game sends);
 *   the memory card: 81 52 00 00 00 00 and six more bytes (a read of frame 0,
 *   stopped after twelve bytes - the card is deselected, as the kernel does
 *   when it gives up).
 * Each byte is written to JOY_DATA and JOY_STAT polled against Timer 2 at the
 * system clock: when RX has the byte (bit 1), when /ACK goes low (bit 7) and
 * when it comes back. Sixteen repetitions; per byte the earliest and latest.
 * A byte with no /ACK within ~4,000 cycles reads "no /ACK" (a controller does
 * not acknowledge its last byte; an empty port acknowledges nothing).
 *
 * READ IT LIKE THIS. "/ACK" is cycles from the byte's write to /ACK going low:
 * the byte itself takes ~1,090 at 250 kHz, so the delay after the byte is that
 * much less. "pulse" is how long /ACK stayed low. The resolution is one pass
 * of the polling loop, printed with the results.
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js ack-per-device`, a digital pad
 * and a formatted card in port 1, port 2 empty), polling at <= 36 cycles:
 *   controller: RX +1,156, /ACK +1,467 on bytes 1-4 (FFh 41h 5Ah FFh), none
 *   after byte 5; pulse 109;
 *   card: /ACK +1,305 on all twelve bytes (FFh 08h 5Ah 5Dh 00h 00h 5Ch 5Dh
 *   00h 00h, then data); pulse 109;
 *   port 2: no /ACK to either address.
 * So /ACK falls ~311 cycles after RX for the controller and ~149 for the card
 * - the model's 338 and 170 after the byte's end, seen through the loop - and
 * the pulse is the model's 96 plus up to a pass of the loop.
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
#define REPS 16
#define MAXB 12

struct ByteStat { int rxMin, rxMax, ackMin, ackMax, pulseMin, pulseMax, acked, silent; int reply; };

static inline uint16_t t2() { return read16(0x1F801120); }

static void wait(int cycles) {
    const uint16_t t0 = t2();
    while ((uint16_t)(t2() - t0) < cycles) { }
}

static void clearStats(ByteStat* s) {
    for (int i = 0; i < MAXB; i++) s[i] = { 99999, -1, 99999, -1, 99999, -1, 0, 0, -1 };
}

static int loopCycles = 0;   /* the longest pass of the polling loop seen: the resolution */

/* One transaction on `port`: select, the bytes, deselect. */
static void transaction(int port, const uint8_t* tx, int n, ByteStat* stats) {
    write16(JOY_CTRL, 0x0040);                        /* reset */
    write16(JOY_MODE, 0x000D);                        /* 8 bits, reload factor 1 */
    write16(JOY_BAUD, 0x0088);                        /* 250 kHz */
    write16(JOY_CTRL, 0x0000);
    wait(20000);                                      /* deselected long enough for any device to reset */
    write16(JOY_CTRL, 0x0003 | (port << 13));         /* TX enable, /JOY low on this port */
    wait(1500);
    for (int b = 0; b < n; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);   /* RX empty first */
        write16(JOY_CTRL, 0x0013 | (port << 13));     /* acknowledge any old flag */
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        int rx = -1, low = -1, high = -1, passes = 0, dt = 0;
        for (;;) {
            const uint32_t st = read32(JOY_STAT);
            dt = (uint16_t)(t2() - t0);
            passes++;
            if (rx < 0 && (st & 2)) rx = dt;
            if (low < 0 && (st & 0x80)) low = dt;
            if (low >= 0 && high < 0 && !(st & 0x80)) high = dt;
            if (high >= 0 || dt > 4000) break;
        }
        if (passes > 1 && dt / passes > loopCycles) loopCycles = dt / passes;
        ByteStat& s = stats[b];
        if (read32(JOY_STAT) & 2) s.reply = read8(JOY_DATA);
        if (rx >= 0) { if (rx < s.rxMin) s.rxMin = rx; if (rx > s.rxMax) s.rxMax = rx; }
        if (low >= 0) {
            s.acked++;
            if (low < s.ackMin) s.ackMin = low;
            if (low > s.ackMax) s.ackMax = low;
            if (high >= 0) { const int p = high - low; if (p < s.pulseMin) s.pulseMin = p; if (p > s.pulseMax) s.pulseMax = p; }
        } else {
            s.silent++;
            break;                                    /* no /ACK: the device is done, or absent */
        }
    }
    write16(JOY_CTRL, 0x0000);
}

static void report(const char* what, int port, const uint8_t* tx, int n, const ByteStat* s) {
    printf("port %d, %s:\n", port + 1, what);
    for (int b = 0; b < n; b++) {
        if (!s[b].acked && !s[b].silent) break;       /* never reached */
        printf("  byte %2d  %02xh -> ", b + 1, tx[b]);
        if (s[b].reply >= 0) printf("%02xh", s[b].reply); else printf("-- ");
        if (s[b].rxMax >= 0) printf("  RX +%4d..%4d", s[b].rxMin, s[b].rxMax); else printf("  RX   none   ");
        if (s[b].acked) {
            printf("  /ACK +%4d..%4d (%2d of %2d)", s[b].ackMin, s[b].ackMax, s[b].acked, s[b].acked + s[b].silent);
            if (s[b].pulseMax >= 0) printf("  pulse %3d..%3d", s[b].pulseMin, s[b].pulseMax);
        } else printf("  no /ACK (%d of %d)", s[b].silent, s[b].silent);
        printf("\n");
    }
}

int main() {
    printf("\nsio/ack-per-device\n");
    printf("/ACK after each byte, per device and port: cycles from the byte's write (Timer 2 at the system clock).\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    static const uint8_t pad[5] = { 0x01, 0x42, 0x00, 0x00, 0x00 };
    static const uint8_t card[MAXB] = { 0x81, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static ByteStat ps[2][MAXB], cs[2][MAXB];
    const uint16_t prevT2 = initTimer(2, 0);          /* the system clock */
    EnterCriticalSection();
    for (int port = 0; port < 2; port++) {
        clearStats(ps[port]); clearStats(cs[port]);
        for (int r = 0; r < REPS; r++) {
            transaction(port, pad, 5, ps[port]);
            transaction(port, card, MAXB, cs[port]);
        }
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    for (int port = 0; port < 2; port++) {
        report("controller (01 42 00 00 00)", port, pad, 5, ps[port]);
        report("memory card (81 52 ..., frame 0)", port, card, MAXB, cs[port]);
    }
    printf("\nresolution: one pass of the polling loop, at most %d cycles\n", loopCycles);
    printf("\nsio/ack-per-device: Done.\n");
    for (;;) VSync(0);
}
