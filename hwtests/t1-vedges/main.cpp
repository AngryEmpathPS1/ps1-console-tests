/*
 * timer/t1-vedges - every edge Timer 1's v-blank sync makes, on the system clock: where the count stops, resumes and resets, in
 * cycles after the v-blank IRQ (I_STAT bit 0) (2026-11-12, main session).
 *
 * WHY. hwtests/t0-edges placed Timer 0's hblank edges to a cycle (v0.377.327). Timer 1's v-blank edges are known by the LINE
 * (timer-sync's VBLANK rows, T1_VBLANK; T1_HRESET1's early reset at the last line's hblank end, v0.377.325) but not inside it:
 * timer1-vsync's system-clock rows on the second SCPH-5501 put mode 0's resume 78 cycles into a line where this core reads
 * 91-119 by the lock phase, and mode 2's last v-blank count 23 cycles longer. This places each edge from many frames.
 * hwtests/t1-edges (2026-10-08) already dated sync mode 1's system-clock reset against the IRQ (v0.377.50); this adds the
 * pause's edges (mode 0), mode 2's and mode 3's.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock as the clock. Per mode, 64 frames: a polled v-blank IRQ, a
 * dithered wait to 255 lines on, then Timer 1, Timer 2 and I_STAT polled in a tight loop to the next IRQ (its read is the
 * frame's reference) and the two timers alone to 62,000 cycles past it, and each step between two samples (dt on Timer 2, dz on Timer 1) is classed as in t0-edges - a reset
 * (the count went down: at the step's end - z), a stop (counted less than its time, then still: at its start + dz), a resume
 * (counting after still: at its end - dz). Mode 3 is written again 20 lines before each v-blank. Each kind's places, in
 * cycles after the IRQ, sorted and split where 64 cycles separate them: n, mean, median, min..max. Cases: Timer 1 at 0000h
 * (free, the control) and 0001h / 0003h / 0005h / 0007h, NTSC 320 at the display range the runner leaves.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-vedges --ntsc --frames=3000` (PREDICTED.txt).
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

#define FRAMES 64
#define NE 1000
static int32_t ev[3][NE]; static int nev[3];
static uint32_t rng = 1;
static volatile uint16_t* const t1 = (volatile uint16_t*)T1_COUNT;
static volatile uint16_t* const t2 = (volatile uint16_t*)T2_COUNT;
static volatile uint32_t* const ist = (volatile uint32_t*)I_STAT;

static void clusters(const char* what, int k) {
    int32_t* v = ev[k]; const int n = nev[k];
    printf("    %-7s", what);
    if (!n) { printf(" none\n"); return; }
    isort(v, n);
    for (int lo = 0; lo < n;) {
        int hi = lo + 1; while (hi < n && v[hi] - v[hi - 1] <= 64) hi++;
        int32_t sum = 0; for (int i = lo; i < hi; i++) sum += v[i] - v[lo];   /* about v[lo]: the sum fits */
        const int32_t m = v[lo] * 100 + sum * 100 / (hi - lo);
        const int32_t am = m < 0 ? -m : m;
        printf(" [%d: mean %s%d.%02d, median %d, %d..%d]", hi - lo, m < 0 ? "-" : "", (int)(am / 100), (int)(am % 100), (int)v[lo + (hi - lo) / 2], (int)v[lo], (int)v[hi - 1]);
        lo = hi;
    }
    printf("\n");
}

/* Events kept as raw Timer 2 reads (16 bits) and whether they came after the frame's IRQ, then made relative to it. */
static uint16_t fe[3][16]; static uint8_t fa[3][16]; static int nfe[3];
static void keep(int k, uint16_t at, int after) { if (nfe[k] < 16) { fe[k][nfe[k]] = at; fa[k][nfe[k]] = (uint8_t)after; nfe[k]++; } }

static void measure(const char* name, uint16_t mode) {
    nev[0] = nev[1] = nev[2] = 0;
    write16(T1_MODE, mode);
    uint32_t irq; int lost = 0, frames = 0;
    for (int i = 0; i < 2; i++) edge(&irq);
    for (int f = 0; f < FRAMES; f++) {
        if (!edge(&irq)) { printf("  %-24s no VBlank IRQ\n", name); return; }
        rng = rng * 1103515245u + 12345u;
        const uint32_t w = 243u * 2172u + ((rng >> 16) & 63);
        while (now() - irq < w) {}
        if ((mode & 7) == 7) write16(T1_MODE, mode);             /* mode 3: armed again before each v-blank */
        while (now() - irq < 255u * 2172u) {}
        nfe[0] = nfe[1] = nfe[2] = 0;
        write32(I_STAT, ~1u);
        uint16_t pz = *t1, pt = *t2, stopAt = 0, tirq = 0;
        bool counting = true, pendStop = false;
        int after = 0;
        /* one step of the classifier (t0-edges' rule): a reset, a stop (short, then still), a resume */
        auto step = [&](uint16_t z, uint16_t t) {
            const int32_t dt = (int32_t)(uint16_t)(t - pt);
            if (z < pz) { if (pz < 0xF000u) keep(0, (uint16_t)(t - z), after); counting = true; pendStop = false; }
            else {
                const int32_t dz = (int32_t)(uint16_t)(z - pz);
                const bool still = dz <= 1;
                if (pendStop) { if (still) { keep(1, stopAt, after); counting = false; } pendStop = false; }
                if (counting && !still && dz < dt - 2) { pendStop = true; stopAt = (uint16_t)(pt + dz); }
                else if (counting && still) { keep(1, (uint16_t)(pt + dz), after); counting = false; }
                else if (!counting && !still) { keep(2, (uint16_t)(t - dz), after); counting = true; }
            }
            pz = z; pt = t;
        };
        /* phase A: Timer 1, Timer 2 and I_STAT until the IRQ (its read dates the frame); phase B: the two timers alone, to
         * 62,000 cycles past it (the 16-bit offsets from the IRQ's read stay unambiguous) */
        uint32_t guard = 0; bool got = false;
        for (;;) {
            const uint16_t z = *t1; const uint16_t t = *t2; const uint32_t st = *ist;
            step(z, t);
            if (st & 1) { tirq = t; got = true; break; }
            if (++guard > 400000u) break;
        }
        after = 1;
        if (got) for (;;) {
            const uint16_t z = *t1; const uint16_t t = *t2;
            step(z, t);
            if ((uint16_t)(t - tirq) > 62000u) break;
        }
        lastT = read16(T2_COUNT);                                  /* now()'s clock: it missed a wrap or so, nothing times across it */
        if (!got) { lost++; continue; }
        frames++;
        for (int k = 0; k < 3; k++) for (int i = 0; i < nfe[k]; i++) if (nev[k] < NE)
            { const uint16_t u = (uint16_t)(fe[k][i] - tirq);          /* phase B reaches 62,000: above 63,000 is just before the IRQ */
              ev[k][nev[k]++] = (fa[k][i] && u <= 63000u) ? (int32_t)u : -(int32_t)(uint16_t)(tirq - fe[k][i]); }
    }
    printf("  %-24s T1_MODE %04x  %d frames  (%d without an IRQ)\n", name, mode, frames, lost);
    clusters("reset", 0); clusters("stop", 1); clusters("resume", 2);
}

int main() {
    printf("\ntimer/t1-vedges\n");
    printf("Timer 1's v-blank-sync edges on the system clock - reset, stop, resume - in cycles after the v-blank IRQ. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    printf("  GPUSTAT %08x\n", (unsigned)read32(GP1));
    measure("sysclk free", 0x0000);
    measure("sysclk 0 pause in vb", 0x0001);
    measure("sysclk 1 reset at vb", 0x0003);
    measure("sysclk 2 count in vb", 0x0005);
    measure("sysclk 3 wait, free", 0x0007);
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t1-vedges: Done.\n");
    for (;;) VSync(0);
}
