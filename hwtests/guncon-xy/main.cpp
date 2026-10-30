/*
 * sio/guncon-xy - what a GunCon reports for known points of the picture,
 * and whether it latches the first or the last line it sees.
 *
 * WHY (ROAD B, the controller memo's 14.2; M221). A GunCon reports X as 8 MHz
 * clocks since HSYNC and Y as lines since VSYNC (psx-spx), and what offsets
 * turn a pixel into those numbers is measured nowhere: this emulator takes
 * DuckStation's game-tuned K = -140 video ticks and KY = 0 (guncon.h;
 * "Fixes calibration in Ghoul Panic and default position in Judge Dredd",
 * 3a0c4898), mednafen has -150..-192 by width and reports the LAST bright
 * line of an 8-line window (+0..7), MiSTer 67 + 1.5 x GunX. The memo's
 * search list (4.3) found no console measurement.
 *
 * WHAT IT DOES. 320x240 NTSC, the display ranges set to GP1(06h) 260h..C60h
 * and GP1(07h) 10h..100h and printed. Interrupts off: VBlank found by I_STAT
 * bit 0, the gun read by hand on port 1 right after it (01 42 00 00 00 00 00
 * 00 00, as Point Blank sends). For five targets - (160,120), (40,40),
 * (280,40), (40,200), (280,200) - and two screens each:
 *   WHITE  the whole picture white, the target a cross whose four black
 *          arms stop 3 pixels short of its centre (the centre stays lit);
 *   BAND   the picture black but for a white band of 8 lines, the target's
 *          line the band's 5th, the cross's arms dark red (visible to a
 *          person, not light to a gun: psx-spx's thresholds).
 * A person aims at the cross and holds the trigger; the gun is read every
 * frame until 30 readings with the trigger held and a position (not the
 * "no light" 0001h/000Ah) are in, then "release the trigger". Each target's
 * readings: how many, X and Y from least to most, and their means.
 *
 * READ IT LIKE THIS. Under the model X = (video ticks from the line origin
 * + K) x 8 MHz / the video clock, Y = the line + KY: at 320 wide one pixel is
 * 8 ticks, 1.19 of X, and one line is 1 of Y. So X at a target minus the
 * model without K gives K; Y minus (16 + py) gives KY. A gun that latches
 * the FIRST light it sees reads a smaller Y on WHITE than on BAND (it sees
 * the lines above the target), one that latches the LAST a larger one.
 * THE CRYSTAL: run.js's machine has the PAL video crystal (M108), so this
 * core's X here is ~0.9% above an NTSC console's; the console's own crystal
 * is in its X.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js guncon-xy --guncon
 * --quiet=2500`: run.js aims the emulated gun where each prompt says and
 * holds its trigger), 30 readings a target, every one the same:
 *   (160,120) X 263 Y 136; (40,40) 119 / 56; (280,40) 407 / 56;
 *   (40,200) 119 / 216; (280,200) 407 / 216 - WHITE and BAND alike.
 * So Y = 16 + py (KY = 0), X 1.20 a pixel (1.19 on NTSC's crystal) and 263 at
 * the centre (the memo's NTSC model: ~260). WHITE equals BAND because this
 * core's gun reads the one pixel under its aim when it is polled - it has no
 * field of view and so no first-or-last latch. A console's rows differ where
 * a real sensor sees several lines.
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

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(int cycles) { const uint16_t t0 = t2(); while ((uint16_t)(t2() - t0) < cycles) {} }

/* One 9-byte GunCon poll on port 1; false if the gun stopped acknowledging early. */
static bool poll(uint8_t *rx) {
    static const uint8_t tx[9] = { 0x01, 0x42, 0, 0, 0, 0, 0, 0, 0 };
    write16(JOY_CTRL, 0x0040);
    write16(JOY_MODE, 0x000D);
    write16(JOY_BAUD, 0x0088);
    write16(JOY_CTRL, 0x0000);
    wait(2000);
    write16(JOY_CTRL, 0x0003);
    wait(1500);
    bool ok = true;
    for (int b = 0; b < 9; b++) {
        while (read32(JOY_STAT) & 2) (void)read8(JOY_DATA);
        write16(JOY_CTRL, 0x0013);
        const uint16_t t0 = t2();
        write8(JOY_DATA, tx[b]);
        bool acked = false;
        while ((uint16_t)(t2() - t0) < 4000) { const uint32_t st = read32(JOY_STAT); if (st & 0x80) acked = true; if ((st & 2) && (acked || b == 8)) break; }
        while (!(read32(JOY_STAT) & 2) && (uint16_t)(t2() - t0) < 4000) {}
        rx[b] = (read32(JOY_STAT) & 2) ? read8(JOY_DATA) : 0xFF;
        if (!acked && b < 8) { ok = false; break; }
    }
    write16(JOY_CTRL, 0x0000);
    return ok;
}

static void waitVBlank() {
    write16(I_STAT, (uint16_t)~1);
    while (!(read16(I_STAT) & 1)) {}
}

/* GP0 words written straight to the port, the GPU ready for each command first (interrupts are off). */
static void gp0(uint32_t v) { write32(0x1F801810, v); }
static void gpuReady() { while (!(read32(0x1F801814) & 0x04000000)) {} }
static uint32_t bgr(int r, int g, int b) { return (uint32_t)(b << 16) | (g << 8) | r; }
static void fill(int x, int y, int w, int h, int r, int g, int b) {       /* GP0(02h): 16-pixel units in X */
    gpuReady(); gp0(0x02000000 | bgr(r, g, b)); gp0((y << 16) | x); gp0((h << 16) | w);
}
static void tile(int x, int y, int w, int h, int r, int g, int b) {       /* GP0(60h), any size */
    gpuReady(); gp0(0x60000000 | bgr(r, g, b)); gp0((y << 16) | x); gp0((h << 16) | w);
}
/* A cross at (x,y): four arms 9 pixels long, 2 wide, starting 3 pixels from the centre. */
static void cross(int x, int y, int r, int g, int b) {
    tile(x - 12, y - 1, 9, 2, r, g, b); tile(x + 3, y - 1, 9, 2, r, g, b);
    tile(x - 1, y - 12, 2, 9, r, g, b); tile(x - 1, y + 3, 2, 9, r, g, b);
}
static void screen(int x, int y, bool band) {
    if (!band) { fill(0, 0, 320, 240, 255, 255, 255); cross(x, y, 0, 0, 0); }
    else { fill(0, 0, 320, 240, 0, 0, 0); tile(0, y - 4, 320, 8, 255, 255, 255); cross(x, y, 0x60, 0, 0); }
}

struct Result { int n, xmin, xmax, ymin, ymax; int32_t xsum, ysum; };
static const int16_t targets[5][2] = { { 160, 120 }, { 40, 40 }, { 280, 40 }, { 40, 200 }, { 280, 200 } };
static Result res[5][2];

int main() {
    printf("\nsio/guncon-xy\n");
    printf("A GunCon in port 1, aimed by a person at crosses on a CRT. No assertions.\n\n");
    initVideo(320, 240);
    const uint32_t h1 = 0x260, h2 = 0xC60, v1 = 0x10, v2 = 0x100;
    writeGP1(0x06, (h2 << 12) | h1);
    writeGP1(0x07, (v2 << 10) | v1);
    printf("GPUSTAT %08x; GP1(06h) X %03x..%03x, GP1(07h) Y %03x..%03x\n\n", (unsigned)ReadGPUstat(),
           (unsigned)h1, (unsigned)h2, (unsigned)v1, (unsigned)v2);
    const uint16_t prevT2 = read16(0x1F801124);
    write16(0x1F801124, 0);
    EnterCriticalSection();
    uint8_t rx[9];
    for (int t = 0; t < 5; t++)
        for (int s = 0; s < 2; s++) {
            const int x = targets[t][0], y = targets[t][1];
            screen(x, y, s == 1);
            Result &R = res[t][s];
            R = { 0, 99999, -1, 99999, -1, 0, 0 };
            /* the trigger released first, so a held one does not carry over */
            for (int f = 0; f < 60 * 20; f++) { waitVBlank(); if (poll(rx) && (rx[4] & 0x20)) break; }
            printf("aim at (%d, %d) on the %s screen and hold the trigger\n", x, y, s ? "BAND" : "WHITE");
            for (int f = 0; f < 60 * 20 && R.n < 30; f++) {
                waitVBlank();
                if (!poll(rx) || (rx[4] & 0x20)) continue;           /* trigger (bit 13) not held */
                const int gx = rx[5] | (rx[6] << 8), gy = rx[7] | (rx[8] << 8);
                if (gx == 1 && (gy == 0x0A || gy == 0x05)) continue;  /* no light / light in VSYNC */
                R.n++; R.xsum += gx; R.ysum += gy;
                if (gx < R.xmin) R.xmin = gx; if (gx > R.xmax) R.xmax = gx;
                if (gy < R.ymin) R.ymin = gy; if (gy > R.ymax) R.ymax = gy;
            }
            printf("release the trigger\n");
        }
    ExitCriticalSection();
    write16(0x1F801124, prevT2);

    printf("\ntarget      screen  readings   X min..max  mean     Y min..max  mean\n");
    for (int t = 0; t < 5; t++)
        for (int s = 0; s < 2; s++) {
            const Result &R = res[t][s];
            printf("(%3d,%3d)   %-6s  %2d       ", targets[t][0], targets[t][1], s ? "BAND" : "WHITE", R.n);
            if (!R.n) { printf("none\n"); continue; }
            const int xm10 = R.xsum * 10 / R.n, ym10 = R.ysum * 10 / R.n;
            printf("%4d..%4d  %4d.%d   %4d..%4d  %4d.%d\n", R.xmin, R.xmax, xm10 / 10, xm10 % 10, R.ymin, R.ymax, ym10 / 10, ym10 % 10);
        }
    printf("\nsio/guncon-xy: Done.\n");
    for (;;) VSync(0);
}
