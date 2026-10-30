/*
 * timer/t1-ticks - Timer 1's hblank tick, dated from the VBlank IRQ, with sync off and on (2026-10-08, after t1-line).
 *
 * WHY. On the SCPH-5501 three measurements disagree unless something depends on the sync bit. With Timer 1 counting
 * hblanks and sync OFF (MODE 0100h) its tick comes ~80 cycles earlier against the IRQ and against its own v-blank
 * reset than this core's (t1-edges; runner v5's vernier 80.7, the 7501 and 9001 94); GPUSTAT bit 31's toggle sits
 * where this core's does against the reset (t1-line, within 4 cycles). But gpustat31, which counts hblanks with sync ON
 * (mode 1, reset at v-blank), found the tick where this core puts it against bit 31 - ~198 GPU ticks before the line, on
 * three consoles. If a synchronised counter takes its tick later, all three agree.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For each of MODE 0100h (sync off), 0103h (sync mode 1:
 * reset at v-blank) and 0107h (sync mode 3: free once a v-blank has passed): the VBlank edge polled, then Timer 1 read
 * with Timer 2 for 30 changes, each dated by the read that saw it and the one before, from the IRQ. 32 frames each. A
 * reset (mode 1's, ~25 lines in) is a change too, flagged.
 *
 * READ IT LIKE THIS. Per mode, change k's tightest bracket over the frames (latest read-before, earliest read-after).
 * The modes the same here (this core has one tick); on a console sync on ~80 later than sync off says the synchroniser.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-ticks --ntsc --frames=900`.
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

#define FRAMES 32
#define CH 30
static int32_t lo[3][CH], hi[3][CH];
static uint8_t resetAt[3][CH];

int main() {
    printf("\ntimer/t1-ticks\n");
    printf("Timer 1's hblank tick from the polled VBlank IRQ, sync off and on: change k's tightest bracket over %d frames, cycles. No assertions.\n\n", FRAMES);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    static const uint16_t MODES[3] = { 0x0100, 0x0103, 0x0107 };
    for (int m = 0; m < 3; m++) {
        for (int k = 0; k < CH; k++) { lo[m][k] = -0x7FFFFFFF; hi[m][k] = 0x7FFFFFFF; resetAt[m][k] = 0; }
        write16(T1_MODE, MODES[m]);
        for (int f = -1; f < FRAMES; f++) {
            write32(I_STAT, ~1u);
            const uint32_t w0 = now();
            while (!(read32(I_STAT) & 1)) if (now() - w0 > 1200000u) break;
            const uint32_t irq = now();
            write32(I_STAT, ~1u);
            if (f < 0) continue;
            uint16_t prev = read16(T1_COUNT);
            uint32_t before = now();
            for (int k = 0; k < CH;) {
                const uint16_t v = read16(T1_COUNT);
                const uint32_t t = now();
                if (v != prev) {
                    const int32_t a = (int32_t)(before - irq), b = (int32_t)(t - irq);
                    if (a > lo[m][k]) lo[m][k] = a;
                    if (b < hi[m][k]) hi[m][k] = b;
                    if (v < prev) resetAt[m][k] = 1;
                    k++; prev = v;
                }
                before = t;
                if (t - irq > 90000u) break;
            }
        }
    }
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  k    sync off (0100h)      sync mode 1 (0103h)      sync mode 3 (0107h)\n");
    for (int k = 0; k < CH; k++) {
        printf("  %2d", k);
        for (int m = 0; m < 3; m++) printf("   %6d..%-6d%s", (int)lo[m][k], (int)hi[m][k], resetAt[m][k] ? "R" : " ");
        printf("\n");
    }
    printf("\ntimer/t1-ticks: Done.\n");
    for (;;) VSync(0);
}
