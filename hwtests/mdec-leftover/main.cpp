/*
 * mdec/mdec-leftover - what the MDEC does with output words DMA1 never takes, at the end of a command and at the start
 * of the next.
 *
 * WHY (console day, 2026-10-05). mdec-time on the SCPH-5501 and SCPH-7501 has the MDEC decoding a block in 512 cycles
 * with the output transfer UNDER the next block's decode. A release clock that runs on that way, tried here the same
 * night (STATUS "The MDEC's 512 cycles a block"), let a command end with DMA1's count spent mid-quadrant and 32 words
 * left in the output FIFO; the next command's DMA1 never started, and five games' movies stopped. What a console does
 * with such words - drops them at the next command, delivers them first, or holds the next command off - is
 * unmeasured, and the model needs it before its decoder may run ahead of DMA1.
 *
 * WHAT IT DOES. Two colour macroblocks at 15 bpp (128 words out each), every block DC only (the first macroblock DC
 * 100, the second DC 200, so their words differ), fed by DMA0 and taken by DMA1 told to take 32 words FEWER than the
 * command makes. Then:
 *   A  the status, and every word the CPU can still read (while the status says one is there; at most 256);
 *   B  the same command again, and with the 32 left IN the FIFO a third command of ONE macroblock (DC 300) taken whole
 *      by DMA1 - its first and last words, against what that macroblock decodes to alone (C);
 *   C  that one macroblock from a reset decoder, taken whole: the reference.
 * Every wait is bounded; the status words are printed whole.
 *
 * READ IT LIKE THIS. A's count is what a short DMA1 leaves behind. B's words equal to C's: the next command dropped
 * the leftovers (or a reset at its start did); B's first words A's last ones: they came out first; a B that never
 * finishes: the leftovers held it off.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-leftover`. ON THIS CORE AT v0.362: C done, 7DFFh
 *   throughout; A done (status 2E04FFFFh after), the CPU reads 32 more words (7E1Fh, the second macroblock's last
 *   quadrant), status 8604FFFFh; B done, but 80 of 128 words equal to C, its first words never written (DEADBEEFh) and
 *   status 2E04FFFFh after: this core DELIVERS the leftovers into the next command, shifted.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>

#define DMA0_MADR 0x1F801080
#define DMA0_BCR  0x1F801084
#define DMA0_CHCR 0x1F801088
#define DMA1_MADR 0x1F801090
#define DMA1_BCR  0x1F801094
#define DMA1_CHCR 0x1F801098
#define DPCR      0x1F8010F0

static uint32_t in[64] __attribute__((aligned(16)));
static uint32_t out[512] __attribute__((aligned(16)));

static bool waitClear(uint32_t bit) { for (int i = 0; i < 1000000; i++) if (!(read32(MDEC_STATUS) & bit)) return true; return false; }
static void command(uint32_t w) { waitClear(1u << 29); write32(MDEC_CMD, w); }
static void data(uint32_t w) { waitClear(1u << 30); write32(MDEC_CMD, w); }
static void fresh() {
    write32(MDEC_CTRL, 0x80000000u);
    command(0x40000001u);
    for (int i = 0; i < 32; i++) data(quant[i * 4] | (quant[i * 4 + 1] << 8) | (quant[i * 4 + 2] << 16) | ((uint32_t)quant[i * 4 + 3] << 24));
    command(0x60000000u);
    for (int i = 0; i < 32; i++) data((uint16_t)idct[i * 2] | ((uint32_t)(uint16_t)idct[i * 2 + 1] << 16));
}

/* `mbs` macroblocks of six DC-only blocks each (DC dc0, dc0 + 100, ...), padded with FE00 to 32 words. */
static void build(int mbs, int dc0) {
    uint16_t* hw = (uint16_t*)in;
    int n = 0;
    for (int m = 0; m < mbs; m++) for (int b = 0; b < 6; b++) { hw[n++] = (uint16_t)((dc0 + 100 * m) & 0x3FF); hw[n++] = 0xFE00; }
    while (n < 64) hw[n++] = 0xFE00;
}

/* One command of 32 words at 15 bpp, DMA0 in, DMA1 out `outWords` (a multiple of 32). Returns whether DMA1 finished. */
static bool decode(int outWords) {
    write32(MDEC_CTRL, (1u << 30) | (1u << 29));                /* DMA in and out enabled */
    write32(DPCR, read32(DPCR) | 0x88);
    write32(DMA1_MADR, (uint32_t)out & 0xFFFFFF);
    write32(DMA1_BCR, ((outWords / 32) << 16) | 32);
    write32(DMA1_CHCR, 0x01000200);
    write32(DMA0_MADR, (uint32_t)in & 0xFFFFFF);
    write32(DMA0_BCR, (1 << 16) | 32);
    command((1u << 29) | (3u << 27) | 32);                        /* decode, 15 bpp, 32 words */
    write32(DMA0_CHCR, 0x01000201);
    for (int i = 0; i < 2000000; i++) if (!(read32(DMA1_CHCR) & 0x01000000)) return true;
    write32(DMA0_CHCR, read32(DMA0_CHCR) & ~0x01000000u);
    write32(DMA1_CHCR, read32(DMA1_CHCR) & ~0x01000000u);
    return false;
}

static void showWords(const char* name, const uint32_t* w, int n) {
    printf("  %s:", name);
    for (int i = 0; i < n; i++) printf(" %08x", (int)w[i]);
    printf("\n");
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-leftover\n");
    printf("Output words DMA1 never takes, at a command's end and the next one's start. No assertions.\n\n");

    /* C: the reference - one macroblock DC 300 from a reset decoder, taken whole */
    static uint32_t ref[128];
    fresh(); build(1, 300);
    const bool cOk = decode(128);
    for (int i = 0; i < 128; i++) ref[i] = out[i];
    printf("C  one macroblock DC 300, reset, DMA1 128 words: %s, status after %08x\n", cOk ? "done" : "NOT DONE", (int)read32(MDEC_STATUS));
    showWords("first 4", ref, 4);
    showWords("last 4 ", ref + 124, 4);

    /* A: two macroblocks, DMA1 32 short */
    fresh(); build(2, 100);
    const bool aOk = decode(256 - 32);
    printf("\nA  two macroblocks (DC 100, 200), DMA1 224 of 256 words: %s, status after %08x\n", aOk ? "done" : "NOT DONE", (int)read32(MDEC_STATUS));
    showWords("DMA1's last 4", out + 220, 4);
    static uint32_t left[256];
    int n = 0;
    for (int idle = 0; idle < 1000000 && n < 256; idle++) {
        const uint32_t s = read32(MDEC_STATUS);
        if (!(s & (1u << 31))) { left[n++] = read32(MDEC_DATA); idle = 0; }
        else if (!(s & (1u << 29))) break;
    }
    printf("   the CPU read %d more words, status after %08x\n", n, (int)read32(MDEC_STATUS));
    if (n) { showWords("first 4", left, n < 4 ? n : 4); if (n > 4) showWords("last 4 ", left + n - 4, 4); }

    /* B: the same short command, then - leftovers NOT drained - one macroblock DC 300 taken whole */
    fresh(); build(2, 100);
    const bool b1 = decode(256 - 32);
    const uint32_t sMid = read32(MDEC_STATUS);
    build(1, 300);
    for (int i = 0; i < 128; i++) out[i] = 0xDEADBEEF;
    const bool b2 = decode(128);
    printf("\nB  the short command (%s, status %08x), then one macroblock DC 300 with nothing drained: %s, status after %08x\n",
           b1 ? "done" : "NOT DONE", (int)sMid, b2 ? "done" : "NOT DONE", (int)read32(MDEC_STATUS));
    showWords("first 4", out, 4);
    showWords("last 4 ", out + 124, 4);
    int same = 0;
    for (int i = 0; i < 128; i++) if (out[i] == ref[i]) same++;
    printf("   %d of 128 words equal to C\n", same);

    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-leftover: Done.\n");
    for (;;) VSync(0);
}
