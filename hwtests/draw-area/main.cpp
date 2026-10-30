/*
 * gpu/draw-area — is the drawing area's BOTTOM-RIGHT corner inside it or not?
 *
 * WHY THIS TEST EXISTS. psx-spx does not say. The whole of GP0(E4h) is:
 *
 *   "Sets the drawing area corners. The Render commands GP0(20h..7Fh) are
 *    automatically clipping any pixels that are outside of this region."
 *
 * which never states whether X2,Y2 are the last pixel INSIDE the region or the
 * first one outside it. Every emulator picks inclusive; none of them cites a
 * measurement. This core clips with `x > drawRight`, i.e. inclusive.
 *
 * Then two hardware-derived references turned out to disagree (REFERENCES 40).
 * PeterLemon's `RenderRectangleClip16BPP` screenshot shows the corner NOT
 * drawn: against it this core draws 231 pixels the console does not and misses
 * none, and every one of them sits exactly on an X2 or a Y2. Making the corner
 * exclusive takes that test from 233 differing pixels to 0. But ps1-tests'
 * reference VRAM dumps say the opposite - under the same exclusive rule
 * gputest goes from 11/19 pixel-exact to 8/19, and uv-interpolation from exact
 * to 510 differing halfwords.
 *
 * One of those two is a picture of a television and the other is VRAM read
 * back off the machine. This test is the third voice, and it is the right KIND
 * of evidence: it asks the GPU to draw across a corner and then READS THE
 * CORNER PIXEL BACK, so nothing between the framebuffer and the answer can
 * colour it.
 *
 * HOW TO READ IT. For each case the test prints the four pixels around the
 * bottom-right corner and the one at the top-left:
 *
 *   (X2  ,Y2  )  DRAWN     -> the corner is INSIDE  the area: INCLUSIVE
 *   (X2  ,Y2  )  untouched -> the corner is OUTSIDE the area: EXCLUSIVE
 *
 * (X2-1,Y2-1) must ALWAYS be drawn and (X2+1,Y2+1) must ALWAYS be untouched
 * under either rule; they are there so a test that has stopped working cannot
 * look like an answer. So is the top-left: every reference agrees X1,Y1 is
 * inside, so a run that reports it untouched is a broken instrument, not a
 * discovery.
 *
 * THE RULE (hwtests/README.md): a test is not an oracle until it has been
 * shown to measure what it claims. This one is run on this core first, where
 * the answer is known to be INCLUSIVE, and then again with a require hook that
 * makes the corner exclusive - it must flip. Only then is it worth carrying to
 * a console.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js draw-area`):
 *   INCLUSIVE, every control passing (as since M142).
 */
#include <common.h>

/* A colour nothing else writes, so "untouched" is unambiguous. */
#define SENTINEL 0x7C1F      /* 15-bit magenta: r31 g0 b31 */
#define DRAWN_R  0x00
#define DRAWN_G  0xFF
#define DRAWN_B  0x00

static void setClipArea(int x1, int y1, int x2, int y2) {
    writeGP0(0xE3, (uint32_t)x1 | ((uint32_t)y1 << 10));
    writeGP0(0xE4, (uint32_t)x2 | ((uint32_t)y2 << 10));
    writeGP0(0xE5, 0);                      /* drawing offset 0,0 */
}

/* Was this pixel painted over, or is the sentinel still there? */
static int drawn(int x, int y) {
    return (vramGet(x, y) & 0xFFFF) != SENTINEL;
}

/*
 * A FLAT RECTANGLE, GP0(60h) - a RENDER command, so the drawing area applies.
 *
 * The first version of this test used common's fillRect(), which is GP0(02h)
 * "Fill VRAM": not in the 20h..7Fh render range, so psx-spx's clipping
 * sentence does not cover it and hardware does not clip it - and it rounds X
 * down to a multiple of 16 and the width up to one. Every answer it gave was
 * the fill's own rounding. THE CONTROLS CAUGHT IT: three of the four cases
 * reported the pixel strictly INSIDE the area as untouched, which cannot
 * happen under either rule, so the run disqualified itself instead of printing
 * a plausible wrong answer.
 */
static void tile(int x, int y, int w, int h) {
    TILE t;
    setTile(&t);
    setRGB0(&t, DRAWN_R, DRAWN_G, DRAWN_B);
    setXY0(&t, x, y);
    setWH(&t, w, h);
    DrawPrim(&t);
}

static void probe(const char* label, int x1, int y1, int x2, int y2) {
    /*
     * Only the four probed pixels need the sentinel; a pixel the rectangle
     * never reaches reads as untouched either way. VRAM transfers are not
     * clipped by the drawing area, but it is opened anyway so that nothing in
     * the setup depends on the very rule under test.
     */
    setClipArea(0, 0, 1023, 511);
    vramPut(x1, y1, SENTINEL);
    vramPut(x2 - 1, y2 - 1, SENTINEL);
    vramPut(x2, y2, SENTINEL);
    vramPut(x2 + 1, y2 + 1, SENTINEL);
    DrawSync(0);

    /* Now the area under test, and a rectangle that overhangs it on all sides. */
    setClipArea(x1, y1, x2, y2);
    tile(x1 - 1, y1 - 1, (x2 - x1) + 3, (y2 - y1) + 3);
    DrawSync(0);

    setClipArea(0, 0, 1023, 511);
    const int tl   = drawn(x1, y1);
    const int in   = (x2 - 1 >= x1 && y2 - 1 >= y1) ? drawn(x2 - 1, y2 - 1) : -1;
    const int corn = drawn(x2, y2);
    const int out  = drawn(x2 + 1, y2 + 1);

    printf("  %s  area %d,%d - %d,%d\n", label, x1, y1, x2, y2);
    printf("      (%4d,%4d) top-left      %s   [control: must be DRAWN]\n", x1, y1, tl ? "DRAWN    " : "untouched");
    if (in >= 0)
        printf("      (%4d,%4d) inside       %s   [control: must be DRAWN]\n", x2 - 1, y2 - 1, in ? "DRAWN    " : "untouched");
    printf("      (%4d,%4d) THE CORNER   %s   <-- DRAWN = inclusive, untouched = exclusive\n", x2, y2, corn ? "DRAWN    " : "untouched");
    printf("      (%4d,%4d) past it      %s   [control: must be untouched]\n", x2 + 1, y2 + 1, out ? "DRAWN    " : "untouched");

    if (!tl || in == 0 || out)
        printf("      *** A CONTROL FAILED - this run says nothing about the corner ***\n");
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/draw-area: is GP0(E4h)'s X2,Y2 the last pixel inside the area?\n\n");

    /* A plain area in the middle of the screen. */
    probe("A", 40, 40, 100, 80);
    /* One that ends where PeterLemon's test disagreed with this core. */
    probe("B", 43, 17, 277, 53);
    /* A single-pixel-wide area: X1 == X2. Under an exclusive rule it can draw
     * nothing at all, which is the sharpest form of the question. */
    probe("C", 85, 52, 85, 72);
    /* Hard against the right and bottom of a 320x240 screen. */
    probe("D", 300, 220, 319, 239);

    printf("gpu/draw-area: Done.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
