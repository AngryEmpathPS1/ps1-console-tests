/*
 * mdec/mdec-scale - does the MDEC multiply by the scale table it was sent (MDEC(3)), and at what precision; and the
 * dequantiser's rounding, amplified (2026-10-12 night).
 *
 * WHY. hwtests/mdec-idct2 on the SCPH-7501 (six runs, byte-identical): this core's IDCT (v0.377.31: a 13-bit table, pass
 * 1 floored >> 14, pass 2's products floored >> 7, the sum rounded >> 9) misses 284 of its 3,072 bytes by one, in all
 * 48 blocks, and nothing in reach fits: every matrix-multiply variant (table 8-16 bits, each pass's shift, rounding
 * and product floors, either pass order) against every dequantiser (this core's half-step bias at -8 / -4 / 0, psx-spx's
 * and MiSTer's (l x q x s + 4) / 8, exact) gives 250 or more; MiSTer's own arithmetic (full precision, >> 32) 1,460; an
 * exact IDCT under the best dequantiser 255; the AAN fast IDCT (psx-spx's fast_idct_core) in fixed point - 8-14 bit
 * constants, rounded or floored products and passes - 250 or more (scratch/scratch_idct2fit.js, scratch_idctaan.js,
 * scratch_idct2exact.js). The console's bytes sit up to 0.3 of a step from the exact IDCT's, so its internal precision
 * is coarser somewhere none of those put it. This separates the stages.
 *
 * WHAT IT DOES. Everything by CPU writes and reads, 8 bpp unsigned, a reset decoder before each block (mdec-idct2's).
 *   A. mdec-idct2's first 16 blocks (the same xorshift stream) under six scale tables: the standard one (the control:
 *      mdec-idct2's bytes), every entry halved (arithmetic >> 1: a multiplier that uses the table quarters the picture;
 *      one that ignores it does not), and the standard table with its low 1, 2, 3 and 4 bits cleared (which bits the
 *      multiplier sees). 64 bytes a block.
 *   B. A FLAT table (every entry 7FFFh, ~2.0 in its 14 fraction bits) and a quant table of 1s: one coefficient then puts
 *      (its dequantised value) x ~4 / 16 on every pixel. A coefficient at k = 1, levels -128..127, at q_scale 31, 32, 33
 *      and 7: the dequantiser's (level x q_scale / 8) and the IDCT's rounding of one product each, as a staircase - one
 *      byte a level (the block's first; a block that is not flat prints the level and its first two bytes). And the DC
 *      alone (k = 0, no q_scale) at -512..511 under the same table, one byte each.
 *
 * READ IT LIKE THIS. A: the halved table's bytes against the control's - (c - 128) / 4 + 128 if the table is used. B:
 * where each staircase steps. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-scale`.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>

static bool waitClear(uint32_t bit) { for (int i = 0; i < 1000000; i++) if (!(read32(MDEC_STATUS) & bit)) return true; return false; }
static void command(uint32_t w) { waitClear(1u << 29); write32(MDEC_CMD, w); }
static void data(uint32_t w) { waitClear(1u << 30); write32(MDEC_CMD, w); }
static int16_t table[64];
static uint8_t qtab[64];
static void fresh() {
    write32(MDEC_CTRL, 0x80000000u);
    command(0x40000000u);                                    /* luma quant table only */
    for (int i = 0; i < 16; i++) data(qtab[i * 4] | (qtab[i * 4 + 1] << 8) | (qtab[i * 4 + 2] << 16) | ((uint32_t)qtab[i * 4 + 3] << 24));
    command(0x60000000u);
    for (int i = 0; i < 32; i++) data((uint16_t)table[i * 2] | ((uint32_t)(uint16_t)table[i * 2 + 1] << 16));
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

static uint16_t blocks[16][32];
static int blockLen[16];

int main() {
    printf("\nmdec/mdec-scale\n");
    printf("A: mdec-idct2's first 16 blocks under six scale tables; B: one coefficient under a flat 7FFFh table and a\n");
    printf("quant table of 1s, as a staircase. 8-bit mono, a reset decoder each block. No assertions.\n\n");
    for (int b = 0; b < 16; b++) {                          /* mdec-idct2's stream, block for block */
        int n = 0, k = 0;
        uint16_t* hw = blocks[b];
        const int qs = 1 + (int)rnd(2), dc = (int)rnd(241) - 120, nac = 3 + (int)rnd(10);
        hw[n++] = (uint16_t)((qs << 10) | (dc & 0x3FF));
        for (int a = 0; a < nac; a++) {
            const int run = (int)rnd(6);
            if (k + run + 1 > 62) break;
            k += run + 1;
            int lv = 1 + (int)rnd(15); if (rnd(2)) lv = -lv;
            hw[n++] = (uint16_t)((run << 10) | (lv & 0x3FF));
        }
        if (!(n & 1)) hw[n++] = 0x0000;
        hw[n++] = 0xFE00;
        blockLen[b] = n;
    }
    static const char* const TNAME[6] = { "STD", "STD>>1", "STD&~1", "STD&~3", "STD&~7", "STD&~15" };
    for (int i = 0; i < 64; i++) qtab[i] = quant[i];
    for (int t = 0; t < 6; t++) {
        for (int i = 0; i < 64; i++) {   /* t = 2..5: the low 1..4 bits cleared */
            const int v = idct[i];
            table[i] = (int16_t)(t == 0 ? v : t == 1 ? (v >> 1) : (v & ~((1 << (t - 1)) - 1)));
        }
        printf("A %s:\n", TNAME[t]);
        for (int b = 0; b < 16; b++) {
            fresh(); decode(blocks[b], blockLen[b]);
            const uint8_t* out = (const uint8_t*)outBuf;
            printf("  %2d w%2d ", b, nOut);
            for (int i = 0; i < 64; i++) printf("%02x%s", out[i], (i & 7) == 7 ? (i == 63 ? "\n" : " ") : "");
        }
    }
    /* B: the flat table and quant 1s */
    for (int i = 0; i < 64; i++) { table[i] = 0x7FFF; qtab[i] = 1; }
    static const int QS[4] = { 31, 32, 33, 7 };
    for (int q = 0; q < 4; q++) {
        printf("B k=1 q_scale %d, levels -128..127:\n", QS[q]);
        for (int lv = -128; lv < 128; lv++) {
            const uint16_t hw[4] = { (uint16_t)(QS[q] << 10), (uint16_t)(lv & 0x3FF), 0x0000, 0xFE00 };   /* DC 0, AC at k = 1 */
            fresh(); decode(hw, 4);
            const uint8_t* out = (const uint8_t*)outBuf;
            bool flat = nOut == 16;
            for (int i = 1; i < 64 && flat; i++) if (out[i] != out[0]) flat = false;
            if (((lv + 128) & 31) == 0) printf("  %4d:", lv);
            if (flat) printf(" %02x", out[0]); else printf(" [%d:%02x%02x]", lv, out[0], out[1]);
            if (((lv + 128) & 31) == 31) printf("\n");
        }
    }
    printf("B DC alone (k=0), -512..511:\n");
    for (int dc = -512; dc < 512; dc++) {
        const uint16_t hw[2] = { (uint16_t)((1 << 10) | (dc & 0x3FF)), 0xFE00 };
        fresh(); decode(hw, 2);
        const uint8_t* out = (const uint8_t*)outBuf;
        if (((dc + 512) & 31) == 0) printf("  %4d:", dc);
        printf(" %02x", out[0]);
        if (((dc + 512) & 31) == 31) printf("\n");
    }
    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-scale: Done.\n");
    for (;;) {}
}
