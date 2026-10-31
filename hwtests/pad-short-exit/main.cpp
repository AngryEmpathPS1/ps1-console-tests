/*
 * sio/pad-short-exit - when a DualShock leaves config mode: on the command's byte, or at the packet's end.
 *
 * WHY (2026-10-31). psxsplash ends its analog set-up with a short packet, 01 43 00 00 00, five bytes where config mode
 * answers nine. This core leaves config mode at the last byte of the reply, so that packet never gets there: the pad
 * stays at ID F3h for the whole game, and psxsplash's PR #108 reads it as "other". DuckStation cannot say, as its
 * default controller is digital and never enters config mode. pad-config sends all nine bytes, so no console log
 * had this. Either a real pad obeys the short packet and this core is wrong, or every psxsplash game leaves a real
 * DualShock in config mode. The same question for the way in: 01 43 00 01 00 from digital mode.
 *
 * WHAT IT DOES. SIO0 by hand at 250 kHz, interrupts off, port 1. Each row starts from the same place: 43h 01 in full,
 * 44h 01 00 (analog) in full. Then a 43h 00 of N bytes, N = 9 down to 3, then a poll, printing the ID and status the
 * poll answers and how many bytes the exit acknowledged; 43h 00 in full afterwards, should it have stayed in. Then
 * psxsplash's own set-up, byte for byte. Then 44h 00 00 in full, so the pad goes back to digital, and a 43h 01 of N
 * bytes from digital, N = 9 down to 3, each followed by a poll and a full exit.
 * It needs a DUALSHOCK (SCPH-1200 or later) in port 1; nobody has to press anything. A digital pad answers 41h at
 * every row and says nothing.
 *
 * READ IT LIKE THIS. ID 73h after an exit: it left config mode, analog. F3h: it is still in. The smallest N that gives
 * 73h is the byte that counts.
 *
 * ON THIS CORE AT v0.377.226 (`node hwtests/run.js pad-short-exit`): only N = 9 leaves, psxsplash's set-up stays at F3h,
 * and N of 5 and up enters.
 * ON THE SCPH-5501 (2026-10-31, a white DualShock and a grey one): the same, row for row, acknowledges included. So a
 * DualShock acts on 43h at the packet's last byte, this core is right, and psxsplash leaves the pad in config mode.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* One packet on port 1: every byte's reply; the count is how many bytes were acknowledged. */
static int packet(const uint8_t* tx, int n, uint8_t* rx) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    idle(20000);
    write16(JOY_CTRL, 0x0003);                                  /* TX enable, /JOY1 low */
    idle(1500);
    int acked = 0;
    for (int b = 0; b < n; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);                              /* acknowledge any old flag */
        write8(JOY_DATA, tx[b]);
        const uint32_t t0 = now();
        bool ack = false;
        while (now() - t0 < 4000) { if (read32(JOY_STAT) & 0x80) ack = true; if (ack && (read32(JOY_STAT) & 2)) break; }
        while (!(read32(JOY_STAT) & 2) && now() - t0 < 6000) {}
        rx[b] = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : 0xEE;
        if (!ack) { for (int k = b + 1; k < n; k++) rx[k] = 0xEE; break; }
        acked++;
    }
    write16(JOY_CTRL, 0x0000);
    return acked;
}

static const uint8_t enter[9]   = { 0x01, 0x43, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t leave[9]   = { 0x01, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t analog[9]  = { 0x01, 0x44, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t digital[9] = { 0x01, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t poll[5]    = { 0x01, 0x42, 0x00, 0x00, 0x00 };
static const uint8_t lockSet[9] = { 0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t motors[9]  = { 0x01, 0x4D, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t rx[16];

static void pollRow(const char* name, int n, int acked) {
    uint8_t p[8];
    packet(poll, 5, p);
    printf("  %-12s %d bytes, %d acknowledged: poll ID %02x status %02x\n", name, n, acked, p[1], p[2]);
}

int main() {
    printf("\nsio/pad-short-exit\n");
    printf("When a DualShock leaves config mode. Port 1, nothing to press.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    uint8_t p[8];
    packet(poll, 5, p);
    printf("start: poll ID %02x status %02x\n", p[1], p[2]);

    printf("leaving, from analog in config mode:\n");
    for (int n = 9; n >= 3; n--) {
        packet(enter, 9, rx);
        packet(analog, 9, rx);
        const int acked = packet(leave, n, rx);
        pollRow("43h 00", n, acked);
        packet(leave, 9, rx);
    }

    printf("psxsplash's own set-up, as it sends it, from digital:\n");
    packet(enter, 9, rx);
    packet(digital, 9, rx);
    packet(leave, 9, rx);
    packet(enter, 5, rx);
    packet(lockSet, 9, rx);
    packet(motors, 9, rx);
    {
        const int acked = packet(leave, 5, rx);
        pollRow("its 43h 00", 5, acked);
    }
    for (int i = 0; i < 3; i++) { packet(poll, 5, p); idle(33869 * 16); }
    printf("  three polls later: ID %02x status %02x\n", p[1], p[2]);
    packet(leave, 9, rx);

    printf("entering, from digital:\n");
    packet(enter, 9, rx);
    packet(digital, 9, rx);
    packet(leave, 9, rx);
    for (int n = 9; n >= 3; n--) {
        const int acked = packet(enter, n, rx);
        pollRow("43h 01", n, acked);
        packet(leave, 9, rx);
    }

    packet(enter, 9, rx);
    packet(digital, 9, rx);
    packet(leave, 9, rx);
    packet(poll, 5, p);
    printf("end: poll ID %02x status %02x\n", p[1], p[2]);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nsio/pad-short-exit: Done.\n");
    for (;;) VSync(0);
}
