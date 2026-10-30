/*
 * sio/pocket-write - a write to the card in slot 2 (a PocketStation, 2026-10-24), timed byte by byte, then put back.
 *
 * WHY. pocket-cmds measured the read-only commands; a write (57h) is the other half of every save a game makes, and the
 * PocketStation's kernel answers it in software (its flash written by the ARM). Run AFTER pocket-dump has backed the whole
 * card up (back it up first, safe tests before risky ones).
 *
 * WHAT IT DOES. SIO0 by hand, interrupts off, 250 kHz, Timer 2 at the system clock, on frame 1023 (the last of block 15):
 *   1. read the frame and keep it;
 *   2. write a pattern (i * 37 + 11): every byte's reply and /ACK time, the end status (47h "G" good, 4Eh "N" bad checksum,
 *      FFh bad sector, FDh / FEh the PocketStation's own);
 *   3. read it back and compare;
 *   4. write a second pattern with a WRONG checksum: its status, and read back whether anything was written;
 *   5. write the original back with the right checksum and read it back: "restored" or not.
 *
 * THE RULE: run it here first: `node hwtests/run.js pocket-write --pocket2 --frames=3000`.
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
#define PORT 1
#define FRAME 1023

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }

static int ackAt[140], pulse[140], rxs[140], nb;

static void begin() {
    write16(JOY_CTRL, 0x0040); write16(JOY_MODE, 0x000D); write16(JOY_BAUD, 0x0088); write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003 | (PORT << 13));
    wait(1500);
    nb = 0;
}
static void end() { write16(JOY_CTRL, 0x0000); }

/* One byte: its reply (-2 none); /ACK time and pulse recorded; -1 when no /ACK within ~3 frames of the bus' worth. */
static int xfer(uint8_t v) {
    while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
    write16(JOY_CTRL, 0x0013 | (PORT << 13));
    const uint16_t t0 = t2();
    write8(JOY_DATA, v);
    int low = -1, high = -1, wraps = 0; uint16_t last = t0; uint32_t el = 0;
    for (;;) {
        const uint32_t s = read32(JOY_STAT);
        const uint16_t now = t2(); el += (uint16_t)(now - last); last = now;
        if (low < 0 && (s & 0x80)) low = el;
        if (low >= 0 && high < 0 && !(s & 0x80)) high = el;
        if (high >= 0 || el > 600000) break;
        (void)wraps;
    }
    while (!(read32(JOY_STAT) & 2) && (uint16_t)(t2() - t0) < 4000) { }
    const int rx = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : -2;
    if (nb < 140) { ackAt[nb] = low; pulse[nb] = (high >= 0 && low >= 0) ? high - low : -1; rxs[nb] = rx; nb++; }
    return low < 0 ? -1 : rx;
}

static uint8_t orig[128], buf[128];

static int readFrame(uint8_t* out) {
    begin();
    const uint8_t msb = FRAME >> 8, lsb = FRAME & 0xFF;
    int ok = xfer(0x81) >= 0 && xfer(0x52) >= 0 && xfer(0) == 0x5A && xfer(0) == 0x5D && xfer(msb) >= 0 && xfer(lsb) >= 0
          && xfer(0) == 0x5C && xfer(0) == 0x5D && xfer(0) == msb && xfer(0) == lsb;
    uint8_t chk = msb ^ lsb;
    for (int i = 0; ok && i < 128; i++) { const int v = xfer(0); if (v < 0) ok = 0; else { out[i] = v; chk ^= v; } }
    if (ok && xfer(0) != chk) ok = 0;
    if (ok) { while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA); write16(JOY_CTRL, 0x0013 | (PORT << 13)); write8(JOY_DATA, 0); wait(2000); if (read8(JOY_DATA) != 0x47) ok = 0; }
    end();
    return ok;
}

/* A write: returns the end status byte (-1 when the exchange broke off); badChk sends checksum ^ 0xFF. */
static int writeFrame(const uint8_t* in, int badChk) {
    begin();
    const uint8_t msb = FRAME >> 8, lsb = FRAME & 0xFF;
    int ok = xfer(0x81) >= 0 && xfer(0x57) >= 0 && xfer(0) == 0x5A && xfer(0) == 0x5D && xfer(msb) >= 0 && xfer(lsb) >= 0;
    uint8_t chk = msb ^ lsb;
    for (int i = 0; ok && i < 128; i++) { if (xfer(in[i]) < 0) ok = 0; chk ^= in[i]; }
    if (ok && xfer(badChk ? chk ^ 0xFF : chk) < 0) ok = 0;
    int a = ok ? xfer(0) : -1, b = ok && a >= 0 ? xfer(0) : -1;
    /* the end status comes with no /ACK after it: read it whatever the /ACK did */
    while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
    write16(JOY_CTRL, 0x0013 | (PORT << 13)); write8(JOY_DATA, 0);
    const uint16_t t0 = t2(); while (!(read32(JOY_STAT) & 2) && (uint16_t)(t2() - t0) < 4000) { }
    const int status = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : -2;
    if (nb < 140) { ackAt[nb] = -1; pulse[nb] = -1; rxs[nb] = status; nb++; }
    end();
    (void)a; (void)b;
    return ok ? status : -1;
}

static void dumpTiming(const char* what) {
    printf("%s: %d bytes\n", what, nb);
    for (int i = 0; i < nb; i++) {
        if (i >= 8 && i < nb - 6 && i != 70) { if (i == 8) printf("  ...\n"); continue; }
        printf("  byte %3d -> ", i + 1);
        if (rxs[i] >= 0) printf("%02xh", rxs[i]); else printf("-- ");
        if (ackAt[i] >= 0) printf("  /ACK +%6d  pulse %4d", ackAt[i], pulse[i]); else printf("  no /ACK");
        printf("\n");
    }
    int mn = 1 << 30, mx = -1;
    for (int i = 6; i < nb - 3; i++) if (ackAt[i] >= 0) { if (ackAt[i] < mn) mn = ackAt[i]; if (ackAt[i] > mx) mx = ackAt[i]; }
    if (mx >= 0) printf("  data bytes' /ACK +%d..%d\n", mn, mx);
}

int main() {
    printf("\nsio/pocket-write\n");
    printf("A write to frame %d of the card in slot 2, timed, read back, a bad-checksum write, then the original put back.\n\n", FRAME);
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    int r0 = readFrame(orig);
    printf("1. read: %s\n", r0 ? "ok" : "FAILED - nothing written");
    if (r0) {
        for (int i = 0; i < 128; i++) buf[i] = (uint8_t)(i * 37 + 11);
        const int s1 = writeFrame(buf, 0);
        dumpTiming("2. write pattern A");
        printf("   end status %02xh\n", s1 & 0xFF);
        static uint8_t back[128];
        const int r2 = readFrame(back);
        int same = r2; for (int i = 0; same && i < 128; i++) if (back[i] != buf[i]) same = 0;
        printf("3. read back: %s\n", !r2 ? "FAILED" : same ? "pattern A, as written" : "DIFFERENT from what was written");
        for (int i = 0; i < 128; i++) buf[i] = (uint8_t)(i * 91 + 5);
        const int s4 = writeFrame(buf, 1);
        dumpTiming("4. write pattern B with a wrong checksum");
        printf("   end status %02xh\n", s4 & 0xFF);
        const int r4 = readFrame(back);
        int isA = r4, isB = r4;
        for (int i = 0; i < 128; i++) { if (back[i] != (uint8_t)(i * 37 + 11)) isA = 0; if (back[i] != (uint8_t)(i * 91 + 5)) isB = 0; }
        printf("   read back after it: %s\n", !r4 ? "FAILED" : isA ? "still pattern A (not written)" : isB ? "pattern B (written anyway)" : "neither");
        const int s5 = writeFrame(orig, 0);
        const int r5 = readFrame(back);
        int restored = r5; for (int i = 0; restored && i < 128; i++) if (back[i] != orig[i]) restored = 0;
        printf("5. the original written back: status %02xh, %s\n", s5 & 0xFF, restored ? "restored" : "NOT RESTORED - restore from the pocket-dump backup");
        if (!restored) {
            printf("   its read %s; first bytes original / read:", r5 ? "ok" : "FAILED");
            for (int i = 0; i < 8; i++) printf(" %02x/%02x", orig[i], back[i]);
            printf("\n");
            const int r6 = readFrame(back);
            int again = r6; for (int i = 0; again && i < 128; i++) if (back[i] != orig[i]) again = 0;
            printf("   read once more: %s\n", !r6 ? "FAILED" : again ? "restored after all (the first read too soon)" : "still different");
        }
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    printf("\nsio/pocket-write: Done.\n");
    for (;;) VSync(0);
}
