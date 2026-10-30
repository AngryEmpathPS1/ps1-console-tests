/*
 * gpu/clut-cost — what does a CLUT cache RELOAD cost?
 *
 * WHY THIS TEST EXISTS. ps1-tests has `gpu/clut-cache`, and it tests BEHAVIOUR:
 * it draws textured sprites and compares VRAM against a reference dump, so it
 * says exactly WHEN the palette cache is invalidated and nothing at all about
 * what the reload costs. src/psx.js loadClutCache carries the open question:
 *
 *   "KNOWN-OPEN (timing): psx-spx charges a CLUT cache miss 16.00 or 256.00
 *    clks - CPU clocks, not GPU clocks ... The rasteriser here is not costed at
 *    that granularity, so the reload is free."
 *
 * UNITS (corrected at M240): psx-spx's GPU timing chapter counts in 33 MHz CPU
 * clocks ("All rendering timings seem to be bound to the 33MHz CPU clock"), so
 * its 16/256 are 16/256 CPU cycles. This header and the loadClutCache comment
 * said GPU clocks until then. mednafen and DuckStation both charge 8/128 CPU
 * cycles, uncited - this test's two runs tell 16/256 from 8/128 apart:
 * 2,048/32,768 against 1,024/16,384 cycles over 128 alternating sprites.
 *
 * STATUS M131 measured how much that matters: **every one of 1,719 discs
 * reloads the CLUT, 53,903,815 times over the corpus.** It is one of only two
 * UNIVERSAL entries in the reachability table that carry a real unmeasured
 * quantity, and this core charges zero for all of them.
 *
 * HOW IT MEASURES IT. Two runs of the same number of identical textured
 * sprites, differing only in whether each sprite forces a palette reload:
 *
 *   SAME  - every sprite uses one CLUT, so the cache is loaded once
 *   ALTERNATING - consecutive sprites use two DIFFERENT CLUT addresses, which
 *                 gpu/clut-cache establishes invalidates the cache, so every
 *                 sprite reloads it
 *
 * The difference, divided by the sprite count, is the reload. Everything else -
 * the primitive, its size, its texels, the drawing area, the blend mode - is
 * identical between the two runs, so nothing else can account for a gap. Run at
 * both 4-bit (16 entries, psx-spx's 16.00 clocks) and 8-bit (256 entries,
 * 256.00 clocks), which should differ by about 16x if the published figures are
 * right.
 *
 * Timed with DrawSync so the CPU waits for the GPU, on Timer2 at the
 * system-clock/8 tick, which has 8x the range of the plain CPU clock - the
 * 8-bit alternating run is the long one and a single measure() call only
 * detects ONE counter overflow.
 *
 * READ THE ANSWER LIKE THIS. A difference of ~0 means the reload is free and
 * this core is already right. A difference near psx-spx's 16 and 256 CPU clocks
 * per sprite (or mednafen's and DuckStation's 8 and 128) means we are missing a cost that lands on every disc in the
 * library, and STATUS's standing trade - "adding it would move gpu/bandwidth
 * and the FF7 movie pacing for a timing-dump gain" - finally has a real number
 * on one side of it.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js clut-cost`):
 *   every difference 0 or -48 of ~24,200 - this core charges nothing for a CLUT RELOAD (psx-spx: 16 CPU
 *   clocks at 4bit, 256 at 8bit). A console that reads a cost here has found the missing term.
 *
 * CONSOLE DAY (2026-10-05, SCPH-5501 and SCPH-7501, two runs each): 8bit 252.4-253.4 cycles a sprite, 4bit
 * 0.0-1.6; "same" ~23,970 (187 a sprite, this core ~25,760: 201). The reload is real.
 *
 * ON THIS CORE AT v0.366 (`node hwtests/run.js clut-cost --ntsc`): every difference within 1 cycle a sprite, SERIAL
 * too. A scratch core charging psx-spx's 256 at 8bit (scratch/cdfix/clutreload-attempt.patch) reads 164.1-164.3 in
 * the first four runs and 252.2-252.7 in SERIAL: there the GPU takes 111 cycles a cached 8bit sprite and the
 * reload 256, 367 a sprite against the CPU's 200, and each sprite's words wait in the FIFO until the one before
 * is done - the alternating runs go at the GPU's pace, the same runs at the CPU's, and the difference is
 * 367 - 200, not 256. SERIAL's 8bit difference is the reload (less ~3.5, where the bit-26 poll lands), and on a
 * console that queues the same way the first runs' alternating time a sprite (alternating / 128) is the GPU's
 * sprite and reload together.
 * THE CONSOLES' SERIAL (2026-10-06, all three, two runs each): 4bit 20.4-24.3, 8bit 259.7-262.8 - the reload a CPU cycle
 * an entry and ~9 (25 / 265). HERE SINCE v0.377.45 (CLUT_RELOAD8_CYCLES): SERIAL 26.5 / 256, the DrawSync runs 0.06 / 174 -
 * the rest of the consoles' 252 is this core's 8bit sprite (111 against ~175; gpu-tex-opaque).
 */
#include <common.h>
#include <measure.hpp>

#define SPRITES 128

enum TextureMode { bit4 = 0, bit8 = 1, bit15 = 2, reserved = 3 };

static void setE1(enum TextureMode mode, int texPageX, int texPageY) {
    DR_TPAGE e;
    unsigned short texpage = getTPage((int)mode, 0, texPageX, texPageY);
    setDrawTPage(&e, 1, 0, texpage);
    DrawPrim(&e);
}

/* One 8x8 textured sprite at a fixed place, with the CLUT the caller names. */
static void sprite(int clutx, int cluty) {
    SPRT s;
    setSprt(&s);
    setSemiTrans(&s, false);
    setXY0(&s, 0, 200);
    setWH(&s, 8, 8);
    setRGB0(&s, 0x80, 0x80, 0x80);
    setClut(&s, clutx, cluty);
    setUV0(&s, 0, 0);
    DrawPrim(&s);
}

static void uploadToGPU(uint16_t* buffer, uint16_t x, uint16_t y, int words) {
    DrawSync(0);
    CPU2VRAM buf;
    setcode(&buf, 0xA0);
    setlen(&buf, 3);
    setXY0(&buf, x, y);
    setWH(&buf, words, 1);
    DrawPrim(&buf);
    volatile uint32_t* GP0 = (uint32_t*)0x1F801810;
    for (int n = 0; n < words; n += 2) {
        *GP0 = buffer[n] | (buffer[n + 1] << 16);
    }
}

/* Two palettes and one texture, all in the unused bottom of VRAM. */
#define CLUT_A_X 0
#define CLUT_A_Y 490
#define CLUT_B_X 0
#define CLUT_B_Y 491
#define TEX_X    0
#define TEX_Y    492

static uint16_t clutA[256];
static uint16_t clutB[256];
static uint16_t texels[64];

/*
 * BOTH RUNS EXECUTE THE IDENTICAL INSTRUCTIONS. Only the two table entries
 * differ: equal for the "same" run, different for the "alternating" one. The
 * first version of this test branched on `i & 1` in the alternating loop only,
 * and this emulator - which charges a CLUT reload NOTHING - duly reported
 * "6.00 cycles/sprite" at both 4bit and 8bit. That was the branch, not the
 * palette, and the fact that the two depths agreed exactly is what gave it
 * away. Running a new test where the answer is already known is the only
 * reason that was caught before hardware.
 */
static uint16_t clutYs[2];
/* SERIAL (2026-10-05, after console day): after every sprite, in both runs alike, the CPU waits for GPUSTAT.26
 * ("ready to receive a command" - clear while a command executes) before the next, so nothing of the GPU's time
 * hides under the CPU's next sprite and the difference is the reload itself. DrawPrim alone does not do that: its
 * DrawSync (PSn00bSDK drawsync.s), with the DMA direction off as DrawPrim leaves it, waits for bit 28 only, and
 * ps1-tests' gpu/gpustat log has a console reading bit 28 SET (26 clear) during a CPU-drawn TILE. Console day's
 * runs read 252.4-253.4 cycles a sprite at 8 bits on both consoles and 0-1.6 at 4 bits; this emulator, given
 * psx-spx's 256 and 16 for the reload, reads 165.4 there (v0.366, NTSC): the CPU builds the next sprite under it.
 * So a console's 253 is its reload less whatever its CPU hides, and SERIAL is the reload whole. */
static volatile uint32_t* const GPUSTAT = (volatile uint32_t*)0x1F801814;
static bool serial;

static uint32_t measureRun(void) {
    return measure<MeasureMethod::CpuClock8>([] {
        for (int i = 0; i < SPRITES; i++) { sprite(CLUT_A_X, clutYs[i & 1]); if (serial) while (!(*GPUSTAT & (1u << 26))) {} }
        DrawSync(0);
    });
}

static void runMode(const char* name, enum TextureMode mode) {
    setE1(mode, 0, 0);
    DrawSync(0);

    /* Warm everything once so neither run pays a first-time cost the other
     * does not - the question is the STEADY-STATE reload, not the first. */
    sprite(CLUT_A_X, CLUT_A_Y);
    DrawSync(0);

    clutYs[0] = CLUT_A_Y; clutYs[1] = CLUT_A_Y;   /* one palette: loaded once */
    const uint32_t same = measureRun();
    clutYs[0] = CLUT_A_Y; clutYs[1] = CLUT_B_Y;   /* two: a reload per sprite */
    const uint32_t alt  = measureRun();
    const int32_t  diff = (int32_t)alt - (int32_t)same;

    /* Print the sign once, then work in magnitude - a negative diff formatted
     * as "%d.%02d" reads as POSITIVE, which is the last thing a hardware result
     * should do. Near zero is the expected answer on a core that does not charge
     * for the reload, so the sign is most of the signal. */
    const int32_t mag = diff < 0 ? -diff : diff;
    printf("  %-6s same %7u   alternating %7u   diff %7d  ->  %s%d.%02d cycles/sprite\n",
           name, same, alt, diff, diff < 0 ? "-" : "",
           mag / SPRITES, mag % SPRITES * 100 / SPRITES);
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/clut-cost\n");
    printf("What does a CLUT cache RELOAD cost? gpu/clut-cache says WHEN the palette\n");
    printf("cache is invalidated; nothing says what the reload is worth.\n");
    printf("Two runs of %d identical sprites: one CLUT throughout, then alternating\n", SPRITES);
    printf("between two CLUT addresses so every sprite reloads. The difference is it.\n");
    printf("There are no assertions - compare against your emulator.\n\n");

    for (int i = 0; i < 256; i++) {
        clutA[i] = (uint16_t)(0x8000 | (i & 0x1F));
        clutB[i] = (uint16_t)(0x8000 | ((i & 0x1F) << 5));
    }
    for (int i = 0; i < 64; i++) texels[i] = (uint16_t)(i | (i << 8));

    uploadToGPU(clutA, CLUT_A_X, CLUT_A_Y, 256);
    uploadToGPU(clutB, CLUT_B_X, CLUT_B_Y, 256);
    uploadToGPU(texels, TEX_X, TEX_Y, 64);
    DrawSync(0);

    printf("  psx-spx charges a miss 16.00 CPU clocks at 4bit and 256.00 at 8bit (mednafen, DuckStation: 8 and 128)\n\n");
    runMode("4bit", bit4);
    runMode("8bit", bit8);
    runMode("4bit", bit4);   /* again, because one draw of anything is luck */
    runMode("8bit", bit8);

    serial = true;
    printf("\nSERIAL - GPUSTAT.26 waited for after every sprite, so the GPU's whole time shows:\n");
    runMode("4bit", bit4);
    runMode("8bit", bit8);
    runMode("4bit", bit4);
    runMode("8bit", bit8);

    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05): on a console, Unirom's debug mode
     * takes the next upload over a program that loops, and after one that returned it went deaf (8.0.K, the
     * emulated console) or ran the next program into a crash (the SCPH-5501's Unirom). */
    for (;;) {}
}
