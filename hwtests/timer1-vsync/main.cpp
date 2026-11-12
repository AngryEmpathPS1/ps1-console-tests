/*
 * timer/timer1-vsync - which of the timers' sync inputs the OLD GPU's consoles honour: Timer 1's v-blank sync on the system
 * clock and on hblanks, and Timer 0's hblank sync on the system clock and on the dot clock (2026-11-12, main session).
 *
 * WHY (STATUS v0.377.325 "OPEN, found beside it: the OLD GPU's consoles ignore Timer 1's v-blank sync when it counts
 * hblanks"). timer-sync's VBLANK rows on the SCPH-1001 (six runs, 2026-10-09) and the SCPH-1000 (2026-11-09): Timer 1 at 0100h
 * plus each sync mode reads 263 at the next v-blank and counts on - no pause, no reset, no wait (mode 3 counts from the write)
 * - where every new-GPU console reads 239 / 237 / 0 / 238. The same log's GATE row (Timer 0, system clock, paused in hblank)
 * reads the new consoles' 1613.87, so the hblank reaches Timer 0. psx-spx (both copies) describes the sync modes for every
 * console; DuckStation's timers.cpp has no model split. Three readings fit timer-sync: (a) the old GPU sends no v-blank to the
 * timers (Timer 1 sysclock rows free too), (b) the timers ignore sync when the clock is the GPU's (Timer 1 sysclock rows sync,
 * and Timer 0 on the dot clock ignores its hblank sync), (c) only Timer 1's hblank-clock + v-blank pair (sysclock rows sync,
 * Timer 0 dot clock syncs).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock as the clock.
 *   T1 SYS / T1 HBL: Timer 1 at 0000h / 0100h plus sync mode m (0-3), written just after a v-blank starts (I_STAT bit 0
 *     polled); then the value just before the next v-blank start, and 40 samples one line apart (2,152 cycles) from it,
 *     printed as the first sample and the run-length deltas between samples (free run on the system clock: 2152 each, mod
 *     65536; on hblanks: 1 each; a pause: 0; a reset: a jump).
 *   T0 SYS / T0 DOT: Timer 0 at 0000h / 0100h plus sync mode m (0-3), sampled every 64 cycles for 16 lines (538 samples):
 *     how many samples went DOWN (a reset; free run only at a wrap), how many read the same as the one before (a pause;
 *     the dot clock ticks 6-13 times in 64 cycles), and the largest value. Mode 1 (reset at hblank) gives 16 downs, a
 *     largest under a line; mode 0 (pause in hblank) a run of stalls a line; mode 2 nearly all stalls; mode 3 frees after
 *     the first hblank.
 * READ IT LIKE THIS. Old console T1 SYS free and T0 DOT synced: (a). T1 SYS synced, T0 DOT free: (b). Both synced: (c).
 *
 * THE RULE: run it here first: `node hwtests/run.js timer1-vsync --ntsc --frames=1500` (PREDICTED.txt).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define GP1      0x1F801814u
#define GPUSTAT  0x1F801814u
#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* A v-blank start, bounded: false after ~2 frames with no I_STAT bit 0 (rule 1: every wait has a timeout). */
static bool vblankStart() {
    write32(I_STAT, ~1u);
    const uint32_t w0 = now();
    while (!(read32(I_STAT) & 1)) if (now() - w0 > 1200000u) return false;
    write32(I_STAT, ~1u);
    return true;
}

static const char* const vn[4] = { "0 pause in vblank", "1 reset at vblank", "2 reset, count in vblank", "3 wait once, then free" };
static const char* const hn[4] = { "0 pause in hblank", "1 reset at hblank", "2 reset, count in hblank", "3 wait once, then free" };

static void timer1Rows(uint16_t clockBits, const char* name) {
    printf("\n%s (Timer1 MODE %04Xh + sync, set just after a v-blank starts; before = the value just before the next v-blank\n"
           "starts, then 40 samples a line (2,152 cycles) apart from it: the first, then the deltas, value x count):\n", name, clockBits);
    for (int m = 0; m < 4; m++) {
        if (!vblankStart()) { printf("  mode %-24s NO VBLANK (timed out)\n", vn[m]); continue; }
        write16(T1_MODE, (uint16_t)(clockBits | (m << 1) | 1));
        uint16_t prev = read16(T1_COUNT);
        const uint32_t w0 = now(); bool late = false;
        while (!(read32(I_STAT) & 1)) { prev = read16(T1_COUNT); if (now() - w0 > 1200000u) { late = true; break; } }
        write32(I_STAT, ~1u);
        uint16_t v[40];
        const uint32_t t0 = now();
        for (int i = 0; i < 40; i++) { while (now() - t0 < 2152u * (uint32_t)i) {} v[i] = read16(T1_COUNT); }
        printf("  mode %-24s before %5u%s, first %5u, deltas", vn[m], prev, late ? " (TIMED OUT)" : "", v[0]);
        uint16_t d[39];
        for (int i = 0; i < 39; i++) d[i] = (uint16_t)(v[i + 1] - v[i]);
        for (int i = 0; i < 39;) { int j = i; while (j < 39 && d[j] == d[i]) j++; if (j - i > 1) printf(" %ux%d", d[i], j - i); else printf(" %u", d[i]); i = j; }
        printf("   MODE %04X\n", read16(T1_MODE) & 0x1FFF);
    }
    write16(T1_MODE, 0x0000);
}

static void timer0Rows(uint16_t clockBits, const char* name) {
    printf("\n%s (Timer0 MODE %04Xh + sync, sampled every 64 cycles for 16 lines, 538 samples):\n", name, clockBits);
    for (int m = -1; m < 4; m++) {
        if (!vblankStart()) { printf("  NO VBLANK (timed out)\n"); continue; }
        const uint32_t s0 = now();
        while (now() - s0 < 2152u * 40) {}                       /* into the display, away from the v-blank */
        write16(T0_MODE, (uint16_t)(clockBits | (m < 0 ? 0 : ((m << 1) | 1))));
        uint16_t last = read16(T0_COUNT);
        int downs = 0, stalls = 0; uint16_t hi = last;
        const uint32_t t0 = now();
        for (int i = 1; i < 538; i++) {
            while (now() - t0 < 64u * (uint32_t)i) {}
            const uint16_t c = read16(T0_COUNT);
            if (c < last) downs++; else if (c == last) stalls++;
            if (c > hi) hi = c;
            last = c;
        }
        if (m < 0) printf("  free (no sync)            downs %3d  stalls %3d  largest %5u\n", downs, stalls, hi);
        else printf("  mode %-21s downs %3d  stalls %3d  largest %5u   MODE %04X\n", hn[m], downs, stalls, hi, read16(T0_MODE) & 0x1FFF);
    }
    write16(T0_MODE, 0x0000);
}

int main() {
    printf("\ntimer/timer1-vsync\n");
    printf("Which sync inputs the timers honour: Timer1's v-blank on the system clock and on hblanks, Timer0's hblank on the\n"
           "system clock and on the dot clock. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000); lastT = read16(T2_COUNT);
    clk = 0;
    printf("  GPUSTAT %08X\n", read32(GPUSTAT));

    timer1Rows(0x0000, "T1 SYS");
    timer1Rows(0x0100, "T1 HBL");
    timer0Rows(0x0000, "T0 SYS");
    timer0Rows(0x0100, "T0 DOT");

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/timer1-vsync: Done.\n");
    for (;;) VSync(0);
}
