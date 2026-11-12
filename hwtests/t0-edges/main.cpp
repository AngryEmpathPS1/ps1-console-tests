/*
 * timer/t0-edges - every edge Timer 0's hblank sync makes in a line, on the system clock and on the dot clock: where the count
 * stops, where it resumes, where it resets, against Timer 1's hblank tick (2026-11-12, main session).
 *
 * WHY. v0.377.322 (hwtests/t0-hreset) placed sync mode 1's reset on the system clock to a cycle, and left two things open
 * (KNOWN-OPEN in Timers.catchUp0): on the DOT clock (0103h) the console's count after the reset reads 3.1-3.2 dots below this
 * core's (the second SCPH-5501 x6, the SCPH-7502) while the reset itself is seen only ~4 cycles later - less than a dot, so not a
 * later reset but a count that starts late; and "the hblank START edge (sync mode 2) and the gate's edges are unmeasured".
 * timer1-vsync's T0 rows hint at the gate too: in mode 0 on the dot clock the 5501 counts a dot a line fewer than this core.
 * mednafen's timer notes describe the same family for Timer 1 ("counting again is delayed").
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock as the clock, Timer 1 counting hblanks (0100h). Per case, Timer 0 in
 * its mode, then 360 lines (3 frames x 120, 40 lines after a polled VBlank): a dithered pause, a wait for Timer 1 to change (the
 * tick), then Timer 0 and Timer 2 read in a tight loop until 2,200 cycles past the tick (about a line), each pair kept. Then,
 * for each step between two samples (dt cycles on Timer 2, dz counts on Timer 0, z the count after):
 *   reset   z < the count before: at t - z x (cycles a count)
 *   stop    counting before, dz below the step's dt (sysclk) or 0 (dot clock): at the step's start + dz x (cycles a count)
 *   resume  stopped before, counting now: at the step's end - dz x (cycles a count)
 * (a count: 1 cycle, or a dot = divider x 7/11 cycles). Sync mode 3 (wait for an hblank, then free) is written again after each
 * tick, so each line shows its one edge. Each kind's places are sorted and split where 64 cycles separate them; each cluster
 * printed as n, mean, median, min..max, in cycles after the tick (the tick's own poll latency is the program's, the same on a
 * console and here). Cases: 320 and 512 wide at 260h..C60h, sysclk 0001 / 0003 / 0005 / 0007 and dot clock 0101 / 0103 / 0105
 * / 0107, free-running 0000 / 0100 as controls (no edges).
 *
 * THE RULE: run it here first: `node hwtests/run.js t0-edges --ntsc --frames=2500` (PREDICTED.txt).
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
static void isort(int16_t* v, int n) { for (int i = 1; i < n; i++) { const int16_t x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; } }

#define FRAMES 3
#define LINES 120
#define NS 400
#define NE 1500
static uint16_t sz[NS], st[NS];
static int16_t ev[3][NE]; static int nev[3];
static uint32_t rng = 1;
static volatile uint16_t* const t0 = (volatile uint16_t*)T0_COUNT;
static volatile uint16_t* const t1 = (volatile uint16_t*)T1_COUNT;
static volatile uint16_t* const t2 = (volatile uint16_t*)T2_COUNT;

static void put(int k, int32_t x) { if (x > -2000 && x < 4000 && nev[k] < NE) ev[k][nev[k]++] = (int16_t)x; }

static void clusters(const char* what, int k) {
    int16_t* v = ev[k]; const int n = nev[k];
    printf("    %-7s", what);
    if (!n) { printf(" none\n"); return; }
    isort(v, n);
    for (int lo = 0; lo < n;) {
        int hi = lo + 1; while (hi < n && v[hi] - v[hi - 1] <= 64) hi++;
        int32_t sum = 0; for (int i = lo; i < hi; i++) sum += v[i];
        const int32_t m = sum * 100 / (hi - lo);
        const int32_t am = m < 0 ? -m : m;
        printf(" [%d: mean %s%d.%02d, median %d, %d..%d]", hi - lo, m < 0 ? "-" : "", (int)(am / 100), (int)(am % 100), (int)v[lo + (hi - lo) / 2], (int)v[lo], (int)v[hi - 1]);
        lo = hi;
    }
    printf("\n");
}

/* cpc100: cycles a count x 100 (100 on the system clock; a dot = divider x 7/11 x 100) */
static void measure(const char* name, uint16_t mode, int32_t cpc100) {
    const bool dot = (mode & 0x0100) != 0, rewrite = (mode & 7) == 7;
    nev[0] = nev[1] = nev[2] = 0;
    write16(T0_MODE, mode);
    uint32_t irq; int lone = 0, lines = 0;
    for (int i = 0; i < 2; i++) edge(&irq);
    for (int f = 0; f < FRAMES; f++) {
        if (!edge(&irq)) { printf("  %-34s no VBlank IRQ\n", name); return; }
        while (now() - irq < 40u * 2153u) {}
        for (int l = 0; l < LINES; l++) {
            rng = rng * 1103515245u + 12345u;
            for (volatile uint32_t d = (rng >> 16) & 31; d; d--) {}
            const uint16_t c0 = *t1;
            uint32_t guard = 0;
            while (*t1 == c0) if (++guard > 4000u) break;
            const uint16_t a = *t2;
            if (guard > 4000u) { lone++; continue; }
            if (rewrite) write16(T0_MODE, mode);
            int n = 0;
            for (;;) { const uint16_t z = *t0; const uint16_t t = *t2; if (n < NS) { sz[n] = z; st[n] = (uint16_t)(t - a); n++; } if ((uint16_t)(t - a) > 2200u || n >= NS) break; }
            lines++;
            /* A step is STILL when its count moved at most 1 (a count a cycle moves 8 or more in a step). A stop is a step that
             * counted less than its time, followed by a still step - so a read held up between the two timers (dz short of dt,
             * then long) is not one; a resume is a step that counts after a still one. A "reset" from F000h up is the wrap. */
            bool counting = true;
            for (int i = 1; i < n; i++) {
                const int32_t dt = (int32_t)(uint16_t)(st[i] - st[i - 1]);
                const uint16_t pz = sz[i - 1], z = sz[i];
                if (z < pz) { if (pz < 0xF000u) put(0, (int32_t)st[i] - (int32_t)z * cpc100 / 100); counting = true; continue; }
                const int32_t dz = (int32_t)(uint16_t)(z - pz);
                const bool still = dz <= (dot ? 0 : 1);
                const bool short_ = dot ? still : dz < dt - 2;
                const bool nextStill = i + 1 < n && sz[i + 1] >= z && (int32_t)(uint16_t)(sz[i + 1] - z) <= (dot ? 0 : 1);
                if (counting && short_ && (still || nextStill)) { put(1, (int32_t)st[i - 1] + dz * cpc100 / 100); counting = false; }
                else if (!counting && !still) { put(2, (int32_t)st[i] - dz * cpc100 / 100); counting = true; }
            }
        }
        lastT = read16(T2_COUNT);
    }
    printf("  %-34s T0_MODE %04x  %d lines  GPUSTAT %08x  (%d waits timed out)\n", name, mode, lines, (unsigned)read32(GP1), lone);
    clusters("reset", 0); clusters("stop", 1); clusters("resume", 2);
}

static void range(uint32_t x1, uint32_t x2) { write32(GP1, 0x06000000u | (x2 << 12) | x1); }

int main() {
    printf("\ntimer/t0-edges\n");
    printf("Timer 0's hblank-sync edges in a line - reset, stop, resume - in cycles after Timer 1's tick. No assertions.\n\n");
    const uint32_t st0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0100);
    printf("  found: GPUSTAT %08x\n", (unsigned)st0);
    write32(GP1, 0x03000000u);
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
    static const uint16_t modes[10] = { 0x0000, 0x0001, 0x0003, 0x0005, 0x0007, 0x0100, 0x0101, 0x0103, 0x0105, 0x0107 };
    static const char* const mn[10] = { "sysclk free", "sysclk 0 pause in hb", "sysclk 1 reset at hb", "sysclk 2 count in hb", "sysclk 3 wait, free",
                                        "dot free", "dot 0 pause in hb", "dot 1 reset at hb", "dot 2 count in hb", "dot 3 wait, free" };
    for (int w = 0; w < 2; w++) {
        const uint32_t gp = w ? 0x08000002u : 0x08000001u;      /* 512 (div 5) / 320 (div 8) */
        const int32_t dotc = w ? 5 * 700 / 11 : 8 * 700 / 11;   /* a dot in cycles x 100 */
        for (int m = 0; m < 10; m++) {
            char name[48]; snprintf(name, sizeof(name), "NTSC %s  %s", w ? "512" : "320", mn[m]);
            write32(GP1, gp); range(0x260, 0xC60);
            measure(name, modes[m], (modes[m] & 0x100) ? dotc : 100);
        }
    }
    const uint32_t gp08 = ((st0 >> 17) & 3) | (((st0 >> 19) & 1) << 2) | (((st0 >> 20) & 1) << 3) | (((st0 >> 21) & 1) << 4) |
                          (((st0 >> 22) & 1) << 5) | (((st0 >> 16) & 1) << 6) | (((st0 >> 14) & 1) << 7);
    range(0x260, 0xC60);
    write32(GP1, 0x08000000u | gp08);
    write32(GP1, 0x03000000u | ((st0 >> 23) & 1));
    write16(T0_MODE, m0);
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t0-edges: Done.\n");
    for (;;) VSync(0);
}
