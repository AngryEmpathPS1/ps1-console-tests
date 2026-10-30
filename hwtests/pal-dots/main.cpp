/*
 * timer/pal-dots - how many dot-clock ticks does Timer 0 count in one scanline,
 * at each width, in NTSC mode and in PAL mode?
 *
 * WHY (ROAD A7). This emulator's dot divider restarts every line (psx-spx: the
 * dot clock "ignores the fractional part"; mednafen: "probably being reset each
 * scanline"), so a line counts floor(its ticks / the divider) dots. In NTSC 320
 * that is floor(3412.5 / 8) = 426, which ps1-tests' timers frame confirms
 * (112,034 dots a frame = 263 x 426 - 4). A PAL line is 3405 ticks (M108), so
 * PAL 320 counts 425 here. psx-spx says 426 ("rounded up"), and nocash's DIAG
 * source (DIAG.A22, near its dot-clock experiment) records his console's
 * readings: PAL 320 1AAh (426), PAL 640 353h (851), NTSC 320 426, NTSC 640 355h
 * (853). Every one of those is floor(ticks / divider) except PAL 320, and no
 * floor or ceiling rule fits it beside the others (M236's dig). Phase E found
 * it REACHED: eight of 1,964 European discs count dots at PAL 320. Blood Omen -
 * Legacy of Kain takes IRQ4 every 2,000 dots, and Evil Dead - Hail to the King
 * every 65,500. Star Wars - Rebel Assault II restarts the count at every hblank
 * (sync mode 1), so the largest value it can read is this question exactly.
 * Die Hard Trilogy, Darklight Conflict, Shaolin, Space Debris and Motorhead let
 * it run.
 *
 * WHAT IT DOES. For each video mode (NTSC, PAL) and width (256, 320, 368, 512,
 * 640), interrupts off: GP1(08h) sets the mode, and after two frames Timer 1
 * counts hblanks and Timer 0 dots, both free-running. At a Timer 1 step the
 * test reads Timer 0, and again at each of the next 64 steps. TOTALS are the
 * dots over 64 lines, 16 rounds. PER LINE is round one's 64 single lines.
 *
 * READ IT LIKE THIS - THE TOTALS. A read lands where the polling loop happens
 * to be when the hblank comes, so a single line swings by up to about 4 dots
 * either way (here PAL 320 reads 424 426 424 426..., its lines all 425), and a
 * total by up to about 4 from round to round. The most common total over 16
 * rounds, divided by 64, is the dots a line:
 *   - A divider that restarts every line gives floor(ticks / divider), an
 *     integer.
 *   - A free-running divider gives the fraction: PAL 320 at 3405 ticks is
 *     425.625 a line, 27,240 over 64.
 *   - A restarting divider that counts 426 in PAL 320 needs a line of 3,408
 *     ticks or more (27,264).
 * PAL mode may leave the screen black on an NTSC console (DIAG: "MAY NOT WORK
 * ON SOME CONSOLES"). The timers still count, and the TTY is the result.
 *
 * ON THIS CORE AT v0.263 (2026-09-28, `node hwtests/run.js pal-dots
 * --frames=3000`): floor(ticks / divider) at every setting. The most common
 * total is exactly 64 times it, and the rounds range within 4 of it (NTSC 640
 * 54,588-54,593, PAL 640 54,462-54,468, PAL 320 27,199-27,202):
 *     NTSC  256 341   320 426   368 487   512 682   640 853
 *     PAL   256 340   320 425   368 486   512 681   640 851
 * DIAG's console reads PAL 320 as 426; the other three it records agree with
 * these. There are no assertions - compare against your emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define GP1R     (*(volatile uint32_t*)0x1F801814)
#define GPUSTAT  (*(volatile uint32_t*)0x1F801814)

#define LINES  64
#define ROUNDS 16

struct Width { const char* name; uint32_t bits; };
static const Width WIDTHS[] = {
    { "256", 0x00 }, { "320", 0x01 }, { "368", 0x40 }, { "512", 0x02 }, { "640", 0x03 },
};

static uint16_t perLine[LINES];
static uint32_t totals[ROUNDS];

/* Wait for Timer 1 (hblanks) to step, then read Timer 0. */
static inline uint16_t atHblank() {
    const uint16_t l = read16(T1_COUNT);
    while (read16(T1_COUNT) == l) { }
    return read16(T0_COUNT);
}

static void measure(int pal, const Width& w) {
    GP1R = 0x08000000u | (pal ? 0x08u : 0u) | w.bits;
    write16(T1_MODE, 0x0100);                          /* hblanks, free-running */
    for (int i = 0; i < 700; i++) atHblank();          /* two frames: the new timing settles */
    write16(T0_MODE, 0x0100);                          /* dots, free-running */
    for (int i = 0; i < 4; i++) atHblank();
    for (int r = 0; r < ROUNDS; r++) {
        uint16_t last = atHblank();
        uint32_t total = 0;
        for (int i = 0; i < LINES; i++) {
            const uint16_t v = atHblank();
            const uint16_t d = (uint16_t)(v - last);
            if (r == 0) perLine[i] = d;
            total += d;
            last = v;
        }
        totals[r] = total;
    }
    const uint32_t s = GPUSTAT;
    printf("%s %s  GPUSTAT %08x  totals:", pal ? "PAL " : "NTSC", w.name, (unsigned)s);
    for (int r = 0; r < ROUNDS; r++) printf(" %u", (unsigned)totals[r]);
    const uint32_t mean = (totals[0] * 100u + LINES / 2) / LINES;
    printf("\n          round 1: %u.%02u a line; per line:", (unsigned)(mean / 100), (unsigned)(mean % 100));
    for (int i = 0; i < LINES; i++) { if (i % 16 == 0) printf("\n           "); printf(" %u", (unsigned)perLine[i]); }
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\ntimer/pal-dots\n");
    printf("Timer 0 dots per scanline, NTSC and PAL, every width: 16 totals over 64 lines, then round 1 line by line.\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE);
    for (int pal = 0; pal < 2; pal++)
        for (unsigned i = 0; i < sizeof(WIDTHS) / sizeof(WIDTHS[0]); i++) measure(pal, WIDTHS[i]);
    GP1R = 0x08000001u;                                /* back to NTSC 320 */
    write16(T0_MODE, m0);
    write16(T1_MODE, m1);
    ExitCriticalSection();
    printf("\ntimer/pal-dots: Done.\n");
    for (;;) VSync(0);
}
