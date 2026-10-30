/*
 * gpu/gpu-abort - GP1(01h) while a primitive is still drawing: how much of it
 * reaches VRAM, and when does GPUSTAT say the GPU is ready again?
 *
 * WHY (2026-09-29; an AI's audit). Sony's
 * LibOver (Basic Graphics Library, "Synchronization and Reset"): every
 * ResetGraph level "immediately interrupt[s] the drawing command in progress,
 * cancel[s] all the requests remaining in the queue, and enter[s] wait
 * status", and level 1 - GP1(01h) - "is used frequently when switching the
 * double buffer". psx-spx GP1(01h): "aborts the current rendering command (eg.
 * this may end up with an incompletely drawn triangle)". Nobody says WHERE a
 * primitive stops, and no emulator stops one:
 *   DuckStation (gpu.cpp, GP1 ClearFIFO): the primitive was drawn whole when
 *     it was submitted; the reset drops its remaining ticks, so GPUSTAT reads
 *     idle at once.
 *   mednafen (psx/gpu.cpp, GP1 0x01): drawn whole as well; DrawTimeAvail's
 *     debt is cleared, idle at once.
 *   MiSTer (rtl/gpu.vhd): the FIFO and the VRAM transfer units are reset, the
 *     drawing units are not ("todo: must reset drawing units to idle? -> ridge
 *     racer triggers that in the middle of a rectangle command clearing the
 *     screen") - drawn whole, and busy until done.
 *   This core (src/psx.js GPU.writeGP1, abortFill): a FILL is taken back row
 *     by row - amidog's FIFO > OVERFLOW model, a 1024x256 fill reset 12 cycles
 *     after a burst, keeps rows 256-260 - and everything else was drawn whole
 *     at dispatch. Its busy time ran on after the reset until M267 and ends
 *     there since (Sony's "enter wait status", DuckStation, mednafen).
 * branchaudit's gpu-reset01-mid-draw found games doing it (the Arcade's
 * Greatest Hits collections, Bombing Islands, Blaster Master, Blazing
 * Dragons), so the answer changes pictures somewhere.
 *
 * WHAT IT DOES. Interrupts off. For each primitive and each delay N:
 *   1. GP1(01h); a GP0(02h) fill of the 256x256 area at (512,256) with a
 *      sentinel colour; wait for GPUSTAT.26 (idle).
 *   2. The primitive's words straight into GP0, the last word starting Timer 2
 *      (system clock / 8) at 0.
 *   3. Spin until Timer 2 reads N/8, write GP1(01h), read GPUSTAT at once
 *      (bits 26/27/28), then time how long until GPUSTAT.26 reads 1.
 *   4. Wait ~50,000 cycles, read the area back with GP0(C0h) row by row, and
 *      count what is not the sentinel: rows drawn whole, the row it stopped in
 *      and how far along it, pixels in all.
 * The primitives, each covering the area:
 *   FILL   GP0(02h) 256x256 - the control whose abort amidog's test measured
 *   RECT   GP0(60h) mono rectangle 256x256
 *   TRI    GP0(20h) mono triangle (512,256) (767,256) (512,511) - 32,640 px
 *          whole (the rasteriser leaves out the right and bottom edges)
 *   GTRI   GP0(30h) shaded triangle, the same three vertices
 * CONTROLS: N = 400,000 must read every primitive whole (none takes that
 * long), and a case with no reset at all (N "none") the same.
 *
 * READ IT LIKE THIS. "pixels" climbing with N and "ready" a few cycles after
 * the reset: a console stops the primitive where the reset finds it (the
 * row/partial columns show its raster order and rate). "pixels" whole at every
 * N: a console finishes it, as every emulator does - then "ready" says whether
 * GPUSTAT waits for it (MiSTer) or not (DuckStation, mednafen).
 *
 * There are no assertions - compare against your emulator.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js gpu-abort --frames=4000`):
 *   FILL  taken back row by row (abortFill): 0 px at N 0, 2 rows at 64, 44 at
 *         1,024, 179 at 4,096, whole from 8,192 (a row ~22.8 cycles); bits 101
 *         right after the reset, ready +16..32.
 *   RECT, TRI, GTRI  whole (65,536 / 32,640 px) at every N; bits 101 right
 *         after the reset, ready +16..24 - idle at once, as DuckStation and
 *         mednafen (M267).
 *   N none and 400,000: whole, as they must be.
 * AT v0.266 RECT, TRI and GTRI read bits 001 after the reset and ready only
 * when the busy model said the primitive ended (RECT +35,256 at N 0 and
 * +2,480 at 32,768; TRI / GTRI +17,728 / +17,880 at N 0), as MiSTer would.
 * A console that stops the primitive reads pixels climbing with N, the way
 * FILL's do. One that finishes it reads them whole, and its "ready" column
 * then separates v0.266 (busy to the end) from v0.267 (idle at once).
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>

#define GP0R    (*(volatile uint32_t*)0x1F801810)
#define GPUREADR (*(volatile uint32_t*)0x1F801810)
#define GPUSTAT (*(volatile uint32_t*)0x1F801814)
#define GP1R    (*(volatile uint32_t*)0x1F801814)
#define T2VAL   (*(volatile uint16_t*)0x1F801120)
#define T2MODE  (*(volatile uint16_t*)0x1F801124)

#define AX 512
#define AY 256
#define AW 256
#define AH 256
#define SENTINEL 0x7C1Fu        /* magenta, r31 g0 b31, mask bit clear */

static uint32_t bits(uint32_t s) { return ((s >> 26) & 1) * 100 + ((s >> 27) & 1) * 10 + ((s >> 28) & 1); }

static void waitIdle() {
    for (int guard = 0; guard < 2000000; guard++) if (GPUSTAT & (1u << 26)) return;
}

static void settle(int n) { for (volatile int i = 0; i < n; i++) { } }

/* The whole VRAM drawable, offset 0, no dither, no mask. */
static void setupEnv() {
    GP0R = 0xE1000000 | (1u << 10);                 /* draw mode: drawing to display area allowed, no dither */
    GP0R = 0xE3000000;                              /* area top-left 0,0 */
    GP0R = 0xE4000000 | (511u << 10) | 1023u;       /* area bottom-right 1023,511 */
    GP0R = 0xE5000000;                              /* offset 0,0 */
    GP0R = 0xE6000000;                              /* mask: neither set nor check */
    waitIdle();
}

static void clearArea() {
    GP1R = 0x01000000;
    settle(100);
    /* GP0(02h) colour is 24-bit BGR; sentinel magenta = r 255, g 0, b 255 */
    GP0R = 0x02FF00FF;
    GP0R = ((uint32_t)AY << 16) | AX;
    GP0R = ((uint32_t)AH << 16) | AW;
    waitIdle();
}

enum Prim { P_FILL, P_RECT, P_TRI, P_GTRI };
static const char* NAMES[] = { "FILL", "RECT", "TRI ", "GTRI" };

/* Everything but the primitive's LAST word; the caller writes that and starts the timer. */
static uint32_t sendPrimButLast(Prim p) {
    switch (p) {
        case P_FILL: GP0R = 0x0200FF00; GP0R = ((uint32_t)AY << 16) | AX;                    return ((uint32_t)AH << 16) | AW;
        case P_RECT: GP0R = 0x6000FF00; GP0R = ((uint32_t)AY << 16) | AX;                    return ((uint32_t)AH << 16) | AW;
        case P_TRI:  GP0R = 0x2000FF00; GP0R = ((uint32_t)AY << 16) | AX;
                   GP0R = ((uint32_t)AY << 16) | (AX + AW - 1);                            return ((uint32_t)(AY + AH - 1) << 16) | AX;
        case P_GTRI: GP0R = 0x300000FF; GP0R = ((uint32_t)AY << 16) | AX;
                   GP0R = 0x0000FF00; GP0R = ((uint32_t)AY << 16) | (AX + AW - 1);
                   GP0R = 0x00FF0000;                                                       return ((uint32_t)(AY + AH - 1) << 16) | AX;
    }
    return 0;
}

struct Readback { uint32_t pixels, fullRows, firstRow, lastRow, partialRow, partialPx, firstCol, lastCol; };

static Readback readArea() {
    Readback r = { 0, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0 };
    GP1R = 0x04000003;                              /* DMA direction VRAM->CPU (for GPUSTAT.27 only; read by CPU) */
    for (uint32_t y = 0; y < AH; y++) {
        waitIdle();
        GP0R = 0xC0000000;
        GP0R = ((AY + y) << 16) | AX;
        GP0R = (1u << 16) | AW;
        for (int guard = 0; guard < 100000 && !(GPUSTAT & (1u << 27)); guard++) { }
        uint32_t n = 0;
        for (uint32_t x = 0; x < AW; x += 2) {
            uint32_t w = GPUREADR;
            uint32_t a = w & 0xFFFF, b = w >> 16;
            if ((a & 0x7FFF) != SENTINEL) { n++; if (x < r.firstCol) r.firstCol = x; if (x > r.lastCol || r.lastCol == 0) r.lastCol = x; }
            if ((b & 0x7FFF) != SENTINEL) { n++; if (x + 1 < r.firstCol) r.firstCol = x + 1; if (x + 1 > r.lastCol) r.lastCol = x + 1; }
        }
        r.pixels += n;
        if (n) { if (y < r.firstRow) r.firstRow = y; r.lastRow = y; }
        if (n == AW) r.fullRows++;
        else if (n && r.partialRow == 0xFFFF) { r.partialRow = y; r.partialPx = n; }
    }
    GP1R = 0x04000000;
    return r;
}

static void runCase(Prim p, int32_t n) {
    setupEnv();
    clearArea();
    uint32_t last = sendPrimButLast(p);
    T2MODE = 2u << 8;                               /* clock source: system clock / 8, free running */
    GP0R = last;                                    /* the primitive starts */
    T2VAL = 0;
    uint32_t s0 = 0, readyAt = 0;
    if (n >= 0) {
        const uint32_t target = (uint32_t)n / 8;
        while (T2VAL < target) { }
        GP1R = 0x01000000;                          /* GP1(01h) */
        uint32_t t0 = T2VAL;
        s0 = GPUSTAT;
        uint32_t t = t0;
        for (int guard = 0; guard < 400000; guard++) { if (GPUSTAT & (1u << 26)) { t = T2VAL; break; } }
        readyAt = (t - t0) * 8;
    }
    waitIdle();
    settle(20000);
    Readback r = readArea();
    if (n >= 0) printf("  %s N %6ld", NAMES[p], (long)n);
    else        printf("  %s N   none", NAMES[p]);
    printf(" | px %5lu  rows %3lu..%3lu (%3lu whole)", (unsigned long)r.pixels,
           (unsigned long)(r.firstRow == 0xFFFF ? 0 : r.firstRow), (unsigned long)r.lastRow, (unsigned long)r.fullRows);
    if (r.partialRow != 0xFFFF) printf("  first part-row %3lu: %3lu px", (unsigned long)r.partialRow, (unsigned long)r.partialPx);
    if (n >= 0) printf(" | after reset %03lu, ready +%lu", (unsigned long)bits(s0), (unsigned long)readyAt);
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-abort\n");
    printf("GP1(01h) N cycles after a primitive's last word; the 256x256 area at (512,256) read back.\n");
    printf("px = pixels drawn; rows = first..last drawn row; bits 26/27/28 right after the reset; ready = cycles until GPUSTAT.26.\n");
    printf("TRI/GTRI cover 32,640 px when whole, FILL/RECT 65,536. No assertions.\n\n");
    EnterCriticalSection();
    static const int32_t DELAYS[] = { 0, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 400000, -1 };
    for (int p = P_FILL; p <= P_GTRI; p++) {
        for (unsigned i = 0; i < sizeof(DELAYS) / sizeof(DELAYS[0]); i++) runCase((Prim)p, DELAYS[i]);
        printf("\n");
    }
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) VSync(0);
}
