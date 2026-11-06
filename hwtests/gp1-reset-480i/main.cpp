/*
 * gpu/gp1-reset-480i - does GP1(00h) raise IRQ0 at once when it is written in the picture of a 480i frame?
 *
 * WHY (2026-11-06). gpu/gp1-reset (the second SCPH-5501, 54 rows x3) and psx-spx 11e277d (Rix, seven consoles) say a GP1(00h)
 * written in the display raises IRQ0 at once and one in the blanking raises none - "measured in NTSC 240p with the display off".
 * Every disc's first GP1(00h) comes in the BIOS shell's mode instead, 640x480 interlaced (GP1(08h)=27h). MLB 99 (USA)'s loader
 * writes its ResetGraph there, line 69 or 167 of a field, display just turned off, I_MASK 0; an IRQ0 latched by it is taken
 * when the mask comes back and leaves the k0 that its later jump through a NULL pointer crashes on (STATUS "MLB 99"). Under the
 * 240p rule nine boots in ten would freeze, so either the game freezes on a console or 480i differs. Nobody has measured it.
 *
 * WHAT IT DOES, interrupts off, I_STAT.0 polled, Timer 1 counting hblanks, Timer 2 the system clock (gp1-reset's method):
 *   each row: GP1(00h), set the mode, two VBlank edges (a third if the field bit is not the one wanted), d lines, GP1(00h);
 *   from the write the cycles and lines to the next two edges, GPUSTAT.13 / .31 at the edge and either side of the write.
 *   240P  the reset's own mode, display off: the known rule beside the new rows (d 8 blanking, 100 picture).
 *   480I  GP1(06h) C60260h, (07h) 03FC10h, (08h) 27h, display on - the shell's registers - d through blanking and picture,
 *         each field.
 *   OFF   the same 480i with GP1(03h)=1 written at the edge, as MLB 99 turns the display off just before its reset.
 * Every wait is bounded. 2026-11-06, Claude (AI) for AngryEmpathPS1.
 *
 * READ IT LIKE THIS. The 240p rule carried over: 480I/OFF rows in the picture read "edge ~50 cy" and then about 257 lines;
 * in the blanking, 257 lines. A 480i that differs: the picture rows' first edge is late (257 lines, or the old field's edge).
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

enum { M240 = 0, M480 = 1, MOFF = 2 };
static const char *const NAME[] = { "240P", "480I", "OFF " };

static void setMode(int m) {
    writeGP1(0, 0);
    if (m == M240) return;
    writeGP1(6, 0xC60260);
    writeGP1(7, 0x03FC10);
    writeGP1(8, 0x27);
    writeGP1(3, 0);
}

/* field: -1 any, else the GPUSTAT.13 wanted just after the edge that starts the d lines */
static void row(int m, uint32_t d, int field) {
    setMode(m);
    if (!waitEdge() || !waitEdge()) { printf("%s d=%3d  NO VBLANK before the write\n", NAME[m], d); return; }
    uint32_t f13 = (read32(GPUSTAT) >> 13) & 1;
    if (field >= 0 && (int)f13 != field) {
        if (!waitEdge()) { printf("%s d=%3d  NO VBLANK for the field\n", NAME[m], d); return; }
        f13 = (read32(GPUSTAT) >> 13) & 1;
    }
    if (m == MOFF) writeGP1(3, 1);
    waitLines(d);
    ackVblank();
    uint32_t s = read32(GPUSTAT);
    writeGP1(0, 0);
    uint32_t t0 = now();
    uint32_t l0 = readTimer(1);
    uint32_t after = read32(GPUSTAT) >> 31, last = after;
    uint32_t e1 = 0, e2 = 0, l1 = 0, l2 = 0, fl0 = 0;
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
        if (b != last) { if (!flips) fl0 = t - t0; flips++; last = b; }
    }
    printf("%s field %d d=%3d  stat %08x  edge %7d cy %3d lines  next %7d cy %3d lines  bit31 %d->%d first flip %d\n",
           NAME[m], f13, d, s, e1, l1, e2, l2, s >> 31, after, fl0);
}

int main() {
    printf("\ngpu/gp1-reset-480i\n");
    printf("GPUSTAT at entry %08x\n", read32(GPUSTAT));
    EnterCriticalSection();
    uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    uint16_t prev1 = initTimer(1, 1);                 /* hblanks */
    uint16_t prev2 = initTimer(2, 0);                 /* the system clock */
    lo = readTimer(2);
    resetTimer(1);

    for (int k = 0; k < 2; k++) { row(M240, 8, -1); row(M240, 100, -1); }
    static const uint16_t ds[] = { 0, 8, 16, 22, 24, 30, 69, 100, 167, 200, 250, 258 };
    for (int f = 0; f < 2; f++)
        for (uint32_t k = 0; k < sizeof(ds) / sizeof(ds[0]); k++) { row(M480, ds[k], f); row(M480, ds[k], f); }
    static const uint16_t dOff[] = { 8, 69, 100, 167 };
    for (int f = 0; f < 2; f++)
        for (uint32_t k = 0; k < sizeof(dOff) / sizeof(dOff[0]); k++) { row(MOFF, dOff[k], f); row(MOFF, dOff[k], f); }
    for (int k = 0; k < 2; k++) { row(M240, 8, -1); row(M240, 100, -1); }

    writeGP1(0, 0);
    restoreTimer(1, prev1);
    restoreTimer(2, prev2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
