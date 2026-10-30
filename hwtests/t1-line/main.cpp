/*
 * timer/t1-line - GPUSTAT bit 31's toggles dated from Timer 1's v-blank reset (2026-10-08, after t1-edges and t1-gate).
 *
 * WHY. On the SCPH-5501 Timer 1's hblank tick comes 80.7 cycles earlier against its own v-blank reset than this core's
 * (runner v5's vernier; 94 on the 7501 and 9001) and ~80 earlier against the polled VBlank IRQ (t1-edges), while the
 * reset sits at the same point of the line against the IRQ as here (t1-gate: two whole lines earlier, 4,310 cycles).
 * gpustat31 (three consoles) put the tick ~198 GPU ticks before GPUSTAT bit 31's per-line toggle, as this core does. So
 * either the LINE (bit 31's toggle, the tick with it) starts earlier against the IRQ and the reset on a console, or the
 * tick moved against the line after all. This dates bit 31's toggles from the reset, which t1-gate dates exactly.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; the display left as the runner leaves it (240 lines, VRAM Y
 * even: bit 31 0 in v-blank, toggling each line in the picture - gpustat31). Timer 1 at the system clock in sync mode 1.
 * Each frame: the VBlank edge polled, then Timer 1 read until it drops from a value under F000h (the reset, not the
 * wrap) - the reset's cycle is that read's Timer 2 less Timer 1's value - then GPUSTAT read in a tight loop for 12 bit-31
 * changes, each dated by the read that saw it and the one before, from the reset. 32 frames.
 *
 * READ IT LIKE THIS. Each change k: its earliest and latest bracket over the frames, cycles after the reset. The change's
 * place within the line (mod 2,152.5) on a console against this core's: ~80-94 earlier means the line starts earlier
 * against the reset (and the IRQ) - the IRQ and Timer 1's gate come later in the line there; the same means the tick moved.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-line --ntsc --frames=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define GPUSTAT  0x1F801814u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

#define FRAMES 32
#define CH 12
static int32_t lo[FRAMES][CH], hi[FRAMES][CH];
static uint8_t firstVal[FRAMES];

int main() {
    printf("\ntimer/t1-line\n");
    printf("GPUSTAT bit 31's changes after Timer 1's v-blank reset (system clock, sync mode 1), cycles from the reset. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0003);
    printf("GPUSTAT %08x\n", (unsigned)read32(GPUSTAT));
    for (int f = -1; f < FRAMES; f++) {
        write32(I_STAT, ~1u);
        const uint32_t w0 = now();
        while (!(read32(I_STAT) & 1)) if (now() - w0 > 1200000u) break;
        write32(I_STAT, ~1u);
        if (f < 0) continue;
        for (int k = 0; k < CH; k++) lo[f][k] = hi[f][k] = -1;
        /* the reset: a drop from below F000h (a wrap drops from ~FFFFh) */
        uint16_t prev = read16(T1_COUNT);
        uint32_t reset = 0; bool got = false;
        const uint32_t t0 = now();
        for (;;) {
            const uint16_t v = read16(T1_COUNT);
            const uint32_t t = now();
            if (v < prev && prev < 0xF000) { reset = t - v; got = true; break; }
            prev = v;
            if (t - t0 > 120000u) break;
        }
        if (!got) continue;
        uint32_t s = read32(GPUSTAT) >> 31, before = now();
        firstVal[f] = (uint8_t)s;
        for (int k = 0; k < CH;) {
            const uint32_t b = read32(GPUSTAT) >> 31;
            const uint32_t t = now();
            if (b != s) { lo[f][k] = (int32_t)(before - reset); hi[f][k] = (int32_t)(t - reset); s = b; k++; }
            before = t;
            if (t - reset > 60000u) break;
        }
    }
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int k = 0; k < CH; k++) {
        int32_t L = 0x7FFFFFFF, Lmax = -1, H = 0x7FFFFFFF, Hmin = 0x7FFFFFFF; int n = 0;
        for (int f = 0; f < FRAMES; f++) if (lo[f][k] >= 0) { n++; if (lo[f][k] > Lmax) Lmax = lo[f][k]; if (hi[f][k] < Hmin) Hmin = hi[f][k]; if (lo[f][k] < L) L = lo[f][k]; }
        (void)H;
        printf("  change %2d: after %6d, by %6d (the tightest over %d frames; earliest read-before %d)\n", k, (int)Lmax, (int)Hmin, n, (int)L);
    }
    printf("  bit 31 at the reset: %d (frame 0)\n", firstVal[0]);
    printf("\ntimer/t1-line: Done.\n");
    for (;;) VSync(0);
}
