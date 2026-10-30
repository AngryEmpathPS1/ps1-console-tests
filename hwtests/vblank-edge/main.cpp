/*
 * gpu/vblank-edge - does raising the display's bottom edge (GP1(07h) Y2) past
 * the current line, INSIDE VBlank, end VBlank at once?
 *
 * WHY (STATUS, "Dynasty Warriors (Europe)"). That game's page flip writes
 * GP1(07h) from its VBlank callback at line 283 of a PAL 480i frame, moving Y2
 * from 275 to 290. This core, like DuckStation, decides VBlank every line as
 * "line < Y1 or line >= Y2", so line 284 is suddenly inside the display: VBlank
 * ends and starts again at 291 - a second VBlank IRQ inside one real frame.
 * The game's flip counter then runs out of phase with the interlace field and
 * never recovers. If VBlank can only END at Y1 (a set/reset latch on line == Y2
 * and line == Y1) there is no second VBlank and the game plays. psx-spx says
 * only that VBlank follows GP1(07h), not how its edges are made.
 *
 * WHAT IT DOES. Polls I_STAT.0 for the VBlank edge with interrupts off and
 * counts scanlines on Timer1 (clock source: hblank). For each case it sets a
 * normal range, waits for a VBlank edge, waits d more lines, rewrites GP1(07h),
 * and counts the lines to the NEXT VBlank edge:
 *
 *   RAISE  Y2 + 40 (the current line is now inside the display)
 *            per-line compare predicts  40 - d        lines
 *            edge latch predicts        total - d + 40 lines (next frame)
 *   LOWER  Y2 - 40 (control: the current line stays outside either way)
 *            both predict               total - d - 40 lines
 *
 * in 240-line mode and in 480i, with the display range set to 16..200 (NTSC)
 * or 35..220 (PAL) so that VBlank is long and both new edges stay in the frame. `total` is measured first (edge to edge with an
 * unchanged range), so the numbers are this console's own.
 *
 * Two more cases (M194):
 *
 *   PAST   Y2 past the frame's last line. psx-spx GP1(07h), nocash's note: "If
 *          Y2 is bigger than 106h (NTSC) or 139h (PAL), then the hardware
 *          stops to generate vblank interrupts (IRQ0)". Counts VBlank IRQs over
 *          10 frames, then restores the range and counts 3 more as a control.
 *            per-line compare predicts  10 (it still enters VBlank at the wrap)
 *            either latch predicts       0
 *   DROP   Y2 moved ABOVE the beam, 60 lines into the display.
 *            per-line compare, and a latch that only ENDS at Y1, predict 1 line
 *            a latch that only STARTS on line == Y2 (mednafen) predicts
 *                                        total - 30 lines (next frame)
 *
 * THE RULE (hwtests/README.md): run here first. The M193 core compared per
 * line and read RAISE 40 - d, PAST 10, DROP 1. Since M194 this core is
 * mednafen's latch and reads RAISE total - d + 40, PAST 0, DROP total - 30.
 * LOWER reads total - d - 40 on both.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js vblank-edge`):
 *   RAISE near a whole frame (VBlank ends only at Y1, M194) - 302 lines at d=1 falling a line a step to 273
 *   at d=30; LOWER 222..193; PAST 0 IRQs in 10 frames; DROP 233 (232 in 480i).
 *
 * THE CONSOLES (SCPH-5501, 7501, 9001, two runs each): every row as this core at v0.236 but one - 240-line PAST's
 * control, range restored, 3 VBlanks in 3 frames where this core read 2. The restore follows the 2,630th Timer1 tick
 * after an edge, and on a console that tick comes before line Y2's check (T1_RETRACE: Timer1 ticks 200 GPU ticks
 * before a line ends, mednafen's retrace). ON THIS CORE AT v0.377.23: all 20 rows the consoles', identical.
 */
#include <common.h>
#include <psxapi.h>
#include <gpu.h>
#include <io.h>
#include <timer.h>

#define I_STAT 0x1F801070
#define I_MASK 0x1F801074

static int pal;
static uint32_t Y1, Y2;

static void setRange(uint32_t y1, uint32_t y2) { writeGP1(7, (y1 & 0x3FF) | ((y2 & 0x3FF) << 10)); }

static inline void ackVblank() { write32(I_STAT, ~1u); }
static inline int vblankEdge() { return (read32(I_STAT) & 1) != 0; }

/* Wait for the next VBlank edge; return the Timer1 line count at it (then reset). */
static uint32_t waitEdge() {
    ackVblank();
    while (!vblankEdge()) { }
    uint32_t lines = readTimer(1);
    resetTimer(1);
    ackVblank();
    return lines;
}

static uint32_t waitLines(uint32_t d) { while (readTimer(1) < d) { } return readTimer(1); }

static void runMode(const char* name, uint32_t mode08) {
    writeGP1(8, mode08);
    setRange(Y1, Y2);
    waitEdge(); waitEdge();
    uint32_t total = waitEdge();
    uint32_t total2 = waitEdge();
    printf("%s: frame (VBlank edge to edge, unchanged range) %d / %d lines\n", name, total, total2);
    static const uint32_t ds[] = { 1, 3, 6, 10, 15, 20, 30 };
    for (uint32_t k = 0; k < sizeof(ds) / sizeof(ds[0]); k++) {
        uint32_t d = ds[k];
        /* RAISE */
        setRange(Y1, Y2); waitEdge(); waitEdge();
        uint32_t at = waitLines(d);
        setRange(Y1, Y2 + 40);
        ackVblank(); while (!vblankEdge()) { } uint32_t raise = readTimer(1) - at;
        /* LOWER (control) */
        setRange(Y1, Y2); waitEdge(); waitEdge();
        at = waitLines(d);
        setRange(Y1, Y2 - 40);
        ackVblank(); while (!vblankEdge()) { } uint32_t lower = readTimer(1) - at;
        printf("%s d=%2d  RAISE next VBlank after %3d lines (per-line %d, latch %d)   LOWER %3d (both %d)\n",
               name, d, raise, 40 - d, total - d + 40, lower, total - d - 40);
    }
    setRange(Y1, Y2);

    /* PAST: Y2 beyond the last line of the frame. */
    setRange(Y1, Y2); waitEdge();
    setRange(Y1, pal ? 0x150 : 0x110);
    uint32_t past = 0;
    resetTimer(1); ackVblank();
    while (readTimer(1) < total * 10) { if (vblankEdge()) { past++; ackVblank(); } }
    setRange(Y1, Y2);
    uint32_t back = 0;
    resetTimer(1); ackVblank();
    while (readTimer(1) < total * 3) { if (vblankEdge()) { back++; ackVblank(); } }
    printf("%s PAST  Y2 past the last line: %d VBlank IRQs in 10 frames (per-line 10, latch 0); range restored: %d in 3\n",
           name, past, back);

    /* DROP: Y2 moved above the beam during the display. */
    setRange(Y1, Y2); waitEdge();                  /* at line Y2, Timer1 reset */
    uint32_t at = waitLines(total - Y2 + Y1 + 60); /* line Y1 + 60 */
    setRange(Y1, Y1 + 30);
    ackVblank(); while (!vblankEdge()) { } uint32_t drop = readTimer(1) - at;
    printf("%s DROP  Y2 moved above the beam: next VBlank after %d lines (per-line 1, start-latch %d)\n",
           name, drop, total - 30);
    setRange(Y1, Y2);
}

int main() {
    printf("\ngpu/vblank-edge\n");
    pal = (ReadGPUstat() >> 20) & 1;
    Y1 = pal ? 0x23 : 0x10;
    /* A bottom edge well above the frame's end, so VBlank is 60+ lines long and
     * Y2 +/- 40 stays inside the frame. The first version used the BIOS's own
     * 256 (NTSC) - VBlank then lasted 7 lines before the wrap, Y2 + 40 fell past
     * the frame's end, and its predictions described nothing. */
    Y2 = pal ? 220 : 200;
    printf("video: %s, display range %d..%d\n", pal ? "PAL" : "NTSC", Y1, Y2);

    EnterCriticalSection();
    uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    uint16_t prev = initTimer(1, 1);     /* Timer1 counts hblanks, free-running */
    resetTimer(1);

    const uint32_t p = pal ? 0x08 : 0x00;
    runMode("240", 0x01 | p);            /* 320 wide, 240 lines */
    runMode("480i", 0x01 | 0x04 | 0x20 | p);   /* 320 wide, 480 lines, interlaced */
    writeGP1(8, 0x01 | p);

    restoreTimer(1, prev);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\nRead it like this: RAISE near 40-d means VBlank follows the range line by line\n");
    printf("(DuckStation); RAISE near a whole frame means VBlank ends only at Y1 (mednafen,\n");
    printf("this core since M194). PAST 0 is nocash's documented behaviour; DROP near a\n");
    printf("whole frame means VBlank also STARTS only on the line equal to Y2.\n");
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
