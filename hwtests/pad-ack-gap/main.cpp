/*
 * sio/pad-ack-gap - what a controller times its /ACK FROM: the CPU's write, the byte's last clock edge, or its own last /ACK.
 *
 * WHY (2026-11-12). v0.377.331 times a digital pad's and a DualShock's /ACK from the byte's WRITE (two places a byte, ~190-235
 * apart): on hwtests/pad-ack-hist x47 the clusters stay ~16 cycles wide where the same chain's RX spreads over a whole bit.
 * But a pad cannot see the write - only /SEL, the clock and its own past. In pad-ack-hist the CPU writes each next byte a
 * fixed time after the last /ACK ended (`wait(400)`), so "from the write" and "from the pad's own last /ACK" are the same
 * there. They part when the gap varies. It decides src/psx.js SIO.padAckDelay's reference, and what the Pico TAS player
 * (pico-tas/padcore.c, which sees only the clock) can do to answer as a Sony pad does (experiments/proposals/sio-dma-refit.md).
 *
 * WHAT IT DOES. pad-ack-hist's poll (port 1, 01 42 00 00, 250 kHz, interrupts off), with G more cycles before each byte's
 * write after the last /ACK ended: G = 0, 17, 34 .. 119 (one bit period, 136, in eight steps) and 272, 680. For each G,
 * 128 polls timing /ACK and 128 timing RX, both from the write, every byte; per byte the mean, and the histogram in 8-cycle
 * bins.
 *
 * READ IT LIKE THIS. Bytes 2-4 (byte 1 follows /SEL, not an /ACK):
 *   - /ACK - write the same at every G: timed from the WRITE (or from something the write sets off) - v0.377.331.
 *   - /ACK - write tracking RX - write (a sawtooth over the eight steps, the byte's wait for the baud clock's bit edge):
 *     timed from the byte's last edge - v0.377.330's 500.
 *   - /ACK - write FALLING by G (to the floor where the byte is still being clocked): timed from the pad's own last /ACK.
 * RX - write is the instrument's own check: it must sawtooth over the eight steps (the bit edge) on every rule.
 *
 * THE RULE: run it here first: `node hwtests/run.js pad-ack-gap`. Port 1: a Sony digital pad (or a DualShock in digital
 * mode); say which.
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
#define REPS 128
#define BIN 8
#define LO 1200
#define NBINS 128                                  /* 1,200 .. 2,224 */
#define NG 10

static const int GAPS[NG] = { 0, 17, 34, 51, 68, 85, 102, 119, 272, 680 };
static uint16_t hist[NG][2][4][NBINS + 2];
static uint32_t sum[NG][2][4], cnt[NG][2][4];
static int none[NG][2][4];

static inline uint16_t t2() { return read16(T2); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) { } }

static int __attribute__((noinline)) until(uint32_t bit, uint16_t t0) {
    for (int i = 0; i < 2000; i++) if (read32(JOY_STAT) & bit) return (uint16_t)(t2() - t0);
    return -1;
}

static void poll(int g, int kind) {
    static const uint8_t tx[4] = { 0x01, 0x42, 0x00, 0x00 };
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(20000);
    write16(JOY_CTRL, 0x0003);
    wait(1500);
    for (int b = 0; b < 4; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        const int t = until(kind == 0 ? 0x80 : 0x02, t0);
        if (t < 0) { none[g][kind][b]++; break; }
        sum[g][kind][b] += t; cnt[g][kind][b]++;
        const int i = (t - LO) / BIN;
        hist[g][kind][b][t < LO ? NBINS : (i >= NBINS ? NBINS + 1 : i)]++;
        if (kind == 1) until(0x80, t0);
        for (int k = 0; k < 4000 && (read32(JOY_STAT) & 0x80); k++) { }
        wait(400 + GAPS[g]);
    }
    write16(JOY_CTRL, 0x0000);
}

int main() {
    printf("\nsio/pad-ack-gap\n");
    printf("Port 1's controller, 01 42 00 00: /ACK and RX from each byte's write, with G more cycles before each write. %d polls\n", REPS);
    printf("each, %d-cycle bins. No assertions - compare against your emulator, and say which controller was in port 1.\n\n", BIN);
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    for (int g = 0; g < NG; g++) {
        for (int r = 0; r < REPS; r++) poll(g, 0);
        for (int r = 0; r < REPS; r++) poll(g, 1);
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    static const char* names[2] = { "/ACK", "RX  " };
    printf("means (byte 1 / 2 / 3 / 4):\n");
    for (int g = 0; g < NG; g++)
        for (int k = 0; k < 2; k++) {
            printf("  G %3d %s", GAPS[g], names[k]);
            for (int b = 0; b < 4; b++) {
                const uint32_t n = cnt[g][k][b];
                if (n) printf("  %4d.%02d", (int)(sum[g][k][b] / n), (int)((100 * (sum[g][k][b] % n)) / n)); else printf("        -");
                if (none[g][k][b]) printf("(%d never)", none[g][k][b]);
            }
            printf("\n");
        }
    printf("\nhistograms, bytes 2-4:\n");
    for (int g = 0; g < NG; g++)
        for (int k = 0; k < 2; k++)
            for (int b = 1; b < 4; b++) {
                printf("  G %3d %s byte %d:", GAPS[g], names[k], b + 1);
                if (hist[g][k][b][NBINS]) printf(" <%d:%d", LO, hist[g][k][b][NBINS]);
                for (int i = 0; i < NBINS; i++) if (hist[g][k][b][i]) printf(" %d:%d", LO + i * BIN, hist[g][k][b][i]);
                if (hist[g][k][b][NBINS + 1]) printf(" >=%d:%d", LO + NBINS * BIN, hist[g][k][b][NBINS + 1]);
                printf("\n");
            }
    printf("\nsio/pad-ack-gap: Done.\n");
    for (;;) VSync(0);
}
