/*
 * sio/justifier-irq - when does a Justifier pull IRQ10, relative to where it
 * is aimed?
 *
 * WHY (ROAD B, the controller memo's 14.3 and 5.3; M221). Konami's Justifier
 * (and Hyper Blaster) reports no position: it pulls /IRQ10 as the beam
 * crosses what its sensor sees, one edge per line, while the host's 4th poll
 * byte had bit 4 set, and the game reads Timer 0 (system clock, reset at
 * hblank) and Timer 1 (lines) in its handler (Sony's LibOver 13-14). WHICH
 * lines and how far into them is game-tuned in every emulator and measured in
 * none (the memo's table 5.3): DuckStation -12..-6 lines at +50 video ticks
 * (this emulator's JUSTIFIER_F/G/D), the MiSTer core -13..-7 at ~+80,
 * ReARMed -12..-9 at ~+105 sysclk (tuned on Die Hard Trilogy), mednafen -6..+6
 * at ~-245. How many lines the sensor sees is its field of view.
 *
 * WHAT IT DOES. 320x240 NTSC (display ranges set and printed). Interrupts
 * off; Timer 0 at the system clock reset at every hblank (libgun's 0043h
 * without its IRQ), Timer 1 counting hblanks reset at vblank (the line). Each
 * frame, right after VBlank (I_STAT bit 0), the gun is polled by hand on port
 * 1 with the enable (01 42 00 10 00, as Sony's GUN.EXE sends), and every
 * IRQ10 until the next VBlank is taken from I_STAT bit 10: Timer 1, then
 * Timer 0, then the bit acknowledged. The same five targets and two screens
 * as guncon-xy (a white screen; a white band of 8 lines on black around the
 * target's line), each marked by two horizontal arms only: a black mark
 * above the aim would hide the lines a gun fires on. A person aims and holds
 * the trigger, 30 frames each. After the first, a CONTROL: 30 frames polled
 * WITHOUT the enable bit, still aimed.
 *
 * READ IT LIKE THIS. For each target: IRQ10s a frame (least..most), the
 * line (Timer 1) of the frame's first and last, and Timer 0 at the first (cycles
 * after that line's hblank; x1.585 for video ticks at 320 wide). The target's
 * own line is 16 + py counted from VSYNC; Timer 1 counts from the vblank's
 * start, so read the lines against the (160,120) row. WHITE gives the whole
 * window the gun fires on; BAND shows which of those lines it fires on only
 * when they are lit - a window that never meets the band draws no IRQ10 there.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js justifier-irq --justifier
 * --quiet=2500`, the emulated gun aimed where each prompt says):
 *   CONTROL: no IRQ10. WHITE, every target: 7 IRQ10s a frame on 7 lines -
 *   Timer 1 109..115 at py 120, 29..35 at 40, 189..195 at 200 (Timer 1 =
 *   the GPU's line - 15) - with Timer 0 at the first 874..890 at px 160,
 *   263..279 at 40, 1485..1501 at 280: 5.1 cycles a pixel (8 video ticks).
 *   BAND: no IRQ10 at all.
 * That is JUSTIFIER_F..G = -12..-6 at +50 ticks exactly: the band's lines
 * (py - 4 .. py + 3) are not in that window. A console's WHITE rows give its
 * window; its BAND rows say how many lines around the aim the sensor sees.
 * The first version drew a full cross, whose upper arm lay on those seven
 * lines at the aim's column: 0 of 7 lit, and no IRQ10 anywhere.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <io.h>
#include <gpu.h>

#define JOY_DATA 0x1F801040
#define JOY_STAT 0x1F801044
#define JOY_MODE 0x1F801048
#define JOY_CTRL 0x1F80104A
#define JOY_BAUD 0x1F80104E
#define I_STAT 0x1F801070
#define T0_COUNT 0x1F801100
#define T0_MODE  0x1F801104
#define T1_COUNT 0x1F801110
#define T1_MODE  0x1F801114

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) {} }

/* One 5-byte Justifier poll on port 1 (4th byte `enable`); the buttons' second byte, or -1. */
static int poll(uint8_t enable) {
    const uint8_t tx[5] = { 0x01, 0x42, 0x00, enable, 0x00 };
    uint8_t rx[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(2000);
    write16(JOY_CTRL, 0x0003);
    wait(1500);
    for (int b = 0; b < 5; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        bool acked = false;
        while ((uint16_t)(t2() - t0) < 4000) { const uint32_t st = read32(JOY_STAT); if (st & 0x80) acked = true; if ((st & 2) && (acked || b == 4)) break; }
        while (!(read32(JOY_STAT) & 2) && (uint16_t)(t2() - t0) < 4000) {}
        rx[b] = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : 0xFF;
        if (!acked && b < 4) { write16(JOY_CTRL, 0x0000); return -1; }
    }
    write16(JOY_CTRL, 0x0000);
    return rx[4];
}

/* Wait for VBlank's start, then clear the flags of anything before it. */
static void waitVBlank() {
    write16(I_STAT, (uint16_t)~1);
    while (!(read16(I_STAT) & 1)) {}
    write16(I_STAT, (uint16_t)~0x0401);
}

struct Frame { int n, firstLine, lastLine, firstT0; };
/* Every IRQ10 from now to the next VBlank. */
static Frame collect() {
    Frame f = { 0, -1, -1, -1 };
    for (;;) {
        const uint16_t st = read16(I_STAT);
        if (st & 0x400) {
            const int line = read16(T1_COUNT), t0 = read16(T0_COUNT);
            write16(I_STAT, (uint16_t)~0x400);
            if (f.n == 0) { f.firstLine = line; f.firstT0 = t0; }
            f.lastLine = line;
            f.n++;
        }
        if (st & 1) break;
    }
    return f;
}

static void gp0(uint32_t v) { write32(0x1F801810, v); }
static void gpuReady() { while (!(read32(0x1F801814) & 0x04000000)) {} }
static uint32_t bgr(int r, int g, int b) { return (uint32_t)(b << 16) | (g << 8) | r; }
static void fill(int x, int y, int w, int h, int r, int g, int b) { gpuReady(); gp0(0x02000000 | bgr(r, g, b)); gp0((y << 16) | x); gp0((h << 16) | w); }
static void tile(int x, int y, int w, int h, int r, int g, int b) { gpuReady(); gp0(0x60000000 | bgr(r, g, b)); gp0((y << 16) | x); gp0((h << 16) | w); }
/* The mark: two horizontal arms either side of the aim point, and nothing above or below it - the gun fires on
 * lines ABOVE its aim (every model has it so), and a black arm there would hide them. */
static void cross(int x, int y, int r, int g, int b) {
    tile(x - 20, y - 1, 16, 2, r, g, b); tile(x + 5, y - 1, 16, 2, r, g, b);
}
static void screen(int x, int y, bool band) {
    if (!band) { fill(0, 0, 320, 240, 255, 255, 255); cross(x, y, 0, 0, 0); }
    else { fill(0, 0, 320, 240, 0, 0, 0); tile(0, y - 4, 320, 8, 255, 255, 255); cross(x, y, 0x60, 0, 0); }
}

struct Result { int frames, nMin, nMax, fMin, fMax, lMin, lMax, tMin, tMax; };
static void add(Result &R, const Frame &f) {
    R.frames++;
    if (f.n < R.nMin) R.nMin = f.n; if (f.n > R.nMax) R.nMax = f.n;
    if (!f.n) return;
    if (f.firstLine < R.fMin) R.fMin = f.firstLine; if (f.firstLine > R.fMax) R.fMax = f.firstLine;
    if (f.lastLine < R.lMin) R.lMin = f.lastLine; if (f.lastLine > R.lMax) R.lMax = f.lastLine;
    if (f.firstT0 < R.tMin) R.tMin = f.firstT0; if (f.firstT0 > R.tMax) R.tMax = f.firstT0;
}
static const int16_t targets[5][2] = { { 160, 120 }, { 40, 40 }, { 280, 40 }, { 40, 200 }, { 280, 200 } };
static Result res[5][2], control;

static void show(const char *name, int t, int s, const Result &R) {
    if (name) printf("%-20s", name);
    else printf("(%3d,%3d) %-5s     ", targets[t][0], targets[t][1], s ? "BAND" : "WHITE");
    printf(" %2d  %2d..%2d   ", R.frames, R.nMin, R.nMax);
    if (R.fMax < 0) { printf("none\n"); return; }
    printf("%3d..%3d   %3d..%3d   %4d..%4d\n", R.fMin, R.fMax, R.lMin, R.lMax, R.tMin, R.tMax);
}

int main() {
    printf("\nsio/justifier-irq\n");
    printf("A Justifier in port 1, aimed by a person at crosses on a CRT: every IRQ10 it pulls. No assertions.\n\n");
    initVideo(320, 240);
    const uint32_t h1 = 0x260, h2 = 0xC60, v1 = 0x10, v2 = 0x100;
    writeGP1(0x06, (h2 << 12) | h1);
    writeGP1(0x07, (v2 << 10) | v1);
    printf("GPUSTAT %08x; GP1(06h) X %03x..%03x, GP1(07h) Y %03x..%03x\n\n", (unsigned)ReadGPUstat(),
           (unsigned)h1, (unsigned)h2, (unsigned)v1, (unsigned)v2);
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE), m2 = read16(0x1F801124);
    write16(0x1F801124, 0);
    EnterCriticalSection();
    write16(T0_MODE, 0x0003);                 /* system clock, reset at hblank */
    write16(T1_MODE, 0x0103);                 /* hblanks, reset at vblank */
    const Result blank = { 0, 99, -1, 9999, -1, 9999, -1, 99999, -1 };

    control = blank;
    for (int t = 0; t < 5; t++)
        for (int s = 0; s < 2; s++) {
            const int x = targets[t][0], y = targets[t][1];
            screen(x, y, s == 1);
            Result &R = res[t][s];
            R = blank;
            for (int f = 0; f < 60 * 20; f++) { waitVBlank(); const int b = poll(0x10); if (b >= 0 && (b & 0x80)) break; }
            printf("aim at (%d, %d) on the %s screen and hold the trigger\n", x, y, s ? "BAND" : "WHITE");
            for (int f = 0; f < 60 * 20 && R.frames < 30; f++) {
                waitVBlank();
                const int b = poll(0x10);
                const Frame fr = collect();
                if (b >= 0 && !(b & 0x80)) add(R, fr);   /* trigger (bit 15) held */
            }
            /* the CONTROL, still aimed at the first target: the same frames polled without the enable */
            if (t == 0 && s == 0) for (int f = 0; f < 30; f++) { waitVBlank(); poll(0x00); add(control, collect()); }
            printf("release the trigger\n");
        }
    write16(T0_MODE, m0); write16(T1_MODE, m1);
    ExitCriticalSection();
    write16(0x1F801124, m2);

    printf("\n                   frames IRQ10s   first line  last line  Timer0 at the first\n");
    show("CONTROL (no enable)", 0, 0, control);
    for (int t = 0; t < 5; t++)
        for (int s = 0; s < 2; s++) show(0, t, s, res[t][s]);
    printf("\nsio/justifier-irq: Done.\n");
    for (;;) VSync(0);
}
