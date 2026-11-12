/*
 * sio/pad-ack-hist-analog - pad-ack-hist with a DualShock in ANALOG mode: the /ACK delay of every byte of the 42h poll's
 * nine (bytes 1-8 acknowledge), as distributions.
 *
 * WHY (docs/INSTRUMENT-AUDIT.md flag #2, 2026-11-12). SIO_ACK_DELAY 500 came from ack-per-device's 37-cycle loop with a digital
 * pad; hwtests/pad-ack-hist's 47 two-mode runs (digital pads, and an OEM Sony DualShock in digital mode on 2026-11-08 - the same
 * two modes per byte, STATUS "A DualShock /ACKs as the October pads did") refit it per byte (experiments/proposals/
 * sio-dma-refit.md). In analog mode (ID 73h) a DualShock answers nine bytes, the sticks in bytes 6-9, and nothing has timed any
 * /ACK past byte 4 on a console: the proposal gives bytes 5-8 byte 4's two modes, KNOWN-OPEN. This measures them.
 *
 * WHAT IT DOES. SIO0 by hand, interrupts off, 250 kHz - pad-ack-hist's set-up and its timing loop (one register until the bit,
 * Timer 2 once after). First the pad is put in analog mode BY COMMAND, as a game does (no hand on the ANALOG button): 43h
 * enter config, 44h analog on and locked (01h 03h), 43h exit config; the ID read back (73h expected; anything else is printed
 * and the run goes on). Then 256 polls of 01 42 00 00 00 00 00 00 00 timed for /ACK on bytes 1-8, and 256 for RX on bytes
 * 1-9, in 8-cycle bins from each byte's write. At the end the pad is put back as it was found: 43h, 44h digital and UNLOCKED
 * (00h 02h), 43h exit - so the ANALOG button works again and the LED is off.
 *
 * READ IT LIKE THIS. As pad-ack-hist: each "t:n" is n polls whose event came in [t, t+8). Bytes 1-4 should show the digital
 * mode's two modes (or say they do not); bytes 5-8 are new.
 *
 * NEEDS a DualShock (SCPH-1200 or SCPH-110) in port 1 - a part test: say which pad. A digital pad answers 41h and ignores the
 * config commands; the poll then has five bytes and the rows past byte 4 read "never".
 *
 * THE RULE: run it here first: `node hwtests/run.js pad-ack-hist-analog` (the virtual console's port 1 holds a digital pad unless
 * told otherwise; hwtests/run.js --port1=dualshock if it takes one).
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
#define NBINS 128
#define NB 9

static uint16_t hist[2][NB][NBINS + 2];
static uint32_t sum[2][NB], cnt[2][NB];
static int none[2][NB];

static inline uint16_t t2() { return read16(T2); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }

static int __attribute__((noinline)) until(uint32_t bit, uint16_t t0) {
    for (int i = 0; i < 2000; i++) if (read32(JOY_STAT) & bit) return (uint16_t)(t2() - t0);
    return -1;
}

static void begin() {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003);
    wait(1500);
}

/* One untimed exchange (a config command): each byte waits for its /ACK (bounded) and the reply is kept. Returns bytes ACKed. */
static int command(const uint8_t* tx, int n, uint8_t* rx) {
    begin();
    int b = 0;
    for (; b < n; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        if (until(0x02, t0) >= 0) rx[b] = read8(JOY_DATA); else rx[b] = 0xEE;
        if (b < n - 1 && until(0x80, t0) < 0) { b++; break; }
        for (int k = 0; k < 4000 && (read32(JOY_STAT) & 0x80); k++) { }
        wait(400);
    }
    write16(JOY_CTRL, 0x0000);
    wait(2000);
    return b;
}

static void poll(int kind) {
    static const uint8_t tx[NB] = { 0x01, 0x42, 0, 0, 0, 0, 0, 0, 0 };
    begin();
    for (int b = 0; b < NB; b++) {
        if (kind == 0 && b == NB - 1) break;          /* the last byte has no /ACK */
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        const int t = until(kind == 0 ? 0x80 : 0x02, t0);
        if (t < 0) { none[kind][b]++; break; }
        sum[kind][b] += t; cnt[kind][b]++;
        const int i = (t - LO) / BIN;
        hist[kind][b][t < LO ? NBINS : (i >= NBINS ? NBINS + 1 : i)]++;
        if (kind == 1) { if (b == NB - 1) break; if (until(0x80, t0) < 0) break; }
        for (int k = 0; k < 4000 && (read32(JOY_STAT) & 0x80); k++) { }
        wait(400);
    }
    write16(JOY_CTRL, 0x0000);
}

static void showReply(const char* what, const uint8_t* rx, int n, int acked) {
    printf("  %s:", what);
    for (int i = 0; i < n; i++) printf(" %02x", rx[i]);
    printf("  (%d bytes acknowledged)\n", acked);
}

int main() {
    printf("\nsio/pad-ack-hist-analog\n");
    printf("Port 1's DualShock put in analog mode by command, then 01 42 + 7 bytes: cycles from each byte's write to /ACK and to RX,\n");
    printf("%d polls each, %d-cycle bins. No assertions - say which pad was in port 1.\n\n", REPS, BIN);
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    static const uint8_t enter[5] = { 0x01, 0x43, 0x00, 0x01, 0x00 };
    static const uint8_t analog[9] = { 0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t digital[9] = { 0x01, 0x44, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t leave[9] = { 0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A };
    static const uint8_t pollTx[9] = { 0x01, 0x42, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t r0[9], r1[9], r2[9], r3[9], r4[9], r5[9], r6[9];
    const int a0 = command(enter, 5, r0);
    const int a1 = command(analog, 9, r1);
    const int a2 = command(leave, 9, r2);
    const int a3 = command(pollTx, 9, r3);
    for (int r = 0; r < REPS; r++) poll(0);
    for (int r = 0; r < REPS; r++) poll(1);
    const int a4 = command(enter, 9, r4);               /* in analog mode 43h answers nine bytes */
    const int a5 = command(digital, 9, r5);
    const int a6 = command(leave, 9, r6);
    uint8_t r7[9];
    const int a7 = command(pollTx, 5, r7);
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    printf("set-up (replies):\n");
    showReply("43h enter config", r0, 5, a0);
    showReply("44h analog, locked", r1, 9, a1);
    showReply("43h exit config", r2, 9, a2);
    showReply("42h poll (ID 73h = analog)", r3, 9, a3);
    printf("\n");
    static const char* names[2] = { "/ACK", "RX" };
    for (int k = 0; k < 2; k++) {
        for (int b = 0; b < NB; b++) {
            if (k == 0 && b == NB - 1) continue;
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
    printf("put back (replies):\n");
    showReply("43h enter config", r4, 9, a4);
    showReply("44h digital, unlocked", r5, 9, a5);
    showReply("43h exit config", r6, 9, a6);
    showReply("42h poll (ID 41h = digital)", r7, 5, a7);
    printf("\nsio/pad-ack-hist-analog: Done.\n");
    for (;;) VSync(0);
}
