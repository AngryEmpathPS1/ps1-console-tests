/*
 * sio/multitap-modes - the Multitap's full ("transfer all") mode, byte by
 * byte: which half lags a transfer, what the first full read holds, what a
 * slot command other than 42h does, and every /ACK.
 *
 * WHY (ROAD B, the controller memo's 14.1: tables 2.4 and 2.5; M220). Four
 * sources describe full mode and disagree (scratch/controllers_memo.md 2.4):
 *   - where full transfer N's DATA comes from: N itself (mednafen, whose file
 *     says "Notes from tests on real thing"; MiSTer) or N-1 (ggrtk 2021:
 *     "controller response are buffered by an entire transfer sequence ...
 *     ignore the first Transfer All"; DuckStation; ReARMed);
 *   - the first full read after the request: valid, junk, or FFh-filled;
 *   - slot commands other than 42h in the last full transfer: nocash's
 *     "garbage" (FF 80 5A LSB, then the transfer ends), mednafen's abort after
 *     4 bytes, or DuckStation's FFh padding at full length;
 *   - full mode with no pad at the access slot: nocash "needs slot A", mednafen
 *     "returned data will be short: ff 80 5a";
 *   - /ACK through the tap: mednafen 1,000 cycles after the address byte, then
 *     64, 32 (no citation); this core M220's shape scaled to 338.
 *
 * WHAT IT DOES. A Multitap in port 1 with ONE digital pad, in slot B; slots
 * A, C and D empty. SIO0 driven by hand, interrupts off, 250 kHz, each byte
 * written and JOY_STAT polled against Timer 2 at the system clock: its reply
 * and when /ACK fell (a byte with no /ACK ends the transfer, as a driver
 * would). A full transfer is 35 bytes: 01 42 REQ, then 42 00 00 00 00 00 00 00
 * for each slot (libtap's own, M220). REQ (bit 0 of the third byte) asks for
 * full mode on the NEXT transfer.
 *   (a) 02 42 01 00 00 (slot B, requesting), then three full transfers -
 *       addressed 01h as libtap's are, so the ACCESS SLOT (A) IS EMPTY in
 *       every one: nocash "needs slot A", mednafen "short: ff 80 5a"
 *   (b) 01 42 01 00 00 (slot A - empty - requesting) and a full transfer
 *       after it: does an empty slot take the request at all? Then slot B
 *       requesting and a full transfer addressed 02h: does the address matter
 *       once in full mode?
 *   (c) a full transfer with 00h where the slot commands go, then two with
 *       42h: garbage, abort or padding after it
 *   (d) THE LAG, with a person: "press CROSS on the slot-B pad" eight times.
 *       Passthrough and full transfers (both addressed to slot B, the full
 *       one with REQ 0) alternate ~2 ms apart for up to 30 s, and each press
 *       is reported as the transfer where each kind first saw it. Full minus
 *       passthrough is -1 or +1 if full mode reads FRESH data (mednafen), +1
 *       or +3 if it replies with the last transfer's (DuckStation): a -1 says
 *       fresh, a +3 buffered, and +1 is either
 * Every row ends in a full transfer with REQ 0, which leaves full mode.
 *
 * READ IT LIKE THIS. Each transfer prints its bytes' replies and, under
 * them, each /ACK's delay from the byte's write (a byte takes ~1,090 cycles
 * at 250 kHz, so ~1,400 is ~310 after its end); '-' is no /ACK, and the
 * transfer ended there. Slot B's pad reads 41 5A FF FF with nothing pressed.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js multitap-modes --tap
 * --quiet=1500`; M220's model, mednafen's):
 *   (a) the first full read after the request is already valid - FF 80 5A |
 *       A FFh x8 | B 41 5A FF FF FF FF FF FF | C, D FFh x8 - with the access
 *       slot (A) empty; every /ACK 1,490 cycles after its byte's write (338
 *       after its end), none after byte 35.
 *   (b) a request through the empty slot A gets no /ACK at all, so full mode
 *       never starts; once in it, a transfer addressed 02h reads as 01h.
 *   (c) 00h in the slot commands runs 35 bytes; the full read after it ends
 *       after 4 - FF 80 5A FF (nocash's garbage, mednafen's abort) - and the
 *       next runs 35 again.
 *   (d) full - passthrough: 1, 1, -1, -1, 1, 1, 1, 1 - fresh data.
 * TEST THE TEST: the same core with its Multitap in DuckStation's model
 * (TAP_BUFFERED_REPLIES): every full transfer addressed to the empty slot A
 * ends at byte 1, the first addressed to slot B brings slot B back FFh-filled,
 * and (d) reads 1, 1, 1, 3, 3 (five presses).
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
#define MAXB 35

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) {} }

struct Xfer { int n; uint8_t rx[MAXB]; int16_t ack[MAXB]; };

/* One transaction on port 1: select, the bytes until one gets no /ACK, deselect. */
static void transfer(const uint8_t *tx, int n, Xfer &x) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003);
    wait(1500);
    x.n = 0;
    for (int b = 0; b < n; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        int low = -1, dt = 0;
        for (;;) {
            const uint32_t st = read32(JOY_STAT);
            dt = (uint16_t)(t2() - t0);
            if (low < 0 && (st & 0x80)) low = dt;
            if ((low >= 0 && (st & 2)) || dt > 4000) break;
        }
        while (!(read32(JOY_STAT) & 2) && (uint16_t)(t2() - t0) < 4000) {}
        x.rx[b] = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : 0;
        x.ack[b] = (int16_t)low;
        x.n = b + 1;
        if (low < 0) break;
    }
    write16(JOY_CTRL, 0x0000);
}

static uint8_t full[MAXB], fullZero[MAXB];
static void makeFull(uint8_t *t, uint8_t req, uint8_t cmd) {
    t[0] = 0x01; t[1] = 0x42; t[2] = req;
    for (int s = 0; s < 4; s++) { t[3 + s * 8] = cmd; for (int i = 1; i < 8; i++) t[3 + s * 8 + i] = 0; }
}

#define NX 16
static Xfer xs[NX]; static const char *xname[NX]; static int nx;
static void run(const char *name, const uint8_t *tx, int n) { if (nx < NX) { xname[nx] = name; transfer(tx, n, xs[nx]); nx++; } }

static void show(int i) {
    const Xfer &x = xs[i];
    printf("%s (%d bytes):\n  ", xname[i], x.n);
    for (int b = 0; b < x.n; b++) { printf("%02x ", x.rx[b]); if (b == 2 || b == 10 || b == 18 || b == 26) printf("| "); }
    printf("\n  /ACK:");
    for (int b = 0; b < x.n; b++) { if (x.ack[b] < 0) printf(" -"); else printf(" %d", x.ack[b]); if (b == 17) printf("\n       "); }
    printf("\n");
}

/* Is CROSS (bit 14 of the pad's button halfword, active low) pressed in a reply? */
static bool crossIn(const uint8_t *two) { return (two[1] & 0x40) == 0; }

int main() {
    printf("\nsio/multitap-modes\n");
    printf("A Multitap in port 1, one digital pad in slot B, driven by hand. No assertions.\n\n");
    static const uint8_t passB[5] = { 0x02, 0x42, 0x01, 0x00, 0x00 };
    static const uint8_t passA[5] = { 0x01, 0x42, 0x01, 0x00, 0x00 };
    static uint8_t fullOff[MAXB], fullB[MAXB], fullOffB[MAXB];
    makeFull(full, 0x01, 0x42);
    makeFull(fullOff, 0x00, 0x42);
    makeFull(fullZero, 0x01, 0x00);
    makeFull(fullB, 0x01, 0x42); fullB[0] = 0x02;
    makeFull(fullOffB, 0x00, 0x42); fullOffB[0] = 0x02;
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    run("(a) 02 42 01 00 00: slot B, requesting full mode", passB, 5);
    run("(a) full 1", full, MAXB);
    run("(a) full 2", full, MAXB);
    run("(a) full 3, REQ 0", fullOff, MAXB);
    run("(b) 01 42 01 00 00: slot A (empty), requesting", passA, 5);
    run("(b) a full transfer after it (full mode only if that request was taken)", full, MAXB);
    run("(b) 02 42 01 00 00: slot B, requesting", passB, 5);
    run("(b) full, ADDRESSED TO SLOT B (02 42 01 ...)", fullB, MAXB);
    run("(b) full, REQ 0", fullOff, MAXB);
    run("(c) 02 42 01 00 00", passB, 5);
    run("(c) full with 00h slot commands", fullZero, MAXB);
    run("(c) full after it", full, MAXB);
    run("(c) full again, REQ 0", fullOff, MAXB);
    ExitCriticalSection();
    for (int i = 0; i < nx; i++) show(i);

    /* (d): passthrough (requesting full) and full (REQ 0, back to passthrough) alternating. */
    printf("\n(d) press CROSS on the slot-B pad, eight times, a second or so apart\n");
    static int pAt[8], fAt[8];
    int presses = 0; bool pDown = false, fDown = false; int pFirst = -1, fFirst = -1;
    EnterCriticalSection();
    for (int k = 0; k < 30 * 250 && presses < 8; k++) {
        Xfer p, f;
        transfer(passB, 5, p);
        transfer(fullOffB, MAXB, f);                 /* addressed to slot B: works whether or not the access slot needs a pad */
        const bool pNow = p.n >= 5 && crossIn(&p.rx[3]);
        const bool fNow = f.n >= 15 && crossIn(&f.rx[13]);   /* slot B's buttons: bytes 13-14 */
        if (pNow && !pDown && pFirst < 0) pFirst = 2 * k;
        if (fNow && !fDown && fFirst < 0) fFirst = 2 * k + 1;
        pDown = pNow; fDown = fNow;
        if (pFirst >= 0 && fFirst >= 0 && !pNow && !fNow) {
            pAt[presses] = pFirst; fAt[presses] = fFirst; presses++;
            pFirst = fFirst = -1;
        }
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    for (int i = 0; i < presses; i++)
        printf("  press %d: passthrough saw it at transfer %d, full at %d (full - passthrough = %d)\n",
               i + 1, pAt[i], fAt[i], fAt[i] - pAt[i]);
    if (presses < 8) printf("  (%d presses seen)\n", presses);
    printf("\nsio/multitap-modes: Done.\n");
    for (;;) VSync(0);
}
