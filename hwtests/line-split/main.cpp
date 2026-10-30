/*
 * gpu/line-split - which NTSC scanlines are 3412 video ticks long, and which
 * 3413?
 *
 * WHY (ROAD A8 "the 3412/3413 line split", dug 2026-09-27; ROAD B). An NTSC
 * line averages 3412.5 ticks of the video clock - settled by three consoles
 * (M108's ps1-tests frame; nocash's DIAG, 2172.3042 CPU cycles a line; mednafen's
 * 63.55456 us). So lines are 3412 and 3413 long, and WHICH lines are which is
 * measured nowhere. This emulator uses mednafen's toggle (gpu.c, PhaseChange):
 * every line the other length, so with 263 lines a frame the pattern flips
 * from one frame to the next (src/psx.js GPU.lineLen, KNOWN-OPEN). A beam-
 * timed effect or a light gun lands on a particular line's length.
 *
 * WHAT IT DOES. 240-line NTSC, interrupts off. Timer 1 counts hblanks and is
 * reset at every vblank (sync mode 1), so its value is the line number; Timer 2
 * counts the system clock. A tight loop watches Timer 1 and reads Timer 2 at
 * each step: the cycles between two steps are one line's length. They are
 * summed by line number and by frame parity over FRAMES frames. The two
 * lengths differ by 0.63 of a CPU cycle, far below one loop pass, so it is the
 * average over thousands of frames that separates them. It runs about three
 * minutes and prints nothing until it is done.
 *
 * READ IT LIKE THIS. Each line's mean minus the average, in hundredths of a
 * cycle. The average says the crystals: ~2,152.5 cycles a line on an NTSC
 * console, ~2,172.4 on a PAL console in NTSC mode (M108), and the two lengths
 * differ by ~0.63 of a cycle - about +-32 here. The L/S strings mark each line
 * longer or shorter than the average: a toggle reads LSLS... and flips between
 * the parities; a fixed rule reads the same pattern in both. Line 0 is left
 * out: it runs from the vblank reset to the first hblank, not a whole line.
 * Read the pattern from about line 60 on (below).
 *
 * ON THIS CORE (v0.258; run.js's machine has the PAL crystal, M108; 10,000
 * frames, 5,000 of each parity, 1.5 min with --frames=10800 --quiet=11000):
 * 2,172.36 cycles a line on both parities, and mednafen's toggle as src/psx.js
 * has it - parity 0 reads SLSL... from line 1 and parity 1 LSLS..., every line
 * of both. From about line 60 every line reads 31 to 35 off the average (one
 * tick is 0.637 of a cycle here, 31.8). The first ~60 lines read 13 to 52:
 * that is the loop's sampling phase, which each frame inherits from the
 * vblank reset and which takes that long to spread, not a line length - on
 * this core a line is 3412 or 3413 ticks and nothing else, so line 1 reading
 * -52 in one parity and +13 in the other is the toggle's -32/+32 with both
 * parities moved about 20 by the instrument.
 *
 * THE CONSOLES (SCPH-5501, 7501, 9001): +-51 from about line 60, alternating, the SAME sign as this core - and +-51
 * where a 1-tick difference gives +-32: the two lengths differ by ~1.6 ticks there, or the instrument reads them
 * differently (KNOWN-OPEN). AT v0.377.23 (Timer1's tick 200 ticks before the line ends, its v-blank reset a line later
 * and taking the next tick - T1_RETRACE, T1_VBLANK) the sign and the +-35 are as before; with the tick moved and the
 * reset where it was, the sign flipped - so this test also pins where sync mode 1 resets.
 * READ AGAIN (2026-10-08, v0.377.41): the +-51 is the instrument. It shows only in the runs whose average is exactly
 * 2,152.50 (the 7501's two, one of the 9001's): a line of 2,152.5 cycles whose phase does not move reads 2,152 and
 * 2,153 alternately in whole cycles. The runs at 2,152.55 (the 5501's two) and 2,152.45 (the 9001's other) - the
 * crystals' tolerance - read NO alternation (-0.1, +0.7, +2.3 in these hundredths; scratch_lsalt.js), where 3412/3413
 * ticks would read +-32: every NTSC line is 3412.5 ticks. This core since v0.377.41 (half-tick GPU clock): +-4.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define FRAMES 10000
#define LINES 263
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t sum[2][LINES + 8], cnt[2][LINES + 8];

/* A line's mean length in hundredths of a cycle. Each bin holds ~5,000 lines of ~2,170 cycles: x100 is ~1.1e9,
 * inside 32 bits (this toolchain's stdint.h has no 64-bit type). */
static int mean100(int p, int l) { return (int)((sum[p][l] * 100u + cnt[p][l] / 2) / cnt[p][l]); }

int main() {
    printf("\ngpu/line-split\n");
    printf("NTSC line lengths by line number and frame parity, %d frames. No assertions - compare against your emulator.\n\n", FRAMES);
    printf("GPUSTAT %08x (bit 20: PAL)\n", (unsigned)read32(0x1F801814));
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);                          /* the system clock, free-running */
    write16(T1_MODE, 0x0103);                          /* hblank clock, sync on, mode 1: reset at vblank */
    uint16_t last1 = read16(T1_COUNT), last2 = read16(T2_COUNT);
    int frames = 0, parity = 0;
    while (frames < FRAMES) {
        const uint16_t v1 = read16(T1_COUNT);
        if (v1 == last1) continue;
        const uint16_t v2 = read16(T2_COUNT);
        const uint32_t d = (uint16_t)(v2 - last2);
        if (v1 == last1 + 1) {
            if (last1 < LINES + 8) { sum[parity][last1] += d; cnt[parity][last1]++; }
        } else if (v1 < last1) {                       /* the reset at vblank: a new frame */
            frames++;
            parity ^= 1;
        }                                              /* a jump of 2 or more: a line missed, not counted */
        last1 = v1; last2 = v2;
    }
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    /* Line 0 is not a whole line - it runs from the vblank reset to the first hblank - so the report starts at 1. */
    for (int p = 0; p < 2; p++) {
        int top = 1;
        for (int l = 1; l < LINES + 8; l++) if (cnt[p][l]) top = l;
        int total = 0, n = 0;
        for (int l = 1; l <= top; l++) if (cnt[p][l]) { total += mean100(p, l); n++; }
        const int avg = n ? total / n : 0;
        printf("parity %d, lines 1-%d: average %d.%02d cycles a line; each line's mean minus that, in hundredths:\n",
               p, top, avg / 100, avg % 100);
        for (int l = 1; l <= top; l++) {
            printf("%+5d", cnt[p][l] ? mean100(p, l) - avg : 0);
            if (l % 16 == 0 || l == top) printf("\n");
        }
        printf("L/S: ");
        for (int l = 1; l <= top; l++) {
            printf("%c", !cnt[p][l] ? '.' : mean100(p, l) > avg ? 'L' : 'S');
            if (l % 64 == 0) printf("\n     ");
        }
        printf("\n(each line seen %d times)\n\n", (int)cnt[p][10]);
    }
    printf("An average near 2,172 is a PAL console's video crystal in NTSC mode, near 2,152 an NTSC console's (M108).\n");
    printf("3412 and 3413 ticks differ by ~0.63 of a cycle there: about +-32 above.\n\n");
    printf("gpu/line-split: Done.\n");
    for (;;) VSync(0);
}
