/*
 * mdec/mdec-mono-out - everything a console's MDEC hands out for one small command, and the IDCT's rounding asked
 * again with the decoder reset before every block.
 *
 * WHY (console day, 2026-10-05, the SCPH-5501). mdec-round read its blocks back by the CPU, and after each block's
 * 16 words the MDEC still had output: 496 more words after a one-word DC block, 480 after a two-word AC block, and
 * the status idle only once they were read (8204FFFFh). From its second AC block on, the first 16 words were not
 * that block (all 80h, or bytes no rule gives) while rows of it turned up in the words after. Its six DC-only blocks
 * came first and clean: C (MiSTer's floor) exact on all six, A (this emulator, M224) wrong on the negative ones -
 * and its first AC block read A, not C. The AC blocks end with a padding FE00h after their end code, which nugget
 * (b7d9561, STATUS M236) found can stall a console's MDEC. So two questions: what is the stream, and what does a
 * clean decode of each block give?
 *
 * WHAT IT DOES. Everything by CPU writes and reads; every output word is read while the status says the out FIFO
 * holds one, until it is empty and not busy (or 4,096 words, or ~1M status reads with neither).
 *   STREAM: from a reset decoder (both tables sent again), one command each: a DC block [DC, FE00] at 8 and 4 bpp;
 *     two DC blocks in one command; mdec-round's block 7 as it sent it (DC, AC, FE00, FE00), then the same once
 *     more with NO reset between; that block with a zero AC code (0000h: run 0, level 0 - a coefficient that adds
 *     nothing) in place of the padding FE00; a DC block padded with FE00 to a 32-word command, as a DMA feed is; a
 *     15 bpp macroblock of six DC blocks. Each: the words out, against one block's worth, and the stream as runs
 *     of one value, [first-last].
 *   ROUND / QBIAS: mdec-round's 16 blocks and mdec-qbias's 11, each from a reset decoder, the padding FE00 replaced
 *     by a zero AC code; each block's first 16 words graded against the rules those tests predict, printed whole,
 *     and the words after them counted.
 *
 * READ IT LIKE THIS. A console whose STREAM rows say one block's worth with no runs past it does what this
 * emulator does; more words, or the block's rows somewhere other than the first 16, are the layout to model.
 * ROUND's bytes decide the IDCT's rounding (A rounds once, B each pass, C floors once) on blocks nothing went
 * before; QBIAS's the dequantiser's half step (8, 5, 0 - read against ROUND's rule if it is not A).
 *
 * ON THIS CORE AT v0.361: every STREAM case one block's worth (16, 8, 32, 16, 16 again with no reset, 16, 16 and
 * 128 words) and the status idle after; ROUND A on all 16 (B 2, C 4) and QBIAS 8 on all 11 (5 and 0 on 2) - the
 * readings of mdec-round and mdec-qbias themselves, so the zero AC code in place of the padding FE00 changes no byte
 * here. Run with `node hwtests/run.js mdec-mono-out`.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>
#include "rblocks.h"
#include "qblocks.h"

static bool waitClear(uint32_t bit) { for (int i = 0; i < 1000000; i++) if (!(read32(MDEC_STATUS) & bit)) return true; return false; }
static void command(uint32_t w) { waitClear(1u << 29); write32(MDEC_CMD, w); }
static void data(uint32_t w) { waitClear(1u << 30); write32(MDEC_CMD, w); }

/* Reset (abort, status 80040000h) and both tables again, as mdec_quantTable / mdec_idctTable send them, unlogged. */
static void fresh() {
    write32(MDEC_CTRL, 0x80000000u);
    command(0x40000001u);
    for (int i = 0; i < 32; i++) data(quant[i * 4] | (quant[i * 4 + 1] << 8) | (quant[i * 4 + 2] << 16) | ((uint32_t)quant[i * 4 + 3] << 24));
    command(0x60000000u);
    for (int i = 0; i < 32; i++) data((uint16_t)idct[i * 2] | ((uint32_t)(uint16_t)idct[i * 2 + 1] << 16));
}

static uint32_t outBuf[4096];
static int nOut;
static uint32_t endStatus;
/* One decode command of `halfwords` (even) halfwords at depth (0 4-bit, 1 8-bit, 2 24-bit, 3 15-bit), unsigned; then
 * every word out. */
static void decode(const uint16_t* hw, int halfwords, int depth) {
    const int words = halfwords / 2;
    command((1u << 29) | ((uint32_t)depth << 27) | (uint32_t)words);
    for (int i = 0; i < words; i++) data(hw[2 * i] | ((uint32_t)hw[2 * i + 1] << 16));
    nOut = 0;
    for (int idle = 0; idle < 1000000 && nOut < 4096; idle++) {
        const uint32_t s = read32(MDEC_STATUS);
        if (!(s & (1u << 31))) { outBuf[nOut++] = read32(MDEC_DATA); idle = 0; }
        else if (!(s & (1u << 29))) break;
    }
    endStatus = read32(MDEC_STATUS);
}

static void showStream(const char* name, int expect) {
    printf("%-44s %4d words out (one block's worth %3d), status after %08x\n", name, nOut, expect, (int)endStatus);
    int runs = 0;
    for (int i = 0; i < nOut;) {
        int j = i;
        while (j + 1 < nOut && outBuf[j + 1] == outBuf[i]) j++;
        if (runs < 24) printf("    [%4d-%4d] %08x\n", i, j, (int)outBuf[i]);
        runs++;
        i = j + 1;
    }
    if (runs > 24) printf("    ... %d runs in all\n", runs);
}

static int match(const uint8_t* out, const uint8_t* want) {
    int n = 0;
    for (int i = 0; i < 64; i++) if (out[i] == want[i]) n++;
    return n;
}
static void showBytes() {
    const uint8_t* out = (const uint8_t*)outBuf;
    printf("     ");
    for (int i = 0; i < 64; i++) printf("%02x%s", out[i], (i & 7) == 7 ? (i == 63 ? "\n" : " ") : "");
}
/* A table block's halfwords with the padding FE00 (its fourth, after the end code) made a zero AC code. */
static void clean(const uint16_t* rle, int words, uint16_t* hw) {
    for (int i = 0; i < 4; i++) hw[i] = rle[i];
    if (words == 2 && rle[2] == 0xFE00 && rle[3] == 0xFE00) { hw[2] = 0x0000; hw[3] = 0xFE00; }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-mono-out\n");
    printf("What the MDEC hands out for one small command, read by the CPU to the end; then two tests' blocks, each from\n");
    printf("a reset decoder. No assertions - compare against your emulator.\n\n");

    printf("STREAM\n");
    static const uint16_t dcBlock[2] = { 0x0340, 0xFE00 };                       /* q 0, DC -192: bytes 50h */
    static const uint16_t twoDc[4] = { 0x0340, 0xFE00, 0x0082, 0xFE00 };          /* DC -192, then DC 130 */
    static const uint16_t acPad[4] = { 0x03ba, 0x07cd, 0xFE00, 0xFE00 };          /* mdec-round's block 7, as sent */
    static const uint16_t acZero[4] = { 0x03ba, 0x07cd, 0x0000, 0xFE00 };
    static uint16_t dc32[64];
    dc32[0] = 0x0340; for (int i = 1; i < 64; i++) dc32[i] = 0xFE00;
    static const uint16_t mb15[12] = { 0, 0xFE00, 0, 0xFE00, 0, 0xFE00, 0, 0xFE00, 0, 0xFE00, 0, 0xFE00 };
    fresh(); decode(dcBlock, 2, 1);  showStream("8 bpp, DC block [0340 FE00]", 16);
    fresh(); decode(dcBlock, 2, 0);  showStream("4 bpp, DC block [0340 FE00]", 8);
    fresh(); decode(twoDc, 4, 1);    showStream("8 bpp, two DC blocks in one command", 32);
    fresh(); decode(acPad, 4, 1);    showStream("8 bpp, AC block [03ba 07cd FE00 FE00]", 16);
             decode(acPad, 4, 1);    showStream("  the same again, no reset between", 16);
    fresh(); decode(acZero, 4, 1);   showStream("8 bpp, AC block [03ba 07cd 0000 FE00]", 16);
    fresh(); decode(dc32, 64, 1);    showStream("8 bpp, DC block padded FE00 to 32 words", 16);
    fresh(); decode(mb15, 12, 3);    showStream("15 bpp, six DC blocks [0000 FE00]", 128);

    printf("\nROUND (mdec-round's blocks: q_scale 0; A rounds once, B each pass, C floors once)\n");
    int allR[3] = { 0, 0, 0 };
    for (int b = 0; b < NRB; b++) {
        const RBlock& blk = rblocks[b];
        uint16_t hw[4];
        clean(blk.rle, blk.words, hw);
        fresh(); decode(hw, blk.words * 2, 1);
        const uint8_t* out = (const uint8_t*)outBuf;
        const int a = nOut >= 16 ? match(out, blk.a) : 0, bb = nOut >= 16 ? match(out, blk.b) : 0, c = nOut >= 16 ? match(out, blk.c) : 0;
        if (a == 64) allR[0]++;
        if (bb == 64) allR[1]++;
        if (c == 64) allR[2]++;
        printf("  %2d %-7s dc %4d  AC %2d = %4d   A %2d/64  B %2d/64  C %2d/64   %d words out\n", b, blk.role, blk.dc, blk.k, blk.v, a, bb, c, nOut);
        showBytes();
    }
    printf("  blocks each rule gives exactly: A %d, B %d, C %d of %d\n", allR[0], allR[1], allR[2], NRB);

    printf("\nQBIAS (mdec-qbias's blocks: q_scale 1-4; the dequantiser's half step 8, 5 or 0, under this emulator's IDCT)\n");
    int allQ[3] = { 0, 0, 0 };
    for (int b = 0; b < NQB; b++) {
        const QBlock& blk = qblocks[b];
        uint16_t hw[4];
        clean(blk.rle, blk.words, hw);
        fresh(); decode(hw, blk.words * 2, 1);
        const uint8_t* out = (const uint8_t*)outBuf;
        const int m8 = nOut >= 16 ? match(out, blk.b8) : 0, m5 = nOut >= 16 ? match(out, blk.b5) : 0, m0 = nOut >= 16 ? match(out, blk.b0) : 0;
        if (m8 == 64) allQ[0]++;
        if (m5 == 64) allQ[1]++;
        if (m0 == 64) allQ[2]++;
        printf("  %2d %-7s dc %4d  AC %2d = %4d  q %d   8: %2d/64  5: %2d/64  0: %2d/64   %d words out\n", b, blk.role, blk.dc, blk.k, blk.v, blk.qs, m8, m5, m0, nOut);
        showBytes();
    }
    printf("  blocks each half step gives exactly: 8 %d, 5 %d, 0 %d of %d\n", allQ[0], allQ[1], allQ[2], NQB);

    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-mono-out: Done.\n");
    for (;;) VSync(0);
}
