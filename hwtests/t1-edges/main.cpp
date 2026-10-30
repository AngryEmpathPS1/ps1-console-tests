/*
 * timer/t1-edges - where Timer 1's v-blank reset falls against its hblank tick, both dated from the VBlank IRQ
 * (2026-10-08, after runner v5's first console run).
 *
 * WHY. Runner v5 reads Timer 1's hblank tick once a line for 120 lines after each landing, the landing being Timer 1 =
 * 24,000 at the system clock after its sync-mode-1 reset (where its v-blank gate ends, M236). On the SCPH-7501 and 9001
 * the tick comes 94 cycles before this core's, on the 5501 80.7 +- 0.1 (tools/vernfit.js, console-logs/2026-10-08-*-
 * v5check) - every run, every console. That is the tick AGAINST THE RESET. gpustat31 and timer-sync placed the tick in
 * the line as the consoles do (v0.377.23), so the reset is the suspect - but neither test dates either edge to a cycle.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, extended to 32 bits. Each frame: I_STAT bit 0 acknowledged,
 * then polled for the VBlank edge (a tight loop - its latency is the same here and on a console), the edge dated on
 * Timer 2. Then, alternate frames:
 *   RESET  Timer 1 at the system clock, sync mode 1 (MODE 0003h, written a frame before): read with Timer 2 until it
 *          drops. It has counted the cycles since the reset, so the reset's cycle is that Timer 2 read less Timer 1's
 *          value (less the two reads' spacing, a constant). Printed: reset - IRQ.
 *   TICK   Timer 1 counting hblanks (MODE 0100h): read with Timer 2 until it has changed 30 times; each change dated by
 *          the read that saw it and the one before (a bracket). Printed: each tick - IRQ, tick k within a frame.
 * 48 frames of each. Then the two put together: the reset against the tick before it and the one after, which is the
 * quantity the vernier measured (on this core the reset lands where it does against the ticks, a console 80-94 off).
 *
 * READ IT LIKE THIS. Each row: min / median / max over the frames, in cycles from the polled IRQ edge. Two consoles
 * agree with this core on TICK - IRQ and differ on RESET - IRQ by ~80-94: the reset (the gate's end) is placed wrong in
 * the line. The other way round: the tick is. Both differ: the IRQ itself moved.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-edges --frames=1200`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

#define FRAMES 48
#define TICKS 30
static int32_t resetD[FRAMES];
static int32_t tickLo[FRAMES][TICKS], tickHi[FRAMES][TICKS];

/* the VBlank edge, polled; false if none in ~2 frames */
static bool vblankEdge(uint32_t* at) {
    write32(I_STAT, ~1u);
    const uint32_t t0 = now();
    while (!(read32(I_STAT) & 1)) if (now() - t0 > 1200000u) return false;
    *at = now();
    write32(I_STAT, ~1u);
    return true;
}

/* the toolchain has no qsort */
static void isort(int32_t* v, int n) { for (int i = 1; i < n; i++) { const int32_t x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; } }
static void stats(const char* name, int32_t* v, int n) {
    static int32_t s[FRAMES];
    int m = 0;
    for (int i = 0; i < n; i++) if (v[i] != (int32_t)0x80000000) s[m++] = v[i];
    if (!m) { printf("  %-34s none\n", name); return; }
    isort(s, m);
    printf("  %-34s %7d / %7d / %7d   (%d frames)\n", name, (int)s[0], (int)s[m / 2], (int)s[m - 1], m);
}

int main() {
    printf("\ntimer/t1-edges\n");
    printf("Timer 1's v-blank reset and its hblank tick, cycles from the polled VBlank IRQ edge: min / median / max. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;

    uint32_t irq;
    /* RESET */
    write16(T1_MODE, 0x0003);                 /* system clock, sync on, mode 1: reset at the v-blank gate */
    vblankEdge(&irq);
    for (int f = 0; f < FRAMES; f++) {
        resetD[f] = (int32_t)0x80000000;
        if (!vblankEdge(&irq)) continue;
        uint16_t prev = read16(T1_COUNT);
        for (;;) {
            const uint16_t v = read16(T1_COUNT);
            const uint32_t t = now();
            if (v < prev) { resetD[f] = (int32_t)(t - v - irq); break; }
            prev = v;
            if (t - irq > 200000u) break;      /* no reset within ~90 lines */
        }
    }
    /* TICK */
    write16(T1_MODE, 0x0100);                 /* hblank clock, free running */
    vblankEdge(&irq);
    for (int f = 0; f < FRAMES; f++) {
        for (int k = 0; k < TICKS; k++) { tickLo[f][k] = tickHi[f][k] = (int32_t)0x80000000; }
        if (!vblankEdge(&irq)) continue;
        uint16_t prev = read16(T1_COUNT);
        uint32_t before = now();
        for (int k = 0; k < TICKS;) {
            const uint16_t v = read16(T1_COUNT);
            const uint32_t t = now();
            if (v != prev) { tickLo[f][k] = (int32_t)(before - irq); tickHi[f][k] = (int32_t)(t - irq); k++; prev = v; }
            before = t;
            if (t - irq > 200000u) break;
        }
    }
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    stats("RESET - IRQ", resetD, FRAMES);
    static int32_t col[FRAMES];
    for (int k = 0; k < TICKS; k++) {
        char name[48];
        for (int f = 0; f < FRAMES; f++) col[f] = tickLo[f][k];
        sprintf(name, "TICK %2d - IRQ, read before", k);
        stats(name, col, FRAMES);
        for (int f = 0; f < FRAMES; f++) col[f] = tickHi[f][k];
        sprintf(name, "TICK %2d - IRQ, read after", k);
        stats(name, col, FRAMES);
    }
    /* the reset against the ticks around it, by medians */
    static int32_t s[FRAMES]; int m = 0;
    for (int f = 0; f < FRAMES; f++) if (resetD[f] != (int32_t)0x80000000) s[m++] = resetD[f];
    if (m) {
        isort(s, m);
        const int32_t r = s[m / 2];
        for (int k = 0; k + 1 < TICKS; k++) {
            int n = 0;
            for (int f = 0; f < FRAMES; f++) if (tickHi[f][k] != (int32_t)0x80000000) col[n++] = tickHi[f][k];
            if (!n) continue;
            isort(col, n);
            const int32_t a = col[n / 2];
            n = 0;
            for (int f = 0; f < FRAMES; f++) if (tickHi[f][k + 1] != (int32_t)0x80000000) col[n++] = tickHi[f][k + 1];
            if (!n) continue;
            isort(col, n);
            const int32_t b = col[n / 2];
            if (a <= r && r < b) { printf("\n  the reset (median %d) lies %d cycles after tick %d's read and %d before tick %d's\n", (int)r, (int)(r - a), k, (int)(b - r), k + 1); break; }
        }
    }
    printf("\ntimer/t1-edges: Done.\n");
    for (;;) VSync(0);
}
