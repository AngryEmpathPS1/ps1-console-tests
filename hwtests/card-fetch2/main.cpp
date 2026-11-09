/*
 * sio/card-fetch2 - card-fetch with the /ACK latch: a memory card's whole read, byte by byte: how long it takes to fetch the sector after its address, and every
 * byte's /ACK through the 128 data bytes to the end status. READ ONLY: the only command sent is 52h ('R'); nothing here can
 * write a card.
 *
 * WHY. STATUS "The SCPH-5000's first day" (2026-10-26, hwtests/ack-per-device): an official Sony card gave NO /ACK within ~4,000
 * cycles at byte 7 - the first byte after the sector address - where the PSxMemCard (an SD card emulating one) acknowledges at
 * once: a real card fetching its sector. ack-per-device stops waiting at 4,000 cycles and after twelve bytes, so how long the
 * fetch takes, and what the 128 data bytes and the end look like on a real card, were never measured. One card, one run, so
 * nothing was modelled (rule 3): this is the test the note owed, for every Sony card there is (the SCPH-5501's OEM card next).
 *
 * WHAT IT DOES. SIO0 driven by hand, interrupts off, 250 kHz (JOY_BAUD 88h), as ack-per-device. For each port with a card, a full
 * read of frames 0, 1, 2, 63, 64, 512 and 1023, eight times each: 81h 52h 00h 00h MSB LSB, then 00h for the card's 5Ch 5Dh MSB LSB,
 * the 128 data bytes, the checksum and the end byte (psx-spx "Memory Card Read/Write Commands"). Each byte: written to JOY_DATA, then
 * JOY_STAT polled against Timer 2 at the system clock, the 16-bit count accumulated pass by pass so a wait can run to 300,000 cycles
 * without wrapping: when RX has the byte, when /ACK goes low, how long it stays low.
 *
 * WHAT IT PRINTS (no save data - only the protocol bytes, and whether the data's checksum matched):
 *   per port and frame: bytes 1-10 and the last two one by one (the reply, /ACK min..max, the pulse), the 128 data bytes as
 *   one row (/ACK min..max and mean, pulse min..max), the end byte's value (47h good, 4Eh bad sector, FFh bad frame number) and
 *   "checksum ok N of 8".
 * READ IT LIKE THIS. "/ACK" is cycles from the byte's write; the byte itself takes ~1,090 at 250 kHz. Byte 7's /ACK is the sector
 * fetch. The resolution is one pass of the polling loop, printed at the end.
 *
 * WHY THE 2 (2026-11-08 night). card-fetch on the second SCPH-5501 with two Japanese SCPH-1020s: card B (port 2) read whole 168 of
 * 168 times (three runs); card A (port 1) went silent at a random byte of the data in almost every read (35-197 acks of 1,024 over a frame's
 * eight reads, then no /ACK in 300,000 cycles) - and its pulse is 54-68 cycles where one pass of card-fetch's loop takes up to 56:
 * a pulse can fall between two reads of the /ACK LEVEL (JOY_STAT bit 7). So here JOY_CTRL bit 12 (ACK Interrupt Enable, as the
 * kernel's own 1003h) is on, and JOY_STAT bit 9 - "Interrupt Request ... (sticky)" (psx-spx), set when /ACK goes low and held
 * until JOY_CTRL bit 4 acknowledges it - counts an /ACK the level never showed. Such a byte's /ACK time is the first pass that
 * saw the latch (the same resolution) and its pulse is unknown: counted apart, "latch only". JOY_STAT bit 9 and I_STAT bit 7 are
 * acknowledged before interrupts come back (interrupts are off throughout: no IRQ7 is taken).
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
#define REPS 8
#define I_STAT 0x1F801070
#define NBYTES 140
#define LIMIT 300000
#define NFRAMES 7

static const int frames[NFRAMES] = { 0, 1, 2, 63, 64, 512, 1023 };

struct ByteStat { int ackMin, ackMax, pulseMin, pulseMax, acked, silent, reply; long long ackSum; int latchOnly; };

static inline uint16_t t2() { return read16(0x1F801120); }

static void wait(int cycles) {
    const uint16_t t0 = t2();
    while ((uint16_t)(t2() - t0) < cycles) { }
}

static int loopCycles = 0;

/* One byte: returns the reply (or -1) and its /ACK delay and pulse (-1 when none within LIMIT, or when only the latch saw the
 * /ACK: *viaLatch 1). */
static int oneByte(uint8_t tx, int* ackAt, int* pulse, int* viaLatch) {
    while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
    write16(JOY_CTRL, 0x1013 | (read16(JOY_CTRL) & 0x2000));   /* acknowledge any old flag (bits 3, 9), the latch on, keep the port */
    uint16_t last = t2();
    write8(JOY_DATA, tx);
    int elapsed = 0, low = -1, high = -1, passes = 0, reply = -1, latch = 0;
    for (;;) {
        const uint32_t st = read32(JOY_STAT);
        const uint16_t now = t2();
        elapsed += (uint16_t)(now - last); last = now;
        passes++;
        if (reply < 0 && (st & 2)) reply = read8(JOY_DATA);
        if (low < 0 && (st & 0x280)) { low = elapsed; latch = !(st & 0x80); }   /* the level, or the latch alone (bit 9) */
        if (low >= 0 && high < 0 && !(st & 0x80)) high = elapsed;
        if (high >= 0 || elapsed > LIMIT) break;
        if (low < 0 && reply >= 0 && elapsed > 4000 && tx == 0x47) break;   /* never: the end byte is not sent by us */
    }
    if (passes > 1 && elapsed / passes > loopCycles && elapsed < 20000) loopCycles = elapsed / passes;
    if (reply < 0 && (read32(JOY_STAT) & 2)) reply = read8(JOY_DATA);
    *ackAt = low; *pulse = (low >= 0 && high >= 0 && !latch) ? high - low : -1; *viaLatch = latch;
    return reply;
}

/* One read of a frame on `port`. Returns 1 if the checksum matched. */
static int readFrame(int port, int frame, ByteStat* s, int* endByte) {
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x1003 | (port << 13));
    wait(1500);
    const uint8_t msb = (uint8_t)(frame >> 8), lsb = (uint8_t)frame;
    uint8_t x = msb ^ lsb, sum = 0;
    int ok = 0;
    for (int b = 0; b < NBYTES; b++) {
        const uint8_t tx = b == 0 ? 0x81 : b == 1 ? 0x52 : b == 4 ? msb : b == 5 ? lsb : 0x00;   /* 52h 'R' only: READ ONLY */
        int ack, pulse, viaLatch;
        const int r = oneByte(tx, &ack, &pulse, &viaLatch);
        ByteStat& t = s[b];
        if (b < 10 || b >= NBYTES - 2) t.reply = r;
        if (b >= 10 && b < 138 && r >= 0) x ^= (uint8_t)r;
        if (b == 138) sum = (uint8_t)r;
        if (b == NBYTES - 1) *endByte = r;
        if (ack >= 0) {
            t.acked++; t.ackSum += ack; t.latchOnly += viaLatch;
            if (ack < t.ackMin) t.ackMin = ack;
            if (ack > t.ackMax) t.ackMax = ack;
            if (pulse >= 0) { if (pulse < t.pulseMin) t.pulseMin = pulse; if (pulse > t.pulseMax) t.pulseMax = pulse; }
        } else {
            t.silent++;
            if (b < NBYTES - 1) break;                       /* the card stopped: no use sending more */
        }
    }
    if (x == sum) ok = 1;
    write16(JOY_CTRL, 0x0010);                               /* acknowledge JOY_STAT bit 9, deselect */
    write16(JOY_CTRL, 0x0000);
    write32(I_STAT, ~0x80u);                                 /* and I_STAT bit 7 (IRQ7): nothing pending when interrupts return */
    return ok;
}

static void report(int port, int frame, const ByteStat* s, int okCount, const int* ends) {
    printf("port %d, frame %d: checksum ok %d of %d; end bytes", port + 1, frame, okCount, REPS);
    for (int r = 0; r < REPS; r++) { if (ends[r] >= 0) printf(" %02x", ends[r]); else printf(" --"); }
    printf("\n");
    for (int b = 0; b < NBYTES; b++) {
        if (b >= 10 && b < 138) continue;
        if (!s[b].acked && !s[b].silent) break;
        printf("  byte %3d -> ", b + 1);
        if (s[b].reply >= 0) printf("%02xh", s[b].reply); else printf("-- ");
        if (s[b].acked) {
            printf("  /ACK +%6d..%6d (%d of %d)", s[b].ackMin, s[b].ackMax, s[b].acked, s[b].acked + s[b].silent);
            if (s[b].pulseMax >= 0) printf("  pulse %3d..%3d", s[b].pulseMin, s[b].pulseMax);
            if (s[b].latchOnly) printf("  latch only %d", s[b].latchOnly);
        } else printf("  no /ACK (%d)", s[b].silent);
        printf("\n");
        if (b == 9) {   /* the 128 data bytes as one row */
            int mn = 999999, mx = -1, pmn = 999999, pmx = -1, n = 0, sil = 0, lat = 0; long long sum = 0;
            for (int d = 10; d < 138; d++) {
                if (s[d].acked) { if (s[d].ackMin < mn) mn = s[d].ackMin; if (s[d].ackMax > mx) mx = s[d].ackMax; sum += s[d].ackSum; n += s[d].acked; lat += s[d].latchOnly; }
                if (s[d].pulseMax >= 0) { if (s[d].pulseMin < pmn) pmn = s[d].pulseMin; if (s[d].pulseMax > pmx) pmx = s[d].pulseMax; }
                sil += s[d].silent;
            }
            if (n) printf("  data 11-138  /ACK +%6d..%6d mean %d (%d acks, %d silent, %d latch only)  pulse %3d..%3d\n", mn, mx, (int)(sum / n), n, sil, lat, pmn, pmx);
            else printf("  data 11-138  never reached\n");
        }
    }
}

int main() {
    printf("\nsio/card-fetch2\n");
    printf("A memory card's whole read, byte by byte (READ ONLY): cycles from each byte's write (Timer 2 at the system clock),\n");
    printf("an /ACK seen by its level (JOY_STAT bit 7) or by its latch alone (bit 9, \"latch only\": its pulse unknown).\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    static ByteStat st[NBYTES];
    static int ends[REPS];
    const uint16_t prevT2 = initTimer(2, 0);
    for (int port = 0; port < 2; port++) {
        for (int f = 0; f < NFRAMES; f++) {
            for (int b = 0; b < NBYTES; b++) st[b] = { 999999, -1, 999999, -1, 0, 0, -1, 0, 0 };
            int ok = 0;
            EnterCriticalSection();
            for (int r = 0; r < REPS; r++) { ends[r] = -1; ok += readFrame(port, frames[f], st, &ends[r]); }
            ExitCriticalSection();
            if (!st[0].acked) { printf("port %d: no card (no /ACK to 81h)\n", port + 1); break; }
            report(port, frames[f], st, ok, ends);
        }
    }
    restoreTimer(2, prevT2);
    printf("\nresolution: one pass of the polling loop, at most %d cycles\n", loopCycles);
    printf("\nsio/card-fetch2: Done.\n");
    for (;;) VSync(0);
}
