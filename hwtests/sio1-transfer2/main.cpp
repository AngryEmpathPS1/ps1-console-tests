/*
 * sio1/sio1-transfer2 - THE RESTART AFTER RTS, SPLIT AT THE SENDER'S CTS: sio1-transfer with the sender timing its own CTS
 * coming on, and the receiver's 20-ms stop stepped through a bit (2026-11-07).
 *
 * WHY. sio1-transfer on the real pair (console-logs/2026-11-07-link-transfer and -swap): the receiver's RTS raised to the next
 * byte's SR.1 takes 3,233-3,257 cycles when the SCPH-5501 sends to the SCPH-7501 and 3,538-3,558 the other way - ~300 cycles
 * (~9 us) by direction, and this emulator's pair 3,072-3,162 (STATUS "The restart after RTS, by direction"). The bridge Pico's
 * relay cannot split it: it stamps the lines from its main loop, 30-100 us late. The sender can: SR.8 is its CTS input's level
 * and SR.0 sets as its held byte starts (v0.377.83), so CTS-on to the start is the sender's own, and the rest of the total is
 * the wire (the receiver's RTS out, the sender's CTS in) and the frame (9.5 bits and two receiver ticks, 2,772 cycles here).
 * And the receiver's stop, 20 ms, is 2,352 of the sender's bits exactly: if the start waits for the sender's baud timer the
 * phase it meets is fixed in a session - so stop k lasts 20 ms + 18k cycles (k = 0..14, a sixteenth of a bit apart), and a
 * start quantised to the timer shows as a sawtooth over k, one that is not as a flat row (v0.377.83 measured a start from a
 * write "at every phase of the baud timer" one bit after it - not yet a start from CTS).
 *
 * WHAT IT DOES. As sio1-transfer (the runner's port: MODE 4Eh, BAUD 18, 288 cycles a bit; '#' and the role from the PC):
 * A sends 16,384 bytes of the LFSR's sequence as TX allows; in each wait for TX ready it polls SR and notes the first SR.8 rise
 * after it read 0, and SR.0's set: for every wait that saw CTS go off and on, CTS-on -> SR.0. B checks every byte, after each
 * 1,024 drops RTS for 20 ms + 18k, raises it, and notes the cycles to the next byte's SR.1. Both print; '!' from the PC ends it.
 * tools/linkrun.js --test=sio1-transfer2 (--swap for the other direction); tools/linkemu.js for this emulator's pair.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)
#define SIO_DATA 0x1F801050u
#define SIO_STAT 0x1F801054u
#define SIO_MODE 0x1F801058u
#define SIO_CTRL 0x1F80105Au
#define SIO_BAUD 0x1F80105Eu
#define N_BYTES 16384
#define STEP 18u            /* cycles a stop lengthens by: a sixteenth of the 288-cycle bit */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* THE END: the PC's '!' once the relay has closed (sio1-transfer's waitBang). */
static void waitBang() {
    uint32_t acc = 0, secs = 0, p = now();
    while (secs < 240) {
        const uint32_t q = now(); acc += q - p; p = q;
        while (acc >= 33868800u) { acc -= 33868800u; secs++; }
        if ((read16(SIO_STAT) & 2) && read8(SIO_DATA) == '!') break;
    }
}
static void idle(uint32_t c) { const uint32_t t = now(); while (now() - t < c) {} }
static bool rxReady() { return (read16(SIO_STAT) & 2) != 0; }
static uint32_t lfsr = 0x1234567u;
static uint8_t nextByte() { lfsr = lfsr * 1103515245u + 12345u; return (uint8_t)(lfsr >> 24); }

static uint8_t afterStop[16]; static uint32_t resume[16];
static uint32_t ctsToStart[16], ctsOffFor[16], pollMax;

int main() {
    printf("\nsio1/sio1-transfer2\nThe restart after RTS, split at the sender's CTS.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    write16(SIO_MODE, 0x004E); write16(SIO_BAUD, 0x0012); write16(SIO_CTRL, 0x0027);   /* the runner's settings */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);

    char role = 'B'; bool go = false;
    const uint32_t t0 = now();
    while (now() - t0 < MS(60000)) {
        if (!rxReady()) continue;
        const uint8_t c = read8(SIO_DATA);
        if (!go) { if (c == '#') go = true; continue; }
        if (c == 'A' || c == 'B') role = (char)c;
        break;
    }
    idle(MS(1500));                                                      /* the relay starts */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);

    uint32_t total = 0, waitMax = 0; int got = 0, bad = 0, stops = 0, overruns = 0, held = 0;
    if (role == 'A') {
        const uint32_t s = now();
        for (int i = 0; i < N_BYTES; i++) {
            const uint32_t w = now(); uint32_t t = w, prev = w, offAt = 0, onAt = 0; bool off = false;
            for (;;) {
                const uint16_t st = read16(SIO_STAT); t = now();
                if (t - prev > pollMax && i) pollMax = t - prev;
                prev = t;
                if (!(st & 0x100)) { if (!off) { off = true; offAt = t; } }
                else if (off && !onAt) onAt = t;
                if (st & 1) break;
                if (t - w > MS(2000)) break;
            }
            const uint32_t d = t - w; if (d > waitMax) waitMax = d;
            if (onAt && held < 16) { ctsToStart[held] = t - onAt; ctsOffFor[held] = onAt - offAt; held++; }
            write8(SIO_DATA, nextByte());
        }
        while (!(read16(SIO_STAT) & 4)) { if (now() - s > MS(60000)) break; }   /* the last byte out (TX idle, SR.2) */
        total = now() - s;
    } else {
        const uint32_t s = now(); uint32_t last = now();
        while (got < N_BYTES && now() - last < MS(30000)) {
            if (!rxReady()) continue;
            const uint16_t st = read16(SIO_STAT);
            if (st & 0x10) overruns++;
            const uint8_t c = read8(SIO_DATA);
            if (c != nextByte()) bad++;
            got++; last = now();
            if ((got & 1023) == 0 && got < N_BYTES && stops < 16) {
                write16(SIO_CTRL, 0x0007);                                   /* RTS off: the sender's CTS drops */
                const uint32_t p = now(), gap = MS(20) + STEP * (uint32_t)stops; int more = 0;
                while (now() - p < gap) { if (rxReady()) { const uint8_t c2 = read8(SIO_DATA); if (c2 != nextByte()) bad++; got++; more++; } }
                afterStop[stops] = (uint8_t)more;
                write16(SIO_CTRL, 0x0027);                                   /* RTS on */
                const uint32_t r = now();
                while (!rxReady() && now() - r < MS(100)) {}
                resume[stops] = now() - r;
                stops++;
                last = now();
            }
        }
        total = now() - s;
    }
    waitBang();                                                          /* the relay ends, then the PC's '!' */
    for (int i = 0; i < 64 && rxReady(); i++) (void)read8(SIO_DATA);
    write16(SIO_CTRL, 0x0027 | 0x0010);
    ExitCriticalSection();
    if (role == 'A') {
        printf("role A: %d bytes sent in %u cycles (%u a byte), the longest wait for TX ready %u cycles, the poll at most %u cycles\n",
               N_BYTES, (unsigned)total, (unsigned)(total / N_BYTES), (unsigned)waitMax, (unsigned)pollMax);
        printf("waits that saw CTS go off and on: %d; CTS on -> SR.0 (the held byte's start), cycles:", held);
        for (int i = 0; i < held; i++) printf(" %u", (unsigned)ctsToStart[i]);
        printf("\nCTS off for, cycles:");
        for (int i = 0; i < held; i++) printf(" %u", (unsigned)ctsOffFor[i]);
        printf("\n");
    } else {
        printf("role B: %d bytes received, %d not the sequence's, %d reads with the overrun flag, in %u cycles\n", got, bad, overruns, (unsigned)total);
        printf("RTS dropped %d times (stop k: 20 ms + %uk cycles); bytes that still came after:", stops, (unsigned)STEP);
        for (int i = 0; i < stops; i++) printf(" %d", afterStop[i]);
        printf("\nRTS raised: cycles to the next byte's SR.1:");
        for (int i = 0; i < stops; i++) printf(" %u", (unsigned)resume[i]);
        printf("\n");
    }
    printf("go %s, role %c\nsio1/sio1-transfer2: Done.\n", go ? "received" : "NOT received", role);
    return 0;
}
