/*
 * sio/pocket-cmds - a PocketStation's card-port commands, byte by byte, with each byte's /ACK timing (2026-10-24)
 *
 * WHY. This emulator's PocketStation (src/pocketstation.js: its ARM, Sony's kernel, the card protocol the kernel answers)
 * was built from the kernel dump and psx-spx ("Pocketstation BU Command Summary") and never met a real one. A
 * PocketStation arrived 2026-10-24. This asks it the READ-ONLY commands - nothing is written, nothing executed - and
 * records every reply byte and when its /ACK came, against the card in the other slot (the control: an ordinary card).
 *
 * WHAT IT DOES. SIO0 by hand, interrupts off, 250 kHz, Timer 2 at the system clock (as sio/ack-per-device), on BOTH
 * slots, four times each (the replies of the first, the /ACK times' range over all four):
 *   53h  Get ID                   81 53 + 8 zeros
 *   58h  Get ID / version         81 58 + 4 zeros          (FLAG, 02h, 01h, 01h on a PocketStation)
 *   5Ah  dir_index, ComFlags, F_SN, date, time   81 5A + 19 zeros   (three times, ~1 s apart: its clock)
 *   5Bh  FUNC 00h (get date/time) 81 5B 00 + 23 zeros, stopped at the first byte with no /ACK
 *   52h  Read Sector 0            81 52 00 00 00 00 + 134 zeros: the whole frame (the directory's header), with the
 *        delay psx-spx notes between the 5Ch and 5Dh bytes
 * A byte with no /ACK ends the command there (an ordinary card does not know 58h-5Bh; an empty slot answers nothing).
 *
 * READ IT LIKE THIS. Per command and slot: "byte N  tx -> rx  /ACK +a..b  pulse p..q" - cycles from the byte's write to
 * /ACK going low (the byte itself takes ~1,090 at 250 kHz), and how long it stayed low. Then the 5Ah answers decoded.
 *
 * THE RULE: run it here first: `node hwtests/run.js pocket-cmds --pocket2 --frames=3000` (a formatted card in slot 1).
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
#define REPS 4
#define MAXB 140

struct ByteStat { int ackMin, ackMax, pulseMin, pulseMax, acked; int reply; };
static ByteStat st[MAXB];
static int got;                                       /* bytes acknowledged in the first repetition */

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }
static void waitLong(int ms) { for (int i = 0; i < ms; i++) wait(33869); }

static void clear() { for (int i = 0; i < MAXB; i++) st[i] = { 99999, -1, 99999, -1, 0, -1 }; got = 0; }

/* One transaction on `port`: select, up to n bytes (stopping at the first with no /ACK), deselect. */
static int transaction(int port, const uint8_t* tx, int n, int rep) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003 | (port << 13));
    wait(1500);
    int b;
    for (b = 0; b < n; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013 | (port << 13));
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        int low = -1, high = -1, dt = 0;
        for (;;) {
            const uint32_t s = read32(JOY_STAT);
            dt = (uint16_t)(t2() - t0);
            if (low < 0 && (s & 0x80)) low = dt;
            if (low >= 0 && high < 0 && !(s & 0x80)) high = dt;
            if (high >= 0 || dt > 30000) break;
        }
        while (!(read32(JOY_STAT) & 2) && (uint16_t)(t2() - t0) < 4000) { }
        const int rx = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : -1;
        ByteStat& s = st[b];
        if (rep == 0) s.reply = rx;
        if (low < 0) break;
        s.acked++;
        if (low < s.ackMin) s.ackMin = low;
        if (low > s.ackMax) s.ackMax = low;
        if (high >= 0) { const int p = high - low; if (p < s.pulseMin) s.pulseMin = p; if (p > s.pulseMax) s.pulseMax = p; }
    }
    write16(JOY_CTRL, 0x0000);
    if (rep == 0) got = b;
    return b;
}

static void report(const char* what, int port, const uint8_t* tx, int n) {
    printf("slot %d, %s: %d byte(s) acknowledged\n", port + 1, what, got);
    for (int b = 0; b < n && b <= got; b++) {
        printf("  byte %3d  %02xh -> ", b + 1, tx[b]);
        if (st[b].reply >= 0) printf("%02xh", st[b].reply); else printf("-- ");
        if (st[b].acked) {
            printf("  /ACK +%5d..%5d (%d of %d)", st[b].ackMin, st[b].ackMax, st[b].acked, REPS);
            if (st[b].pulseMax >= 0) printf("  pulse %3d..%3d", st[b].pulseMin, st[b].pulseMax);
        } else printf("  no /ACK");
        printf("\n");
    }
}

static void run(const char* what, int port, const uint8_t* tx, int n) {
    clear();
    for (int r = 0; r < REPS; r++) transaction(port, tx, n, r);
    report(what, port, tx, n);
}

int main() {
    printf("\nsio/pocket-cmds\n");
    printf("PocketStation card-port commands (read-only) on both slots: reply bytes and /ACK cycles from each byte's write.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    static uint8_t c53[10] = { 0x81, 0x53 }, c58[6] = { 0x81, 0x58 }, c5a[21] = { 0x81, 0x5A }, c5b[26] = { 0x81, 0x5B, 0x00 };
    static uint8_t c52[MAXB] = { 0x81, 0x52 };
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    for (int port = 0; port < 2; port++) {
        run("53h Get ID", port, c53, 10);
        run("58h Get ID/version", port, c58, 6);
        for (int k = 0; k < 3; k++) {
            clear(); transaction(port, c5a, 21, 0);
            printf("slot %d, 5Ah (%d of 3): %d byte(s):", port + 1, k + 1, got);
            for (int b = 0; b < 21 && b < got + 1; b++) printf(" %02x", st[b].reply & 0xFF);
            printf("\n");
            if (got >= 21 - 1) printf("  dir_index %02x%02x  ComFlags b0 %d b1 %d b3 %d b2 %d  F_SN %02x%02x%02x%02x  date %02x/%02x/%02x%02x  time %02x:%02x:%02x dow %02x\n",
                st[3].reply, st[4].reply, st[5].reply, st[6].reply, st[7].reply, st[8].reply, st[12].reply, st[11].reply, st[10].reply, st[9].reply,
                st[13].reply, st[14].reply, st[16].reply, st[15].reply, st[19].reply, st[18].reply, st[17].reply, st[20].reply);
            waitLong(1000);
        }
        run("5Ah dir/flags/date/time (timing)", port, c5a, 21);
        run("5Bh FUNC 00h get date/time", port, c5b, 26);
        run("52h Read Sector 0", port, c52, MAXB);
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    printf("\nsio/pocket-cmds: Done.\n");
    for (;;) VSync(0);
}
