/*
 * mdec/mdec-time - how long does the MDEC take to decode a macroblock?
 *
 * WHY (ROAD A8, the MDEC dig of 2026-09-27; ROAD B). Every movie a game plays
 * is paced by the MDEC: a player feeds it by DMA and waits for the output,
 * and a decoder that is too fast or too slow moves a movie against its audio
 * and against the CD. This emulator's MDEC timing rests on the step-by-step
 * status logs (M111, M249); nothing here times whole macroblocks through the
 * DMA channels, the way a game does, and nothing times a dense block against
 * a flat one.
 *
 * WHAT IT DOES. 320 macroblocks decoded in one command each time, DMA0 feeding
 * the input and DMA1 taking the output, both in 32-word blocks, timed from the
 * command to DMA1's end on Timer 2 at the system clock / 8 (wraps counted):
 *   DC only   every block its DC coefficient and the end code (2 halfwords)
 *   dense     every block a DC, 62 AC coefficients (run 0) and the end code
 * each at 15 bpp and 24 bpp (six blocks a macroblock) and at 8-bit mono (one
 * block each). Every stream is an exact number of 32-word DMA blocks and every
 * block an even number of halfwords, so no FE00h padding word is ever sent: a
 * console's MDEC can stall on one (nugget b7d9561, STATUS M236).
 *
 * READ IT LIKE THIS. Cycles for the whole command and per macroblock (per
 * block in mono). A dense block against a flat one says what the decoder's
 * time rests on - the run-length stage or the IDCT; 24 bpp against 15 bpp says
 * whether the colour conversion or the output transfer is the limit.
 *
 * ANSWERED by the SCPH-5501, 7501 and 9001 (console day; v0.377.30, src/psx.js MDEC_BLOCK_TICKS): 3,073 a macroblock at
 * 15 and 24 bpp, 513 a monochrome block - 512 a block, the decode running ahead of DMA1. AT v0.377.30: 3,072 3,070 510.
 * Its dense colour command (61,440 = F000h words) STOPS on every console with 8000h words left (A6007FFFh / A4007FFFh,
 * DMA0 400h blocks unsent): the input counter is 15 bits. Since v0.377.43 this core prints the same two lines.
 * ON THIS CORE AT v0.258 (`node hwtests/run.js mdec-time`):
 *   colour, flat or dense alike: 2,820 cycles a macroblock at 15 bpp, 2,885
 *   at 24 bpp - the core's copy-out clock, 448 a block x 6 = 2,688
 *   (DuckStation's TICKS_PER_BLOCK), plus the transfers;
 *   mono: 17 cycles a block flat, 41 dense - the transfers alone: this core
 *   charges a monochrome block no decode time at all.
 * The candidates it cannot choose between (src/psx.js MDEC_COPYOUT_TICKS,
 * KNOWN-OPEN, REFERENCES 53): Sony's "9,000 macroblocks per second" = 3,763
 * cycles a macroblock; mednafen's "about 512" a block, measured it says =
 * 3,072; DuckStation's 448 a block = 2,688. The source says nothing times the
 * decoder on a console. This does - and says whether a dense block costs more
 * than a flat one, and what a mono block costs.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>
#include <mdec.h>

#define MB 320
#define MAX_IN_WORDS  (MB * 6 * 32)       /* dense colour: six blocks of 64 halfwords */
#define MAX_OUT_WORDS (MB * 192)          /* 24 bpp */
#define DMA0_MADR 0x1F801080
#define DMA0_BCR  0x1F801084
#define DMA0_CHCR 0x1F801088
#define DMA1_MADR 0x1F801090
#define DMA1_BCR  0x1F801094
#define DMA1_CHCR 0x1F801098
#define DPCR      0x1F8010F0

static uint32_t in[MAX_IN_WORDS];
static uint32_t out[MAX_OUT_WORDS];

/* A block's run-length code into `hw`; returns halfwords written (always even). */
static int block(uint16_t* hw, bool dense, int seed) {
    int n = 0;
    hw[n++] = (1 << 10) | ((seed * 37) & 0xFF);          /* q_scale 1, a DC */
    if (dense) for (int k = 1; k <= 62; k++) hw[n++] = (0 << 10) | ((((k + seed) & 3) - 1) & 0x3FF);   /* run 0, levels -1..2 */
    hw[n++] = 0xFE00;                                     /* end of block */
    return n;
}

/* The input for `units` units of `blocksPer` blocks each; returns its length in words. */
static int build(bool dense, int units, int blocksPer) {
    uint16_t* hw = (uint16_t*)in;
    int n = 0;
    for (int u = 0; u < units; u++) for (int b = 0; b < blocksPer; b++) n += block(hw + n, dense, u * 7 + b);
    return n / 2;
}

static uint32_t clk8;
static uint16_t last;
static inline void tick() { const uint16_t t = readTimer(2); clk8 += (uint16_t)(t - last); last = t; }

/* One command: depth 0 4-bit, 1 8-bit, 2 24-bit, 3 15-bit. Returns cycles from the command to DMA1's end. */
static uint32_t decode(int depth, int inWords, int outWords) {
    mdec_ctrl(1u << 31);                                  /* reset */
    mdec_ctrl((1u << 30) | (1u << 29));                   /* DMA in and out enabled */
    write32(DPCR, read32(DPCR) | 0x88);                   /* DMA0 and DMA1 on */
    write32(DMA1_MADR, (uint32_t)out & 0xFFFFFF);
    write32(DMA1_BCR, ((outWords / 32) << 16) | 32);
    write32(DMA1_CHCR, 0x01000200);                       /* MDEC -> RAM, SyncMode 1, start */
    write32(DMA0_MADR, (uint32_t)in & 0xFFFFFF);
    write32(DMA0_BCR, ((inWords / 32) << 16) | 32);
    clk8 = 0; last = readTimer(2);
    write32(MDEC_CMD, (1u << 29) | (depth << 27) | (inWords & 0xFFFF));
    write32(DMA0_CHCR, 0x01000201);                       /* RAM -> MDEC, SyncMode 1, start */
    /* A time limit (console day, 2026-10-05): on the SCPH-5501 the dense blocks never finished, and this wait - with
     * interrupts off - left the console deaf to the cable until a reset. 50M cycles is ~1.5 s, some 50x the DC-only
     * run's 983,544. Stuck: what the MDEC and both channels say, printed at once, both channels stopped, the MDEC
     * reset, and the report says STUCK. */
    while (read32(DMA1_CHCR) & 0x01000000) {
        tick();
        if (clk8 > 50000000 / 8) {
            const uint32_t st = read32(0x1F801824), c0 = read32(DMA0_CHCR), b0 = read32(DMA0_BCR), c1 = read32(DMA1_CHCR), b1 = read32(DMA1_BCR);
            write32(DMA0_CHCR, c0 & ~0x01000000u);
            write32(DMA1_CHCR, c1 & ~0x01000000u);
            mdec_ctrl(1u << 31);
            printf("  (depth %d stuck: MDEC status %08x, DMA0 CHCR %08x BCR %08x, DMA1 CHCR %08x BCR %08x)\n", depth, (int)st, (int)c0, (int)b0, (int)c1, (int)b1);
            return 0xFFFFFFFFu;
        }
    }
    tick();
    return clk8 * 8;
}

static void report(const char* name, int units, uint32_t cycles) {
    if (cycles == 0xFFFFFFFFu) { printf("  %-26s STUCK (DMA1 not done after 50M cycles; see above)\n", name); return; }
    printf("  %-26s %8d cycles  %6d each\n", name, (int)cycles, (int)(cycles / units));
}

int main() {
    printf("\nmdec/mdec-time\n");
    printf("%d macroblocks (or mono blocks) in one command, DMA in and out, Timer 2. No assertions - compare against your emulator.\n\n", MB);
    mdec_reset();
    mdec_quantTable(quant, true);
    mdec_idctTable(idct);
    const uint16_t prevT2 = initTimer(2, 2);              /* the system clock / 8 */
    EnterCriticalSection();
    for (int dense = 0; dense < 2; dense++) {
        const int colourWords = build(dense, MB, 6);
        const uint32_t t15 = decode(3, colourWords, MB * 128);
        const uint32_t t24 = decode(2, colourWords, MB * 192);
        const int monoWords = build(dense, MB, 1);
        const uint32_t t8 = decode(1, monoWords, MB * 16);
        printf("%s (input %d words colour, %d mono)\n", dense ? "dense: DC + 62 AC a block" : "DC only", colourWords, monoWords);
        report("15 bpp, per macroblock", MB, t15);
        report("24 bpp, per macroblock", MB, t24);
        report("8-bit mono, per block", MB, t8);
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    mdec_reset();
    printf("\nmdec/mdec-time: Done.\n");
    for (;;) VSync(0);
}
