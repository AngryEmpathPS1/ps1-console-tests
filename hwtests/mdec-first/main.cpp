/*
 * mdec/mdec-first - what sets the time of the MDEC's first output word: the stream's first 32-word chunk, the command's length or
 * DMA0's block size (2026-11-16; STATUS "mdecstream", the MDEC session).
 *
 * WHY. hwtests/mdec-time2 on six consoles: the first DMA1 block (OUT1) of a 24 bpp command ends ~1,760 cycles after the command for sparse
 * and some dense streams and ~1,855 for mid streams (and dense at 6 and 24 macroblocks); at DMA0 bs 8 / 16 / 32 on one mid stream 1,795 /
 * 1,853 / 1,855. The word-clock knob (experiments/land_mdecstream.js) starts its timeline at the 32nd input word and gets that the other
 * way round. This splits the candidates: content (the first chunk's blocks), the command's length / DMA0's block count, DMA0's block size.
 *
 * WHAT IT DOES. As mdec-time2's A rows (Timer 2 at the system clock; DMA1 armed before the command, bs 32; DMA0 started right after the
 * command; the poll stamps DMA0's end IN, DMA1's first MADR step OUT1 and DMA1's end END). Blocks: sparse (DC + end, 2 halfwords), mid
 * (DC, 14 AC, end: 16), dense (DC, 62 AC, end: 64). Rows:
 *   N   one content, N macroblocks, DMA0 bs 32 (N such that the stream is whole 32-word blocks)
 *   BS  mid / dense, 6 macroblocks, DMA0 bs 8 / 16 / 32
 *   MIX the first macroblock dense, the rest mid (and the first mid, the rest dense), 8 macroblocks, bs 32 where it divides
 * at 24 and 15 bpp. No FE00h padding word is sent (mdec-pad).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>
#include <mdec.h>

#define MAX_IN_WORDS  32760
#define MAX_OUT_WORDS (64 * 192)
#define DMA0_MADR 0x1F801080
#define DMA0_BCR  0x1F801084
#define DMA0_CHCR 0x1F801088
#define DMA1_MADR 0x1F801090
#define DMA1_BCR  0x1F801094
#define DMA1_CHCR 0x1F801098
#define DPCR      0x1F8010F0
#define LIMIT     20000000u

static uint32_t in[MAX_IN_WORDS];
static uint32_t out[MAX_OUT_WORDS];
enum { SPARSE = 0, MID = 1, DENSE = 2 };
static const char* const KIND[] = { "sparse", "mid", "dense" };

static int block(uint16_t* hw, int kind, int seed) {
    int n = 0;
    hw[n++] = (1 << 10) | ((seed * 37) & 0xFF);
    if (kind == MID) for (int k = 0; k < 14; k++) hw[n++] = ((k & 3) << 10) | ((((k + seed) % 7) - 3) & 0x3FF);
    if (kind == DENSE) for (int k = 1; k <= 62; k++) hw[n++] = (0 << 10) | ((((k + seed) & 3) - 1) & 0x3FF);
    hw[n++] = 0xFE00;
    return n;
}
/* units macroblocks of six blocks; the first `firstN` of kind k0, the rest of kind k1; returns words */
static int build(int k0, int firstN, int k1, int units) {
    uint16_t* hw = (uint16_t*)in;
    int n = 0;
    for (int u = 0; u < units; u++) for (int b = 0; b < 6; b++) n += block(hw + n, u < firstN ? k0 : k1, u * 7 + b);
    return n / 2;
}

static uint32_t clk; static uint16_t last;
static inline uint32_t now() { const uint16_t t = readTimer(2); clk += (uint16_t)(t - last); last = t; return clk; }
static inline void zero() { clk = 0; last = readTimer(2); }

struct Times { uint32_t in, out1, end; bool bad; };
static Times run(int depth, int inWords, int bs0, int outWords) {
    Times t = { 0, 0, 0, false };
    write32(0x1F801824, 1u << 31); read32(0x1F801824); read32(0x1F801824); read32(0x1F801824);
    write32(0x1F801824, (1u << 30) | (1u << 29));
    write32(DPCR, read32(DPCR) | 0x88);
    write32(DMA1_MADR, (uint32_t)out & 0xFFFFFF);
    write32(DMA1_BCR, ((outWords / 32) << 16) | 32);
    write32(DMA1_CHCR, 0x01000200);
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
        if (c > LIMIT) { write32(DMA0_CHCR, 0); write32(DMA1_CHCR, 0); write32(0x1F801824, 1u << 31); t.bad = true; return t; }
    }
    if (!t.in) t.in = t.end;
    if (!t.out1) t.out1 = t.end;
    return t;
}
static void row(const char* name, int units, const Times& t) {
    if (t.bad) { printf("  %-34s STUCK\n", name); return; }
    printf("  %-34s IN %7d  OUT1 %7d  END %7d  per %5d\n", name, (int)t.in, (int)t.out1, (int)t.end, (int)(t.end / units));
}
static int wordsOut(int depth) { return depth == 2 ? 192 : 128; }

int main() {
    printf("\nmdec/mdec-first\n");
    printf("What sets the MDEC's first output word: the first chunk, the command's length, DMA0's block size (Timer 2, system clock). No assertions.\n\n");
    mdec_reset();
    mdec_quantTable(quant, true);
    mdec_idctTable(idct);
    const uint16_t prevT2 = initTimer(2, 0);
    EnterCriticalSection();
    char name[64];
    for (int d = 0; d < 2; d++) {
        const int depth = d ? 3 : 2;
        const char* dn = depth == 2 ? "24bpp" : "15bpp";
        printf("%s N: one content, N macroblocks, DMA0 bs 32\n", dn);
        static const int NM[] = { 2, 4, 6, 8, 12, 24 };
        static const int ND[] = { 1, 2, 4, 6, 12, 24 };
        static const int NS[] = { 16, 32, 64 };
        for (int i = 0; i < 6; i++) { const int n = NM[i], w = build(MID, 0, MID, n); const Times t = run(depth, w, 32, n * wordsOut(depth)); sprintf(name, "%s mid %2d MB in %d", dn, n, w); row(name, n, t); }
        for (int i = 0; i < 6; i++) { const int n = ND[i], w = build(DENSE, 0, DENSE, n); const Times t = run(depth, w, 32, n * wordsOut(depth)); sprintf(name, "%s dense %2d MB in %d", dn, n, w); row(name, n, t); }
        for (int i = 0; i < 3; i++) { const int n = NS[i], w = build(SPARSE, 0, SPARSE, n); const Times t = run(depth, w, 32, n * wordsOut(depth)); sprintf(name, "%s sparse %2d MB in %d", dn, n, w); row(name, n, t); }
        printf("%s BS: 6 macroblocks, DMA0 bs 8 / 16 / 32\n", dn);
        static const int BS[] = { 8, 16, 32 };
        for (int k = 1; k <= 2; k++) for (int b = 0; b < 3; b++) { const int w = build(k, 0, k, 6); const Times t = run(depth, w, BS[b], 6 * wordsOut(depth)); sprintf(name, "%s %s 6 MB DMA0 bs %d", dn, KIND[k], BS[b]); row(name, 6, t); }
        printf("%s MIX: the first macroblock of one kind, the rest of another, 8 macroblocks, DMA0 bs 32\n", dn);
        { const int w = build(DENSE, 1, MID, 7); const Times t = run(depth, w, 32, 7 * wordsOut(depth)); sprintf(name, "%s 1 dense + 6 mid in %d", dn, w); row(name, 7, t); }
        { const int w = build(MID, 2, DENSE, 8); const Times t = run(depth, w, 32, 8 * wordsOut(depth)); sprintf(name, "%s 2 mid + 6 dense in %d", dn, w); row(name, 8, t); }
        { const int w = build(SPARSE, 16, MID, 24); const Times t = run(depth, w, 32, 24 * wordsOut(depth)); sprintf(name, "%s 16 sparse + 8 mid in %d", dn, w); row(name, 24, t); }
        { const int w = build(MID, 2, SPARSE, 18); const Times t = run(depth, w, 32, 18 * wordsOut(depth)); sprintf(name, "%s 2 mid + 16 sparse in %d", dn, w); row(name, 18, t); }
        printf("\n");
    }
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    mdec_reset();
    printf("mdec/mdec-first: Done.\n");
    for (;;) VSync(0);
}
