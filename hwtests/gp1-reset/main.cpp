/*
 * gpu/gp1-reset - does GP1(00h) restart the video timing?
 *
 * WHY (2026-10-30). psx-spx's dev text (commit 11e277d, "measured on the farm consoles": SCPH-1000, DTL-H2700, 1001, two
 * 5501, 7001, 9002) says GP1(00h) sends the scan to the line before line 0: the next VBlank IRQ0 comes Y2+1 lines after the
 * write wherever it fell (257 with the reset's Y2), a write in the display raises IRQ0 at once (within 50 cycles), a write
 * in VBlank raises none, and GPUSTAT.31 reads 0 and toggles again 18 lines later. This core's GP1(00h) resets registers
 * only, and every disc writes it: the BIOS twice before the first frame, hwtests/runner before every test. If it is so, the
 * boot's whole VBlank grid is 13-250 lines off here. It names none of our models but the 5501, the 7001 and a 1001 of the
 * new GPU; the old GPU's 1001 and the C3 boards are the question.
 *
 * WHAT IT DOES, interrupts off, I_STAT.0 polled, Timer 1 counting hblanks, Timer 2 the system clock:
 *   PHASE  a reset, two VBlank edges, d lines more, GP1(00h). From the write: cycles and lines to the next two VBlank edges,
 *          GPUSTAT.31 either side of it, and the cycles of its first three flips. d runs through the blanking and into the
 *          display (reset range 16..256, so the display starts about 23 lines after the edge), two trials each.
 *   HORIZ  whether the horizontal position restarts: a Timer 1 tick, `phase` cycles, GP1(00h), then the cycles to the next
 *          tick. Without a restart the two parts add up to a line (2,152.5); a control row with no write beside each.
 * Every wait is bounded.
 *
 * READ IT LIKE THIS. No restart (this core): the first edge is wherever the old frame's was. psx-spx's rule: 257 lines
 * from every write in the blanking, about 0 cycles and then 257 lines in the display.
 */
#include <common.h>
#include <psxapi.h>
#include <gpu.h>
#include <io.h>
#include <timer.h>

#define I_STAT 0x1F801070
#define I_MASK 0x1F801074
#define GPUSTAT 0x1F801814
#define T2_COUNT 0x1F801120
#define LIMIT 1300000                                 /* cycles: most of 3 frames, every wait stops here */

static uint32_t hi;
static uint16_t lo;
/* Cycles, Timer 2 and its wraps; every loop here reads it far more often than once in 65,536 cycles. */
static inline uint32_t now() {
    uint16_t t = read16(T2_COUNT);
    if (t < lo) hi += 0x10000;
    lo = t;
    return hi + t;
}
static inline void ackVblank() { write32(I_STAT, ~1u); }
static inline int vblankEdge() { return (read32(I_STAT) & 1) != 0; }

static int waitEdge() {
    ackVblank();
    uint32_t t0 = now();
    while (!vblankEdge()) if (now() - t0 > LIMIT) return 0;
    resetTimer(1);
    ackVblank();
    return 1;
}

static void waitLines(uint32_t d) {
    uint32_t t0 = now();
    while (readTimer(1) < d) if (now() - t0 > LIMIT) return;
}

static void phaseRow(uint32_t d) {
    writeGP1(0, 0);
    if (!waitEdge() || !waitEdge()) { printf("d=%3d  NO VBLANK before the write\n", d); return; }
    waitLines(d);
    ackVblank();
    uint32_t before = read32(GPUSTAT) >> 31;
    writeGP1(0, 0);
    uint32_t t0 = now();
    uint32_t l0 = readTimer(1);
    uint32_t after = read32(GPUSTAT) >> 31, last = after;
    uint32_t e1 = 0, e2 = 0, l1 = 0, l2 = 0, fl[3] = { 0, 0, 0 };
    int flips = 0;
    for (;;) {
        uint32_t t = now();
        if (t - t0 > LIMIT) break;
        if (vblankEdge()) {
            ackVblank();
            if (!e1) { e1 = t - t0 + 1; l1 = readTimer(1) - l0; }
            else { e2 = t - t0 + 1; l2 = readTimer(1) - l0; break; }
        }
        uint32_t b = read32(GPUSTAT) >> 31;
        if (b != last) { if (flips < 3) fl[flips] = t - t0; flips++; last = b; }
    }
    printf("d=%3d  edge %7d cy %3d lines  next %7d cy %3d lines  bit31 %d->%d flips %d %d %d\n",
           d, e1, l1, e2, l2, before, after, fl[0], fl[1], fl[2]);
}

static void horizRow(uint32_t phase, int doWrite) {
    writeGP1(0, 0);
    if (!waitEdge()) { printf("no VBlank\n"); return; }
    waitLines(100);
    uint16_t l = readTimer(1);
    uint32_t t0 = now();
    while (readTimer(1) == l) if (now() - t0 > 20000) break;
    t0 = now();
    while (now() - t0 < phase) { }
    uint32_t w0 = now() - t0;
    if (doWrite) writeGP1(0, 0);
    l = readTimer(1);
    uint32_t t1 = now();
    while (readTimer(1) == l) if (now() - t1 > 20000) break;
    uint32_t tick = now() - t1;
    printf("%s phase %4d: tick to write %4d, write to next tick %5d, sum %5d\n", doWrite ? "RESET  " : "CONTROL", phase, w0, tick, w0 + tick);
}

int main() {
    printf("\ngpu/gp1-reset\n");
    printf("GPUSTAT at entry %08x\n", read32(GPUSTAT));
    EnterCriticalSection();
    uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    uint16_t prev1 = initTimer(1, 1);                 /* hblanks */
    uint16_t prev2 = initTimer(2, 0);                 /* the system clock */
    lo = readTimer(2);
    resetTimer(1);

    static const uint16_t ds[] = { 0, 1, 2, 3, 5, 8, 12, 16, 20, 22, 23, 24, 25, 26, 28, 32, 40, 60, 80, 120, 160, 200, 230, 250, 256, 260, 262 };
    for (uint32_t k = 0; k < sizeof(ds) / sizeof(ds[0]); k++) {
        phaseRow(ds[k]);
        phaseRow(ds[k]);
    }
    static const uint16_t ps[] = { 200, 700, 1200, 1700, 2000 };
    for (uint32_t k = 0; k < sizeof(ps) / sizeof(ps[0]); k++) {
        horizRow(ps[k], 0);
        horizRow(ps[k], 1);
    }
    writeGP1(0, 0);
    restoreTimer(1, prev1);
    restoreTimer(2, prev2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
