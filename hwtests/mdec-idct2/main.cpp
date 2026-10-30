/*
 * mdec/mdec-idct2 - the IDCT's arithmetic on AC-rich blocks, seen directly: 48 pseudo-random blocks decoded alone to
 * 8-bit mono, every output byte printed (2026-10-12 night).
 *
 * WHY. v0.377.31's IDCT (a 13-bit table, pass 1 floored >> 14, pass 2's products floored >> 7 and the sum rounded >> 9) was
 * fitted to hwtests/mdec-idct's DC staircase and single-AC blocks and holds there (DC0 1,024 of 1,024; AC 68 of 72).
 * mdec-step24's symbols image (24 bpp, every console byte-identical) still parts from it on 236 bytes in 140 of 768 words
 * - and the colour stage is not the cause: hwtests/mdec-color's flat blocks read exact (4,404 numbers, 3 off), and on
 * the two GREY macroblocks (no chroma: R = G = B = Y + 128) whole pixels' Y is one off at fractions 0.15-0.4 from a
 * rounding edge. So the IDCT itself errs on blocks with several AC coefficients, where no test sees it directly: every
 * variant of the matrix-multiply family (table bits, each pass's shift and rounding, per-product floors, the pass order,
 * the colour stage carrying fractions) was graded against those 768 words (scratch/scratch_mdec24eval.js) and none beats
 * the rule. This prints the IDCT's own output on such blocks, Y alone, for the next search.
 *
 * WHAT IT DOES. Everything by CPU writes and reads (mdec-mono-out's helpers). The standard tables (the common library's
 * quant and idct, sent again from a reset decoder before each block). 48 blocks from a fixed xorshift seed: q_scale
 * 1-2, DC -120..120, 3-12 AC coefficients at runs 0-5 and levels +-1..15 (never 0; small, so few bytes clip at 00h or FFh), the stream padded with a zero AC
 * code before its end code to an even count (mdec-mono-out: a padding FE00 can stall a console's MDEC). Each decoded
 * at 8 bpp unsigned; its 16 words printed as 64 bytes, row by row, after the block's stream.
 *
 * READ IT LIKE THIS. Byte for byte against your emulator's output for the same streams. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-idct2`.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>

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
static uint32_t outBuf[64];
static int nOut;
static void decode(const uint16_t* hw, int halfwords) {
    const int words = halfwords / 2;
    command((1u << 29) | (1u << 27) | (uint32_t)words);         /* 8 bpp, unsigned */
    for (int i = 0; i < words; i++) data(hw[2 * i] | ((uint32_t)hw[2 * i + 1] << 16));
    nOut = 0;
    for (int idle = 0; idle < 1000000 && nOut < 64; idle++) {
        const uint32_t s = read32(MDEC_STATUS);
        if (!(s & (1u << 31))) { const uint32_t v = read32(MDEC_DATA); if (nOut < 16) outBuf[nOut] = v; nOut++; idle = 0; }
        else if (!(s & (1u << 29))) break;
    }
}

static uint32_t rng = 0x2545F491u;
static uint32_t rnd(uint32_t n) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % n; }

int main() {
    printf("\nmdec/mdec-idct2\n");
    printf("48 pseudo-random AC-rich blocks, each decoded alone to 8-bit mono from a reset decoder: the stream, then the\n");
    printf("64 bytes. No assertions - compare against your emulator.\n\n");
    static uint16_t hw[32];
    for (int b = 0; b < 48; b++) {
        int n = 0, k = 0;
        const int qs = 1 + (int)rnd(2), dc = (int)rnd(241) - 120, nac = 3 + (int)rnd(10);
        hw[n++] = (uint16_t)((qs << 10) | (dc & 0x3FF));
        for (int a = 0; a < nac; a++) {
            const int run = (int)rnd(6);
            if (k + run + 1 > 62) break;
            k += run + 1;
            int lv = 1 + (int)rnd(15); if (rnd(2)) lv = -lv;
            hw[n++] = (uint16_t)((run << 10) | (lv & 0x3FF));
        }
        if (!(n & 1)) hw[n++] = 0x0000;                         /* a zero AC code: the count even with the end code */
        hw[n++] = 0xFE00;
        fresh(); decode(hw, n);
        printf("  %2d q %d words %2d:", b, qs, nOut);
        for (int i = 0; i < n; i++) printf(" %04x", hw[i]);
        printf("\n     ");
        const uint8_t* out = (const uint8_t*)outBuf;
        for (int i = 0; i < 64; i++) printf("%02x%s", out[i], (i & 7) == 7 ? (i == 63 ? "\n" : " ") : "");
    }
    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-idct2: Done.\n");
    for (;;) {}
}
