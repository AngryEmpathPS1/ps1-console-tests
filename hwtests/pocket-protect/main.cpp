/*
 * sio/pocket-protect - the PocketStation's write-protected region: 5Dh sets ComFlags.10, and a write to frames 16..55 is
 * then refused (a PocketStation in slot 2, 2026-10-24).
 *
 * WHY. psx-spx: "The write-protection is enabled by ComFlags.bit10 (which can be set/cleared via BU Command 5Dh)" -
 * "If value.8-15 = 00h, then ComFlags.bit10=1, else ComFlags.bit10=0" - and it gives the refusal as FEh, the running
 * file's directory as FDh. The kernel (Kernel.bin's 57h handler at 0860h, STATUS "psx-spx has the PocketStation's two
 * write-reject codes swapped") gives FDh here: 06F4h tests ComFlags bit 10 and frame 10h..37h. pocket-pa3 met the other
 * code (FEh, the running file's directory) on this unit; this is the other half.
 *
 * WHAT IT DOES. SIO0 by hand, interrupts off, 250 kHz, as pocket-pa3. Frames 15, 16, 55 and 56 read and kept. 5Dh with
 * value 00 00 00 (protect on, duration 0); each of the four written back with the bytes it already holds - nothing on the
 * card changes, whatever the answer - its status printed, and read again: still what it was. 5Dh with value 00 01 00
 * (bit 8 "download finished", protect off); 16 and 55 written back again. The 5Dh replies printed (FLAG, 03h, 00 00 00).
 *
 * THE RULE: run it here first: `node hwtests/run.js pocket-protect --pocket2 --frames=3000 --quiet=2000`.
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

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }
static void waitMs(int ms) { for (int i = 0; i < ms; i++) wait(33869); }

static void begin() {
    write16(JOY_CTRL, 0x0040); write16(JOY_MODE, 0x000D); write16(JOY_BAUD, 0x0088); write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003 | (PORT << 13));
    wait(1500);
}
static void end() { write16(JOY_CTRL, 0x0000); }

/* One byte: its reply, or -1 when no /ACK came (needAck), -2 for no reply at all. */
static int xfer(uint8_t v, int needAck) {
    while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
    write16(JOY_CTRL, 0x0013 | (PORT << 13));
    write8(JOY_DATA, v);
    const uint16_t t0 = t2(); int acked = 0;
    for (;;) {
        const uint32_t s = read32(JOY_STAT);
        if (s & 0x80) acked = 1;
        if ((s & 2) && (acked || !needAck)) break;
        if ((uint16_t)(t2() - t0) > 30000) break;
    }
    if (needAck && !acked) { const uint16_t t1 = t2(); while ((uint16_t)(t2() - t1) < 6000) if (read32(JOY_STAT) & 0x80) { acked = 1; break; } }
    const int rx = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : -2;
    return (needAck && !acked) ? -1 : rx;
}

static uint8_t keep[4][128], got[128];
static const int FR[4] = { 15, 16, 55, 56 };

static int readFrame(int f) {
    begin();
    const uint8_t msb = f >> 8, lsb = f & 0xFF;
    int ok = xfer(0x81, 1) >= 0 && xfer(0x52, 1) >= 0 && xfer(0, 1) == 0x5A && xfer(0, 1) == 0x5D && xfer(msb, 1) >= 0 && xfer(lsb, 1) >= 0
          && xfer(0, 1) == 0x5C && xfer(0, 1) == 0x5D && xfer(0, 1) == msb && xfer(0, 1) == lsb;
    uint8_t chk = msb ^ lsb;
    for (int i = 0; ok && i < 128; i++) { const int v = xfer(0, 1); if (v < 0) ok = 0; else { got[i] = v; chk ^= v; } }
    if (ok && xfer(0, 1) != chk) ok = 0;
    if (ok && xfer(0, 0) != 0x47) ok = 0;
    end();
    return ok;
}
/* A read, again once if the first fails (the flash still busy after a write: pocket-write, on this unit). */
static int readBack(int f) { if (readFrame(f)) return 1; waitMs(30); return readFrame(f); }

static int writeFrame(int f, const uint8_t* data) {
    begin();
    const uint8_t msb = f >> 8, lsb = f & 0xFF;
    int ok = xfer(0x81, 1) >= 0 && xfer(0x57, 1) >= 0 && xfer(0, 1) == 0x5A && xfer(0, 1) == 0x5D && xfer(msb, 1) >= 0 && xfer(lsb, 1) >= 0;
    uint8_t chk = msb ^ lsb;
    for (int i = 0; ok && i < 128; i++) { if (xfer(data[i], 1) < 0) ok = 0; chk ^= data[i]; }
    if (ok && xfer(chk, 1) < 0) ok = 0;
    if (ok && (xfer(0, 1) < 0 || xfer(0, 1) < 0)) ok = 0;
    const int status = ok ? xfer(0, 0) : -1;
    end();
    return status;
}

static int rep5d[6];
static void cmd5d(uint8_t hi, uint8_t mid, uint8_t lo) {
    begin();
    const uint8_t b[6] = { 0x81, 0x5D, 0, hi, mid, lo };
    for (int i = 0; i < 6; i++) rep5d[i] = xfer(b[i], i < 5);
    end();
}

int main() {
    printf("\nsio/pocket-protect\n");
    printf("ComFlags.10 (5Dh) and a write to frames 16..55: each frame written back with its own bytes.\n\n");
    const uint16_t prevT2 = initTimer(2, 0);
    int ok[4], st[4], same[4];
    EnterCriticalSection();
    for (int k = 0; k < 4; k++) { ok[k] = readBack(FR[k]); for (int i = 0; i < 128; i++) keep[k][i] = got[i]; }
    ExitCriticalSection();
    for (int k = 0; k < 4; k++) printf("  frame %d read %s, first bytes %02x %02x %02x %02x\n", FR[k], ok[k] ? "ok" : "FAILED", keep[k][0], keep[k][1], keep[k][2], keep[k][3]);
    for (int k = 0; k < 4; k++) if (!ok[k]) { printf("\nA frame could not be read: nothing written.\nsio/pocket-protect: Done.\n"); for (;;) VSync(0); }

    EnterCriticalSection();
    cmd5d(0, 0, 0);
    waitMs(20);
    for (int k = 0; k < 4; k++) {
        st[k] = writeFrame(FR[k], keep[k]); waitMs(30);
        same[k] = readBack(FR[k]); for (int i = 0; same[k] && i < 128; i++) if (got[i] != keep[k][i]) same[k] = 0;
    }
    ExitCriticalSection();
    printf("\nprotect ON (5Dh 00 00 00), replies %02x %02x %02x %02x %02x %02x\n", rep5d[0] & 0xFF, rep5d[1] & 0xFF, rep5d[2] & 0xFF, rep5d[3] & 0xFF, rep5d[4] & 0xFF, rep5d[5] & 0xFF);
    for (int k = 0; k < 4; k++) printf("  frame %d: write status %02x, after it %s\n", FR[k], st[k] & 0xFF, same[k] ? "unchanged" : "CHANGED OR UNREAD");

    EnterCriticalSection();
    cmd5d(0, 1, 0);
    waitMs(20);
    for (int k = 1; k < 3; k++) {
        st[k] = writeFrame(FR[k], keep[k]); waitMs(30);
        same[k] = readBack(FR[k]); for (int i = 0; same[k] && i < 128; i++) if (got[i] != keep[k][i]) same[k] = 0;
    }
    ExitCriticalSection();
    printf("\nprotect OFF (5Dh 00 01 00), replies %02x %02x %02x %02x %02x %02x\n", rep5d[0] & 0xFF, rep5d[1] & 0xFF, rep5d[2] & 0xFF, rep5d[3] & 0xFF, rep5d[4] & 0xFF, rep5d[5] & 0xFF);
    for (int k = 1; k < 3; k++) printf("  frame %d: write status %02x, after it %s\n", FR[k], st[k] & 0xFF, same[k] ? "unchanged" : "CHANGED OR UNREAD");

    restoreTimer(2, prevT2);
    printf("\nsio/pocket-protect: Done.\n");
    for (;;) VSync(0);
}
