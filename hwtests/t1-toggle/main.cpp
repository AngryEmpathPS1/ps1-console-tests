/*
 * timer/t1-toggle - Timer 1's hblank tick against GPUSTAT bit 31's toggle in the same line, by display mode, range and
 * video standard (2026-10-08, after runner v7's sweep).
 *
 * WHY. Runner v7's vernier put Timer 1's tick 5 dots after the display's end on the SCPH-5501 and 7501 (src/psx.js
 * T1_DOTS) - NTSC, the display on, ranges 260h..A60h-C60h, at the five dot clocks. Not measured: a PAL line, a range
 * whose end comes past the line's end, an empty range (X2 = X1, X2 < X1: psx-spx's "endless hblank"), an X1 off the dot
 * grid, the display switched off. The runner sets only the width and the range before it lands, so this test sets the
 * rest itself and measures within each line instead.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting hblanks (MODE 0100h). For each case: the
 * mode and range written, four frames waited, then on 4 frames, from 80 lines after the polled VBlank IRQ (past a PAL
 * v-blank on the NTSC display range too), 150 lines: a dithered pause, a tight wait for Timer 1 to change, Timer 2 read,
 * a tight wait for GPUSTAT bit 31 to change, Timer 2 read - the distance in cycles from a tick to the next toggle. Each
 * wait reads its register every ~8 cycles, so one distance is good to ~8 cycles and their mean over ~600 lines to a
 * fraction of one; the waits' own latency is the same on a console and on the virtual console, which is the comparison.
 * A tick after its line's toggle (a display ending past the line) reads as almost a whole line. Printed per case: the
 * mean, the median, min..max, the count, and how many waits timed out (no tick, or no toggle, within ~4,000 passes).
 *
 * READ IT LIKE THIS. This core: the tick at the display's end + 5 dots + 0.7 GPU ticks, at the line's end if later,
 * the toggle at the line's end (bit 31 changes with the line) - so toggle minus tick is (line - tick place) GPU ticks,
 * ~128 cycles at 320 wide, 260h..C60h. A console row that differs names a case the rule does not cover.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-toggle --ntsc --frames=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define GP1      0x1F801814u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static bool edge(uint32_t* at) {
    write32(I_STAT, ~1u);
    const uint32_t w0 = now();
    while (!(read32(I_STAT) & 1)) if (now() - w0 > 1200000u) return false;
    *at = now();
    write32(I_STAT, ~1u);
    return true;
}
static void isort(int32_t* v, int n) { for (int i = 1; i < n; i++) { const int32_t x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; } }

#define FRAMES 4
#define LINES 150
static int32_t dist[FRAMES * LINES + 8];
static uint32_t rng = 1;
static volatile uint16_t* const t1 = (volatile uint16_t*)T1_COUNT;
static volatile uint16_t* const t2 = (volatile uint16_t*)T2_COUNT;
static volatile uint32_t* const gs = (volatile uint32_t*)GP1;

static void measure(const char* name) {
    uint32_t irq;
    for (int i = 0; i < 4; i++) edge(&irq);
    int n = 0, lone = 0, toggles = 0;
    for (int f = 0; f < FRAMES; f++) {
        if (!edge(&irq)) { printf("  %-40s no VBlank IRQ\n", name); return; }
        /* 80 lines on (2,152.5 cycles each on NTSC; a PAL line is longer, so further on there) */
        while (now() - irq < 80u * 2153u) {}
        /* Per line: a dithered pause (0-31 passes of a short loop, so the polling's phase against the line is not the
         * line's own), then a tight wait for Timer 1 to change, Timer 2 read, a tight wait for bit 31 to change, Timer 2
         * read. Each wait reads its register every ~8 cycles; the first version polled both in one 58-cycle loop that
         * locked onto the line and read means 112-130 for one place (the virtual console). */
        for (int k = 0; k < LINES; k++) {
            rng = rng * 1103515245u + 12345u;
            for (volatile uint32_t d = (rng >> 16) & 31; d; d--) {}
            const uint16_t c0 = *t1;
            uint32_t guard = 0;
            while (*t1 == c0) if (++guard > 4000u) break;
            const uint16_t a = *t2;
            if (guard > 4000u) { lone++; continue; }
            const uint32_t s0 = *gs & 0x80000000u;
            guard = 0;
            while ((*gs & 0x80000000u) == s0) if (++guard > 4000u) break;
            const uint16_t b = *t2;
            if (guard > 4000u) { lone++; continue; }
            toggles++;
            if (n < FRAMES * LINES) dist[n++] = (int32_t)(uint16_t)(b - a);
        }
        lastT = read16(T2_COUNT);   /* now() picks up from here (its wraps in the lines above are not needed) */
    }
    int32_t sum = 0;   /* 600 lines of < 2,600 cycles: x 100 fits 32 bits (the toolchain has no 64-bit division) */
    for (int i = 0; i < n; i++) sum += dist[i];
    isort(dist, n);
    const int32_t mean100 = n ? sum * 100 / n : 0;
    printf("  %-40s GPUSTAT %08x  toggle - tick: mean %d.%02d, median %d, %d..%d cycles (%d lines; %d waits timed out)\n",
           name, (unsigned)read32(GP1), (int)(mean100 / 100), (int)(mean100 % 100), n ? (int)dist[n / 2] : -1,
           n ? (int)dist[0] : -1, n ? (int)dist[n - 1] : -1, n, lone);
}

static void range(uint32_t x1, uint32_t x2) { write32(GP1, 0x06000000u | (x2 << 12) | x1); }

int main() {
    printf("\ntimer/t1-toggle\n");
    printf("Timer 1's hblank tick against GPUSTAT bit 31's toggle in the same line, cycles, by mode and range. No assertions.\n\n");
    const uint32_t st0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0100);
    printf("  found: GPUSTAT %08x\n", (unsigned)st0);
    write32(GP1, 0x03000000u);                                       /* display on */
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);             /* lines 16..256 */
    /* NTSC, the cases the vernier measured, as controls */
    write32(GP1, 0x08000001u); range(0x260, 0xC60); measure("NTSC 320  260h..C60h");
    write32(GP1, 0x08000002u); range(0x260, 0xC60); measure("NTSC 512  260h..C60h");
    write32(GP1, 0x08000001u); range(0x260, 0xA60); measure("NTSC 320  260h..A60h");
    /* an X1 off the dot grid */
    write32(GP1, 0x08000001u); range(0x265, 0xC60); measure("NTSC 320  265h..C60h");
    write32(GP1, 0x08000001u); range(0x25B, 0xC5B); measure("NTSC 320  25Bh..C5Bh");
    write32(GP1, 0x08000000u); range(0x265, 0xC65); measure("NTSC 256  265h..C65h");
    /* the display's end near and past the line's end (3,412 GPU ticks) */
    write32(GP1, 0x08000001u); range(0x260, 0xD00); measure("NTSC 320  260h..D00h");
    write32(GP1, 0x08000001u); range(0x260, 0xD30); measure("NTSC 320  260h..D30h");
    write32(GP1, 0x08000001u); range(0x260, 0xD50); measure("NTSC 320  260h..D50h");
    write32(GP1, 0x08000001u); range(0x260, 0xFFF); measure("NTSC 320  260h..FFFh");
    /* an empty range */
    write32(GP1, 0x08000001u); range(0x260, 0x260); measure("NTSC 320  260h..260h (X2 = X1)");
    write32(GP1, 0x08000001u); range(0x260, 0x268); measure("NTSC 320  260h..268h (one dot)");
    write32(GP1, 0x08000001u); range(0xC60, 0x260); measure("NTSC 320  C60h..260h (X2 < X1)");
    write32(GP1, 0x08000001u); range(0x000, 0xA00); measure("NTSC 320  000h..A00h");
    /* the display off */
    write32(GP1, 0x08000001u); range(0x260, 0xC60); write32(GP1, 0x03000001u); measure("NTSC 320  260h..C60h, display OFF");
    write32(GP1, 0x03000000u);
    /* PAL lines (the PAL mode on this console's crystal), at the PAL games' range and at NTSC's */
    write32(GP1, 0x08000009u); range(0x260, 0xC60); measure("PAL  320  260h..C60h");
    write32(GP1, 0x0800000Au); range(0x260, 0xC60); measure("PAL  512  260h..C60h");
    write32(GP1, 0x08000009u); range(0x260, 0xA60); measure("PAL  320  260h..A60h");
    write32(GP1, 0x08000008u); range(0x260, 0xC60); measure("PAL  256  260h..C60h");
    /* put back the mode found: GP1(08h) from GPUSTAT's bits, display enable, the BIOS's range */
    const uint32_t gp08 = ((st0 >> 17) & 3) | (((st0 >> 19) & 1) << 2) | (((st0 >> 20) & 1) << 3) | (((st0 >> 21) & 1) << 4) |
                          (((st0 >> 22) & 1) << 5) | (((st0 >> 16) & 1) << 6) | (((st0 >> 14) & 1) << 7);
    range(0x260, 0xC60);
    write32(GP1, 0x08000000u | gp08);
    write32(GP1, 0x03000000u | ((st0 >> 23) & 1));
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t1-toggle: Done.\n");
    for (;;) VSync(0);
}
