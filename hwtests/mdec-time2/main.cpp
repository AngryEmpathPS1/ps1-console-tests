/*
 * mdec/mdec-time2 - one picture's MDEC decode timed end to end, split into in-transfer, decode and out-transfer,
 * by size, depth, run-level density, DMA block sizes, and a CPU gap between DMA1 slices.
 *
 * WHY (2026-11-13; STATUS "The Doom probe", "mdec-time2"). Doom (USA)'s intro movie on the second SCPH-5501 waits for the
 * MDEC's picture 24 % longer than this core (v0.377.348): 458,502 against 370,213 cycles a frame. hwtests/mdec-time timed only
 * DC-only blocks through one unbroken DMA1 (3,073 a macroblock on three consoles = src/psx.js MDEC_BLOCK_TICKS 512); its
 * dense colour rows never ran (the 15-bit input counter). Doom's player (SLUS_000.77, profiled on this core) sends a frame as
 * ONE 24 bpp command (DMA0 bs 32) and takes the output in sixteen-pixel columns: DMA1 15 macroblocks (bs 32 x 90), then the
 * CPU uploads the column (DMA2) and restarts DMA1 ~5,600 cycles after its end. This core's decoder runs a macroblock
 * ahead while DMA1 is off; how far a console's runs ahead is what part B measures.
 *
 * WHAT IT DOES. Timer 2 at the system clock (wraps counted in the poll). Every run: reset, enables, both channels armed,
 * the command, DMA0 started; then a poll that stamps DMA0's end (IN), DMA1's first word out (OUT1: MADR moved) and DMA1's
 * end (END). Streams are whole numbers of DMA0 blocks; no FE00h padding word is ever sent (mdec-pad: padding ends a
 * command a block out of step). Contents, per 8x8 block:
 *   sparse  the DC and the end code (2 halfwords)
 *   mid     the DC, 14 AC (run 0..3) and the end code (16 halfwords) - about a movie's
 *   dense   the DC, 62 AC at run 0 and the end code (64 halfwords)
 * A  whole pictures: 1, 6, 24, 240 macroblocks (dense 240 is over the 15-bit word count: 160), 15 / 24 bpp and 8-bit
 *    mono (N blocks), DMA0 and DMA1 bs 32.
 * B  Doom's pattern: 30 macroblocks, mid, 24 and 15 bpp, DMA1 in two slices of 15; after the first slice ends the CPU
 *    waits G cycles, reads the MDEC status, starts the second. Slice 2's time against G says how far the decoder ran ahead.
 *    Then 30 one-macroblock slices with G 3,000.
 * C  DMA1's block size: 24 macroblocks, mid, bs 4 / 8 / 16 / 32 / 64 (and 48 at 24 bpp).
 * D  DMA0's block size: 24 macroblocks, mid, 24 bpp, bs 8 / 16 / 32.
 * E  out-transfer alone: one macroblock decoded with DMA1 off, then DMA1 started D cycles later.
 * Every wait has a 20M-cycle limit; a stuck run prints the MDEC status and both channels, stops them and resets the MDEC.
 *
 * READ IT LIKE THIS. Cycles from the command write. IN = DMA0 done, OUT1 = first DMA1 word in RAM, END = DMA1 done;
 * "per" = END / macroblocks (mono: / blocks). The poll's resolution is its loop, ~20-30 cycles.
 *
 * ANSWERED (2026-11-13, the SCPH-7502 and the PSone SCPH-101, two runs each, all four logs identical to the cycle; STATUS
 * "mdec-time2"): 3,072 a macroblock at every density and both depths, END = 3,072 N + ~420, the first word out at ~1,800 (24 bpp)
 * / ~1,950 (15 bpp); with DMA1 off the decoder banks ~1,500 / ~1,770 cycles of work, not a macroblock (B2: 1,575 / 1,300 a
 * slice after the gap; v0.377.348 read 55). Not modelled, measured: a DMA0 block size that does not divide 32 (18, 24, 30) loses
 * words on the consoles (STUCK, the MDEC still wanting 4-112 words, DMA0 done), and DMA1 at bs 48 / 64 ends after ~516 cycles a
 * block whatever the depth (C: END 49,574 / 37,265), reading words the decoder has not made; E needs a DMA0 bs that divides 32.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>
#include <mdec.h>

#define MAXMB 240
#define MAX_IN_WORDS  32760
#define MAX_OUT_WORDS (MAXMB * 192)
#define DMA0_MADR 0x1F801080
#define DMA0_BCR  0x1F801084
#define DMA0_CHCR 0x1F801088
#define DMA1_MADR 0x1F801090
#define DMA1_BCR  0x1F801094
#define DMA1_CHCR 0x1F801098
#define DPCR      0x1F8010F0
#define MDEC_STAT 0x1F801824
#define LIMIT     20000000u

static uint32_t in[MAX_IN_WORDS];
static uint32_t out[MAX_OUT_WORDS];

enum { SPARSE = 0, MID = 1, DENSE = 2 };
static const char* const KIND[] = { "sparse", "mid", "dense" };

/* One block's run-level code into hw; returns halfwords (even). */
static int block(uint16_t* hw, int kind, int seed) {
    int n = 0;
    hw[n++] = (1 << 10) | ((seed * 37) & 0xFF);                                   /* q_scale 1, a DC */
    if (kind == MID) for (int k = 0; k < 14; k++) hw[n++] = ((k & 3) << 10) | ((((k + seed) % 7) - 3) & 0x3FF);   /* runs 0..3: 14 + 1.5 x 14 = 35 < 63 */
    if (kind == DENSE) for (int k = 1; k <= 62; k++) hw[n++] = (0 << 10) | ((((k + seed) & 3) - 1) & 0x3FF);
    hw[n++] = 0xFE00;
    return n;
}
static int build(int kind, int units, int blocksPer) {
    uint16_t* hw = (uint16_t*)in;
    int n = 0;
    for (int u = 0; u < units; u++) for (int b = 0; b < blocksPer; b++) n += block(hw + n, kind, u * 7 + b);
    return n / 2;
}
static int bsFor(int words, int want) { for (int b = want; b > 1; b--) if (words % b == 0) return b; return 1; }

/* the clock: Timer 2 at the system clock, wraps counted by each read */
static uint32_t clk;
static uint16_t last;
static inline uint32_t now() { const uint16_t t = readTimer(2); clk += (uint16_t)(t - last); last = t; return clk; }
static inline void zero() { clk = 0; last = readTimer(2); }
static void waitCycles(uint32_t g) { const uint32_t t0 = now(); while (now() - t0 < g) {} }

static uint32_t stuckSt;
static void stuck(const char* where) {
    const uint32_t st = read32(MDEC_STAT), c0 = read32(DMA0_CHCR), b0 = read32(DMA0_BCR), c1 = read32(DMA1_CHCR), b1 = read32(DMA1_BCR);
    write32(DMA0_CHCR, c0 & ~0x01000000u);
    write32(DMA1_CHCR, c1 & ~0x01000000u);
    mdec_ctrl(1u << 31);
    printf("    STUCK at %s: MDEC %08x DMA0 %08x/%08x DMA1 %08x/%08x\n", where, (int)st, (int)c0, (int)b0, (int)c1, (int)b1);
    stuckSt = st;
}

static void setup() {
    mdec_ctrl(1u << 31);
    read32(MDEC_STAT); read32(MDEC_STAT); read32(MDEC_STAT);                       /* past the reset's 7 deaf cycles */
    mdec_ctrl((1u << 30) | (1u << 29));
    write32(DPCR, read32(DPCR) | 0x88);
}
static void arm1(uint32_t addr, int words, int bs) {
    write32(DMA1_MADR, addr & 0xFFFFFF);
    write32(DMA1_BCR, ((words / bs) << 16) | bs);
    write32(DMA1_CHCR, 0x01000200);
}

struct Times { uint32_t in, out1, end; bool bad; };

/* One command, DMA1 armed for all of it. depth 1 8-bit, 2 24-bit, 3 15-bit. */
static Times run(int depth, int inWords, int bs0, int outWords, int bs1) {
    Times t = { 0, 0, 0, false };
    setup();
    arm1((uint32_t)out, outWords, bs1);
    write32(DMA0_MADR, (uint32_t)in & 0xFFFFFF);
    write32(DMA0_BCR, ((inWords / bs0) << 16) | bs0);
    const uint32_t m0 = (uint32_t)out & 0xFFFFFF;
    zero();
    write32(MDEC_CMD, (1u << 29) | (depth << 27) | (inWords & 0xFFFF));
    write32(DMA0_CHCR, 0x01000201);
    for (;;) {
        const uint32_t c = now();
        if (!t.in && !(read32(DMA0_CHCR) & 0x01000000)) t.in = c;
        if (!t.out1 && read32(DMA1_MADR) != m0) t.out1 = c;
        if (!(read32(DMA1_CHCR) & 0x01000000)) { t.end = c; break; }
        if (c > LIMIT) { stuck("run"); t.bad = true; return t; }
    }
    if (!t.in) t.in = t.end;
    if (!t.out1) t.out1 = t.end;
    return t;
}

static void row(const char* name, int units, const Times& t) {
    if (t.bad) { printf("  %-34s STUCK\n", name); return; }
    printf("  %-34s IN %7d  OUT1 %7d  END %7d  per %5d\n", name, (int)t.in, (int)t.out1, (int)t.end, (int)(t.end / units));
}

static int wordsOut(int depth) { return depth == 2 ? 192 : depth == 3 ? 128 : 16; }   /* 8-bit mono: one block, 64 bytes */

int main() {
    printf("\nmdec/mdec-time2\n");
    printf("MDEC decode time end to end (Timer 2, system clock). No assertions - compare against your emulator.\n\n");
    mdec_reset();
    mdec_quantTable(quant, true);
    mdec_idctTable(idct);
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    char name[64];

    /* A: whole pictures */
    printf("A: whole pictures, DMA0/DMA1 bs 32 (bs0 shown when a stream is not a multiple of 32)\n");
    static const int NS[] = { 1, 6, 24, 240 };
    for (int kind = 0; kind < 3; kind++) {
        for (int ni = 0; ni < 4; ni++) {
            int n = NS[ni];
            if (kind == DENSE && n == 240) n = 160;
            const int w = build(kind, n, 6), b0 = bsFor(w, 32);
            for (int d = 0; d < 2; d++) {
                const int depth = d ? 2 : 3;
                const Times t = run(depth, w, b0, n * wordsOut(depth), 32);
                sprintf(name, "%s %3d MB %s in %d/%d", KIND[kind], n, depth == 2 ? "24bpp" : "15bpp", w, b0);
                row(name, n, t);
            }
        }
        for (int ni = 0; ni < 4; ni++) {
            const int n = NS[ni], w = build(kind, n, 1), b0 = bsFor(w, 32);
            const Times t = run(1, w, b0, n * 16, bsFor(n * 16, 32));
            sprintf(name, "%s %3d blk mono in %d/%d", KIND[kind], n, w, b0);
            row(name, n, t);
        }
    }

    /* B: Doom's pattern - DMA1 in slices, the CPU away between them */
    printf("\nB: 30 MB mid, DMA1 in two slices of 15 MB (bs 32); G cycles between slice 1's end and slice 2's start\n");
    static const uint32_t GS[] = { 0, 500, 1000, 2000, 3000, 4000, 6000, 12000, 50000 };
    for (int d = 0; d < 2; d++) {
        const int depth = d ? 3 : 2, mbw = wordsOut(depth), w = build(MID, 30, 6);
        for (int gi = 0; gi < 9; gi++) {
            const uint32_t G = GS[gi];
            setup();
            arm1((uint32_t)out, 15 * mbw, 32);
            write32(DMA0_MADR, (uint32_t)in & 0xFFFFFF);
            write32(DMA0_BCR, ((w / 32) << 16) | 32);
            zero();
            write32(MDEC_CMD, (1u << 29) | (depth << 27) | (w & 0xFFFF));
            write32(DMA0_CHCR, 0x01000201);
            uint32_t e1 = 0, s2 = 0, e2 = 0, st = 0; bool bad = false;
            while (read32(DMA1_CHCR) & 0x01000000) if (now() > LIMIT) { stuck("B1"); bad = true; break; }
            if (!bad) {
                e1 = now();
                waitCycles(G);
                st = read32(MDEC_STAT);
                arm1((uint32_t)out + 15 * mbw * 4, 15 * mbw, 32);
                s2 = now();
                while (read32(DMA1_CHCR) & 0x01000000) if (now() > LIMIT) { stuck("B2"); bad = true; break; }
                e2 = now();
            }
            if (bad) { printf("  %s G %5d STUCK\n", depth == 2 ? "24bpp" : "15bpp", (int)G); continue; }
            printf("  %s G %5d  slice1 %6d  restart %7d (status %08x)  slice2 %6d  END %7d\n", depth == 2 ? "24bpp" : "15bpp",
                   (int)G, (int)e1, (int)s2, (int)st, (int)(e2 - s2), (int)e2);
        }
    }
    printf("\nB2: 30 MB mid, DMA1 one MB a slice, G 3000 between; slice times\n");
    for (int d = 0; d < 2; d++) {
        const int depth = d ? 3 : 2, mbw = wordsOut(depth), w = build(MID, 30, 6);
        setup();
        write32(DMA0_MADR, (uint32_t)in & 0xFFFFFF);
        write32(DMA0_BCR, ((w / 32) << 16) | 32);
        uint32_t sl[30]; uint32_t sts[30]; bool bad = false;
        arm1((uint32_t)out, mbw, 32);
        zero();
        write32(MDEC_CMD, (1u << 29) | (depth << 27) | (w & 0xFFFF));
        write32(DMA0_CHCR, 0x01000201);
        uint32_t s = 0;
        for (int k = 0; k < 30 && !bad; k++) {
            if (k) { waitCycles(3000); sts[k] = read32(MDEC_STAT); arm1((uint32_t)out + k * mbw * 4, mbw, 32); s = now(); }
            else sts[k] = 0;
            while (read32(DMA1_CHCR) & 0x01000000) if (now() > LIMIT) { stuck("B2"); bad = true; break; }
            sl[k] = now() - s;
        }
        if (bad) { printf("  %s STUCK\n", depth == 2 ? "24bpp" : "15bpp"); continue; }
        printf("  %s END %d; slices:", depth == 2 ? "24bpp" : "15bpp", (int)now());
        for (int k = 0; k < 30; k++) printf(" %d", (int)sl[k]);
        printf("\n  %s status at each restart:", depth == 2 ? "24bpp" : "15bpp");
        for (int k = 1; k < 30; k++) printf(" %08x", (int)sts[k]);
        printf("\n");
    }

    /* C: DMA1 block size */
    printf("\nC: 24 MB mid, DMA1 block size\n");
    static const int BS1[] = { 4, 8, 16, 32, 48, 64 };
    for (int d = 0; d < 2; d++) {
        const int depth = d ? 3 : 2, w = build(MID, 24, 6);
        for (int bi = 0; bi < 6; bi++) {
            const int bs = BS1[bi];
            if ((24 * wordsOut(depth)) % bs) continue;
            const Times t = run(depth, w, 32, 24 * wordsOut(depth), bs);
            sprintf(name, "%s DMA1 bs %d", depth == 2 ? "24bpp" : "15bpp", bs);
            row(name, 24, t);
        }
    }

    /* D: DMA0 block size */
    printf("\nD: 24 MB mid 24bpp, DMA0 block size (DMA1 bs 32)\n");
    static const int BS0[] = { 8, 16, 32 };
    for (int bi = 0; bi < 3; bi++) {
        const int w = build(MID, 24, 6);
        const Times t = run(2, w, BS0[bi], 24 * 192, 32);
        sprintf(name, "24bpp DMA0 bs %d", BS0[bi]);
        row(name, 24, t);
    }

    /* E: out-transfer alone */
    printf("\nE: 1 MB mid, DMA1 started D cycles after the command (DMA1 bs 32): its own time\n");
    static const uint32_t DS[] = { 0, 2000, 4000, 8000, 20000 };
    for (int d = 0; d < 2; d++) {
        const int depth = d ? 3 : 2, mbw = wordsOut(depth), w = build(MID, 1, 6);
        for (int di = 0; di < 5; di++) {
            setup();
            write32(DMA0_MADR, (uint32_t)in & 0xFFFFFF);
            write32(DMA0_BCR, ((w / bsFor(w, 32)) << 16) | bsFor(w, 32));
            zero();
            write32(MDEC_CMD, (1u << 29) | (depth << 27) | (w & 0xFFFF));
            write32(DMA0_CHCR, 0x01000201);
            waitCycles(DS[di]);
            const uint32_t st = read32(MDEC_STAT);
            arm1((uint32_t)out, mbw, 32);
            const uint32_t s = now();
            bool bad = false;
            while (read32(DMA1_CHCR) & 0x01000000) if (now() > LIMIT) { stuck("E"); bad = true; break; }
            if (bad) { printf("  %s D %5d STUCK\n", depth == 2 ? "24bpp" : "15bpp", (int)DS[di]); continue; }
            const uint32_t e = now();
            printf("  %s D %5d  start %6d (status %08x)  DMA1 %6d  END %6d\n", depth == 2 ? "24bpp" : "15bpp", (int)DS[di], (int)s, (int)st, (int)(e - s), (int)e);
        }
    }

    ExitCriticalSection();
    restoreTimer(2, prevT2);
    mdec_reset();
    printf("\nmdec/mdec-time2: Done.\n");
    for (;;) VSync(0);
}
