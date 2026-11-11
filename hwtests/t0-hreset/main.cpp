/*
 * timer/t0-hreset - where Timer 0's reset by hblank (sync mode 1) lands against Timer 1's hblank tick and GPUSTAT bit 31's
 * toggle in the same line (2026-11-11, main session).
 *
 * WHY (STATUS "OPEN: Timer 0's hblank reset and GPUSTAT bit 31's toggle are 16.6 cycles further apart here ..."). gpu-a0abort2
 * and -3 read Timer 0 (T0_MODE 0003h: system clock, reset at hblank) a fixed wait after bit 31's toggle: on the second
 * SCPH-5501 its count there reads 16.5-16.8 cycles higher than on v0.377.321, over 10,368 rows. One difference of two edges:
 * the reset is later against the toggle, or the toggle earlier against the reset. Bit 31 against Timer 1's tick is measured
 * (t1-toggle, T1_DOTS) and this core reads the consoles' there, so the suspect is the reset - or a poll's own latency.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock as the clock, Timer 1 counting hblanks (0100h), Timer 0 in the
 * mode of the case. Per line (150 lines on 4 frames, 80 lines after a polled VBlank, a dithered pause first, as t1-toggle):
 * a tight wait for Timer 1 to change, then Timer 0 read (A: Timer 0's count at the tick, plus the wait's latency) and Timer 2;
 * a tight wait for bit 31 to change, then Timer 0 read (B: its count at the toggle, what gpu-a0abort2/3 read) and Timer 2
 * (C = toggle - tick, t1-toggle's number, the control). Each wait polls every ~8 cycles; the latency is the same on a console
 * and on the virtual console, which is the comparison. A third pass waits for Timer 0 to go DOWN (its reset) after the tick:
 * D = reset - tick on Timer 2, negative never (a reset before the tick shows as nearly a line).
 *
 * READ IT LIKE THIS. If the console's A and D are ~17 cycles from this core's and C is the same, the reset is not at the
 * tick's instant: the fix is the reset, not bit 31. If A and D agree and C differs by ~17, it is bit 31. If all three agree,
 * gpu-a0abort's offset is its own poll's latency (a GPUSTAT read inside a transfer). Cases: the five dot clocks at 260h..C60h,
 * 512 at 260h..A60h (the runner's), Timer 0 on the dot clock in sync mode 1 (0103h, M105's external-clock reset), and
 * free-running (0000h) as a control that never resets.
 *
 * THE RULE: run it here first: `node hwtests/run.js t0-hreset --ntsc --frames=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define GP1      0x1F801814u
#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
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
#define NMAX (FRAMES * LINES + 8)
static int32_t va[NMAX], vb[NMAX], vc[NMAX], vd[NMAX];
static uint32_t rng = 1;
static volatile uint16_t* const t0 = (volatile uint16_t*)T0_COUNT;
static volatile uint16_t* const t1 = (volatile uint16_t*)T1_COUNT;
static volatile uint16_t* const t2 = (volatile uint16_t*)T2_COUNT;
static volatile uint32_t* const gs = (volatile uint32_t*)GP1;

/* A reading taken just after a poll saw an edge is bimodal when the reset falls near that edge: a count since the reset
 * (small) or the line before's count (near a line, ~2,150). Each half is printed on its own, so a mean never mixes them;
 * how many land in each half is itself the measurement (the reset's place against the poll). */
static void half(int32_t* v, int lo, int hi) {
    const int n = hi - lo;
    if (n <= 0) { printf(" none"); return; }
    int32_t sum = 0;   /* 600 values under 4,000: x 100 fits 32 bits (no 64-bit division in the toolchain) */
    for (int i = lo; i < hi; i++) sum += v[i];
    const int32_t m = sum * 100 / n;
    printf(" %d: mean %d.%02d, median %d, %d..%d", n, (int)(m / 100), (int)(m % 100), (int)v[lo + n / 2], (int)v[lo], (int)v[hi - 1]);
}
static void stats(const char* what, int32_t* v, int n) {
    isort(v, n);
    int s = 0; while (s < n && v[s] < 1000) s++;
    printf("    %s under 1000", what); half(v, 0, s); printf(" | from 1000"); half(v, s, n); printf("\n");
}

static void measure(const char* name, uint16_t t0mode) {
    write16(T0_MODE, t0mode);
    uint32_t irq;
    for (int i = 0; i < 4; i++) edge(&irq);
    int na = 0, nd = 0, lone = 0;
    for (int f = 0; f < FRAMES; f++) {
        if (!edge(&irq)) { printf("  %-40s no VBlank IRQ\n", name); return; }
        while (now() - irq < 80u * 2153u) {}
        for (int k = 0; k < LINES; k++) {
            rng = rng * 1103515245u + 12345u;
            for (volatile uint32_t d = (rng >> 16) & 31; d; d--) {}
            /* odd lines: A, B, C; even lines: D (one wait each after the tick, so neither shifts the other) */
            const uint16_t c0 = *t1;
            uint32_t guard = 0;
            while (*t1 == c0) if (++guard > 4000u) break;
            const uint16_t za = *t0;
            const uint16_t a = *t2;
            if (guard > 4000u) { lone++; continue; }
            if (k & 1) {
                const uint32_t s0 = *gs & 0x80000000u;
                guard = 0;
                while ((*gs & 0x80000000u) == s0) if (++guard > 4000u) break;
                const uint16_t zb = *t0;
                const uint16_t b = *t2;
                if (guard > 4000u) { lone++; continue; }
                if (na < NMAX) { va[na] = za; vb[na] = zb; vc[na] = (int32_t)(uint16_t)(b - a); na++; }
            } else {
                uint16_t p = za;
                guard = 0;
                for (;;) { const uint16_t z = *t0; if (z < p) break; p = z; if (++guard > 4000u) break; }
                const uint16_t d = *t2;
                if (guard > 4000u) { lone++; continue; }
                if (nd < NMAX) vd[nd++] = (int32_t)(uint16_t)(d - a);
            }
        }
        lastT = read16(T2_COUNT);
    }
    printf("  %-40s T0_MODE %04x  GPUSTAT %08x  (%d waits timed out)\n", name, t0mode, (unsigned)read32(GP1), lone);
    stats("A T0 at the tick:     ", va, na);
    stats("B T0 at the toggle:   ", vb, na);
    stats("C toggle - tick (T2): ", vc, na);
    stats("D reset - tick (T2):  ", vd, nd);
}

static void range(uint32_t x1, uint32_t x2) { write32(GP1, 0x06000000u | (x2 << 12) | x1); }

int main() {
    printf("\ntimer/t0-hreset\n");
    printf("Timer 0's reset by hblank against Timer 1's tick and GPUSTAT bit 31's toggle, cycles. No assertions.\n\n");
    const uint32_t st0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0100);
    printf("  found: GPUSTAT %08x\n", (unsigned)st0);
    write32(GP1, 0x03000000u);                                       /* display on */
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);             /* lines 16..256 */
    /* gpu-a0abort2/3's own: the runner's 512 wide, Timer 0 0003h */
    write32(GP1, 0x08000002u); range(0x260, 0xC60); measure("NTSC 512  260h..C60h  sysclk reset", 0x0003);
    write32(GP1, 0x08000002u); range(0x260, 0xA60); measure("NTSC 512  260h..A60h  sysclk reset", 0x0003);
    /* the five dot clocks */
    write32(GP1, 0x08000000u); range(0x260, 0xC60); measure("NTSC 256  260h..C60h  sysclk reset", 0x0003);
    write32(GP1, 0x08000001u); range(0x260, 0xC60); measure("NTSC 320  260h..C60h  sysclk reset", 0x0003);
    write32(GP1, 0x08000040u); range(0x260, 0xC60); measure("NTSC 368  260h..C60h  sysclk reset", 0x0003);
    write32(GP1, 0x08000003u); range(0x260, 0xC60); measure("NTSC 640  260h..C60h  sysclk reset", 0x0003);
    /* Timer 0 on the dot clock, reset at hblank (M105: an external clock's reset shows 6 cycles after the tick) */
    write32(GP1, 0x08000001u); range(0x260, 0xC60); measure("NTSC 320  260h..C60h  dotclk reset", 0x0103);
    /* control: free-running, never reset (D times out: no reset) */
    write32(GP1, 0x08000001u); range(0x260, 0xC60); measure("NTSC 320  260h..C60h  free (control)", 0x0000);
    const uint32_t gp08 = ((st0 >> 17) & 3) | (((st0 >> 19) & 1) << 2) | (((st0 >> 20) & 1) << 3) | (((st0 >> 21) & 1) << 4) |
                          (((st0 >> 22) & 1) << 5) | (((st0 >> 16) & 1) << 6) | (((st0 >> 14) & 1) << 7);
    range(0x260, 0xC60);
    write32(GP1, 0x08000000u | gp08);
    write32(GP1, 0x03000000u | ((st0 >> 23) & 1));
    write16(T0_MODE, m0);
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t0-hreset: Done.\n");
    for (;;) VSync(0);
}
