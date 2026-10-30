/*
 * gpu/pal-line - how long is a PAL scanline, in video ticks: 3405 or 3406?
 *
 * WHY (2026-10-08). The two references disagree: mednafen's PAL line is 3405 ticks, psx-spx's "3406 video cycles per
 * scanline (or 3406.1 or so?)", which DuckStation follows; this core has mednafen's (M108) and nothing here measured it.
 * pal-dots on the SCPH-5501, 7501 and 9001 (console day) counts PAL 320 at 426 dots a line, where floor(3405 / 8) is 425
 * and floor(3406 / 8) is 425 too - psx-spx calls 425.75 "the only value that is rounded up", and with 3406 ticks every one
 * of the ten readings is "floor, but up from a fraction of .75". v0.377.33 gives PAL 320 its 426 as an exception on 3405;
 * a frame's length in CPU cycles tells the two lines apart.
 *
 * WHAT IT DOES. Interrupts off. For each video mode - NTSC and PAL, 240 lines and 480 interlaced, 320 wide, and PAL at 256
 * and 640 wide as controls (a line's length should not depend on its width) - GP1(08h) sets it, four frames settle, then
 * from one VBlank edge (I_STAT bit 0) to the 64th after it Timer 2 counts CPU cycles and Timer 1 hblanks, each read in the
 * polling loop with its 16-bit wraps added up. Then the first eight frames one by one (an interlaced frame's fields may
 * differ by a line).
 *
 * READ IT LIKE THIS. The console's two crystals (CPU 33.8688 MHz, video 53.693182 MHz on an NTSC console) give
 * 0.6307845 CPU cycles a video tick:
 *   NTSC 240   263 lines x 3412.5 (3412 and 3413 alternating, mednafen and this core): 566,121 a frame; 3413 every line: 566,204
 *   PAL  240   314 lines x 3405: 674,416 a frame;  x 3406: 674,614 (198 more - 290 ppm)
 * A crystal off by its tolerance (~50 ppm) moves both, so the RATIO PAL / NTSC is printed too, which the crystals cancel
 * out of: 3405 -> 1.191317, 3406 -> 1.191667 (with NTSC's 3412.5).
 * PAL mode may leave the screen black on an NTSC console. The timers still count, and the TTY is the result.
 *
 * ON THIS CORE AT v0.377.33 (2026-10-08): NTSC 240 566,120.25 a frame, 263 lines, 2,152.548 a line; NTSC 480i fields
 * 263/262 alternating (566,1xx / 563,9xx); PAL 240 674,415.75, 314 lines, 2,147.820 a line, and the same at 256 and 640
 * wide; PAL 480i 314/313 alternating; PAL / NTSC 1.191294 - mednafen's 3405. A console at 3406 reads ~1.19164.
 * There are no assertions - compare against your emulator.
 * `node hwtests/run.js pal-line --ntsc --frames=3000`
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STATR  (*(volatile uint32_t*)0x1F801070)
#define GP1R     (*(volatile uint32_t*)0x1F801814)
#define GPUSTAT  (*(volatile uint32_t*)0x1F801814)

#define FRAMES 64
#define EACH   8

struct Mode { const char* name; uint32_t bits; };
static const Mode MODES[] = {
    { "NTSC 320 240 ", 0x01 }, { "NTSC 320 480i", 0x01 | 0x04 | 0x20 },
    { "PAL  320 240 ", 0x09 }, { "PAL  320 480i", 0x09 | 0x04 | 0x20 },
    { "PAL  256 240 ", 0x08 }, { "PAL  640 240 ", 0x0B },
};

/* Cycles and hblanks, their wraps added up at every look. */
static uint32_t cyc, lin;
static uint16_t lastC, lastL;
static inline void look() {
    const uint16_t c = read16(T2_COUNT), l = read16(T1_COUNT);
    cyc += (uint16_t)(c - lastC); lastC = c;
    lin += (uint16_t)(l - lastL); lastL = l;
}
/* Until the next VBlank edge, looking all the while. */
static void edge() {
    I_STATR = ~1u;
    while (!(I_STATR & 1)) look();
    look();
}

static uint32_t frameCyc[EACH], frameLin[EACH];
static uint32_t ntscCyc = 0;

static void measure(const Mode& m) {
    GP1R = 0x08000000u | m.bits;
    for (int i = 0; i < 4; i++) edge();
    edge();
    cyc = lin = 0;
    for (int f = 0; f < FRAMES; f++) edge();
    const uint32_t c = cyc, l = lin;
    for (int f = 0; f < EACH; f++) {
        const uint32_t c0 = cyc, l0 = lin;
        edge();
        frameCyc[f] = cyc - c0; frameLin[f] = lin - l0;
    }
    const uint32_t s = GPUSTAT;
    /* a frame in cycles (two places), lines (two places), cycles a line (three places) - in 32 bits: no 64-bit
     * arithmetic in this toolchain */
    const uint32_t fI = c / FRAMES, fF = (c % FRAMES) * 100 / FRAMES;
    const uint32_t lI = l / FRAMES, lF = (l % FRAMES) * 100 / FRAMES;
    const uint32_t pI = c / l, pF = (c % l) * 1000 / l;
    printf("%s  GPUSTAT %08x  %d frames: %u cycles, %u lines = %u.%02u cycles a frame, %u.%02u lines, %u.%03u cycles a line\n",
           m.name, (unsigned)s, FRAMES, (unsigned)c, (unsigned)l, (unsigned)fI, (unsigned)fF, (unsigned)lI, (unsigned)lF,
           (unsigned)pI, (unsigned)pF);
    printf("               one by one:");
    for (int f = 0; f < EACH; f++) printf(" %u/%u", (unsigned)frameCyc[f], (unsigned)frameLin[f]);
    printf("\n");
    if (m.bits == 0x01) ntscCyc = c;
    else if (m.bits == 0x09 && ntscCyc) {
        /* PAL 240 against NTSC 240, both 64 frames: the crystals cancel. Long division, a digit at a time. */
        uint32_t r = c % ntscCyc, digits = 0;
        for (int i = 0; i < 6; i++) { r *= 10; digits = digits * 10 + r / ntscCyc; r %= ntscCyc; }
        printf("               PAL / NTSC frame: %u.%06u (3405 ticks a line: 1.191317, 3406: 1.191667)\n",
               (unsigned)(c / ntscCyc), (unsigned)digits);
    }
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/pal-line\n");
    printf("A frame's length in CPU cycles (Timer 2) and hblanks (Timer 1), NTSC and PAL: 64 frames from a VBlank edge, then 8 one by one.\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);                          /* hblanks, free-running */
    write16(T2_MODE, 0x0000);                          /* the system clock, free-running */
    lastC = read16(T2_COUNT); lastL = read16(T1_COUNT);
    for (unsigned i = 0; i < sizeof(MODES) / sizeof(MODES[0]); i++) measure(MODES[i]);
    GP1R = 0x08000001u;                                /* back to NTSC 320 */
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/pal-line: Done.\n");
    for (;;) VSync(0);
}
