/*
 * sio/multitap-full - a FULL Multitap: a pad in every slot and cards in its card slots, each exchange's every /ACK timed
 * (2026-10-27). Read-only: the cards are only READ (sector 0), never written.
 *
 * WHY. hwtests/multitap-modes on the SCPH-5000 (2026-10-27, one pad in slot B; STATUS "The Multitap on a console at
 * last"): every data byte as this core's model (M220), but the tap's own /ACKs ~1,271 cycles from a byte's write where
 * this core gives 1,635-1,775 (338 after the byte's end, SIO_ACK_DELAY, for every tap byte), a full read's address byte
 * 2,559 / 2,699, and slot A's first three bytes - with no pad in slot A - pad-like (1,495-1,777). With a pad in EVERY slot:
 * is each slot's first bytes pad-like (the tap asking that pad live), or only slot A's? And a card behind the tap: src/psx.js
 * has no number for it ("Unmeasured ... for a card behind a Multitap (the tap's own delay)" - each card byte 170 after its
 * end, a card's own, DuckStation's).
 *
 * WHAT IT DOES. Port 1, SIO0 driven by hand at 250 kHz, interrupts off, Timer 2 at the system clock: each byte written,
 * JOY_STAT polled for /ACK and the reply, a byte with no /ACK ending the exchange (as multitap-modes):
 *   (a) passthrough to each slot's pad: 0N 42 00 x7 (N 1-4), to its last /ACK - its ID, its bytes, its /ACKs
 *   (b) slot A requesting full mode (01 42 01 00 00), then three full reads (35 bytes; the third with REQ 0)
 *   (c) each card slot's card read through the tap: 8N 52 00 00 00 00 then 134 zeros (sector 0; psx-spx "Multitap Memory
 *       Card Access", "Memory Card Read Data Command") - 140 bytes to the end byte (47h, no /ACK), N 1-4
 *   (d) the same addressed 80h (a port's own card address, with the tap in)
 * Then (b) and (c) once more, for the spread within a run.
 *
 * SET-UP. The Multitap in port 1 (it takes the port's card slot too). A pad in every slot A-D: digital ones and analog
 * ones, one analog pad in ANALOG mode (its LED red) - the slot is printed with its ID (41h digital, 73h analog). Memory
 * cards in the tap's card slots - two Sony cards and the generic one in A, B and C, D EMPTY (an empty slot's
 * answer is part of the measurement). Nothing to press.
 *
 * READ IT LIKE THIS. Each exchange: its reply bytes and, under them, each /ACK's delay from the byte's write ('-' none,
 * the exchange ended there). A card read prints its first ten replies (flag, 5A 5D, its echo, 5C 5D, the address), the
 * sector's first four bytes ("MC" on a formatted card), the checksum (and whether it matches), the end byte (47h = good),
 * and its /ACKs: the first twelve, then the data bytes' least / median / most, then the checksum's.
 *
 * THE RULE: run it here first: `node hwtests/run.js multitap-full --tap4`.
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
#define MAXB 140

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) {} }

struct Xfer { int n; uint8_t rx[MAXB]; int16_t ack[MAXB]; };

/* One transaction on port 1: select, the bytes until one gets no /ACK, deselect (multitap-modes' own). */
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

static void showPad(const char *name, const Xfer &x) {
    printf("%s (%d bytes):\n  ", name, x.n);
    for (int b = 0; b < x.n; b++) printf("%02x ", x.rx[b]);
    printf("\n  /ACK:");
    for (int b = 0; b < x.n; b++) { if (x.ack[b] < 0) printf(" -"); else printf(" %d", x.ack[b]); }
    printf("\n");
}

static void showFull(const char *name, const Xfer &x) {
    printf("%s (%d bytes):\n  ", name, x.n);
    for (int b = 0; b < x.n; b++) { printf("%02x ", x.rx[b]); if (b == 2 || b == 10 || b == 18 || b == 26) printf("| "); }
    printf("\n  /ACK:");
    for (int b = 0; b < x.n; b++) { if (x.ack[b] < 0) printf(" -"); else printf(" %d", x.ack[b]); if (b == 17) printf("\n       "); }
    printf("\n");
}

static void isort(int16_t *v, int n) { for (int i = 1; i < n; i++) { const int16_t t = v[i]; int j = i - 1; while (j >= 0 && v[j] > t) { v[j + 1] = v[j]; j--; } v[j + 1] = t; } }

static void showCard(const char *name, const Xfer &x) {
    printf("%s (%d bytes):", name, x.n);
    if (x.n < 140) {
        printf("\n  ");
        for (int b = 0; b < x.n; b++) printf("%02x ", x.rx[b]);
        printf("\n  /ACK:");
        for (int b = 0; b < x.n; b++) { if (x.ack[b] < 0) printf(" -"); else printf(" %d", x.ack[b]); }
        printf("\n");
        return;
    }
    uint8_t chk = x.rx[8] ^ x.rx[9];
    for (int i = 10; i < 138; i++) chk ^= x.rx[i];
    printf(" ");
    for (int b = 0; b < 10; b++) printf("%02x ", x.rx[b]);
    printf("| data %02x %02x %02x %02x | checksum %02x (%s) | end %02x\n", x.rx[10], x.rx[11], x.rx[12], x.rx[13], x.rx[138],
           chk == x.rx[138] ? "matches" : "DIFFERS", x.rx[139]);
    printf("  /ACK first 12:");
    for (int b = 0; b < 12; b++) { if (x.ack[b] < 0) printf(" -"); else printf(" %d", x.ack[b]); }
    static int16_t d[128];
    int nd = 0;
    for (int b = 10; b < 138; b++) if (x.ack[b] >= 0) d[nd++] = x.ack[b];
    isort(d, nd);
    if (nd) printf("; data bytes %d..%d, median %d (%d)", d[0], d[nd - 1], d[nd / 2], nd);
    printf("; checksum %d; end %d\n", x.ack[138], x.ack[139]);
}

static uint8_t full[35], fullOff[35], cardRead[140];
static void makeFull(uint8_t *t, uint8_t req) {
    t[0] = 0x01; t[1] = 0x42; t[2] = req;
    for (int s = 0; s < 4; s++) { t[3 + s * 8] = 0x42; for (int i = 1; i < 8; i++) t[3 + s * 8 + i] = 0; }
}

static Xfer xs[16];

int main() {
    printf("\nsio/multitap-full\n");
    printf("A Multitap in port 1, a pad in every slot, cards in its card slots (D empty), driven by hand. Read-only. No assertions.\n\n");
    makeFull(full, 0x01);
    makeFull(fullOff, 0x00);
    for (int i = 0; i < 140; i++) cardRead[i] = 0;
    cardRead[1] = 0x52;
    const uint16_t prevT2 = initTimer(2, 0);
    for (int round = 0; round < 2; round++) {
        EnterCriticalSection();
        int k = 0;
        if (round == 0)
            for (int s = 1; s <= 4; s++) { static uint8_t p[9]; p[0] = (uint8_t)s; p[1] = 0x42; for (int i = 2; i < 9; i++) p[i] = 0; transfer(p, 9, xs[k++]); }
        { static const uint8_t reqA[5] = { 0x01, 0x42, 0x01, 0x00, 0x00 }; transfer(reqA, 5, xs[k++]); }
        transfer(full, 35, xs[k++]);
        transfer(full, 35, xs[k++]);
        transfer(fullOff, 35, xs[k++]);
        for (int s = 1; s <= 4; s++) { cardRead[0] = (uint8_t)(0x80 + s); transfer(cardRead, 140, xs[k++]); }
        if (round == 0) { cardRead[0] = 0x80; transfer(cardRead, 140, xs[k++]); }
        ExitCriticalSection();
        k = 0;
        printf(round == 0 ? "== round 1\n" : "\n== round 2\n");
        if (round == 0) {
            static const char *pn[4] = { "(a) slot A's pad, passthrough 01 42 00 ..", "(a) slot B's pad, passthrough 02 42 00 ..",
                                         "(a) slot C's pad, passthrough 03 42 00 ..", "(a) slot D's pad, passthrough 04 42 00 .." };
            for (int s = 0; s < 4; s++) showPad(pn[s], xs[k++]);
        }
        showPad("(b) 01 42 01 00 00: slot A, requesting full mode", xs[k++]);
        showFull("(b) full 1", xs[k++]);
        showFull("(b) full 2", xs[k++]);
        showFull("(b) full 3, REQ 0", xs[k++]);
        static const char *cn[4] = { "(c) card slot A (81h), sector 0", "(c) card slot B (82h), sector 0",
                                     "(c) card slot C (83h), sector 0", "(c) card slot D (84h), sector 0" };
        for (int s = 0; s < 4; s++) showCard(cn[s], xs[k++]);
        if (round == 0) showCard("(d) addressed 80h, sector 0", xs[k++]);
    }
    restoreTimer(2, prevT2);
    printf("\nsio/multitap-full: Done.\n");
    for (;;) VSync(0);
}
