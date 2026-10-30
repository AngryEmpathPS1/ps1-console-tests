/*
 * mdec/mdec-qbias - the dequantiser's half step: 8, 5 or none?
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, MDEC row "dequantiser bias 8 (the photo
 * fits 5)"). Every coefficient a movie sends goes through the dequantiser,
 * which this core does as mednafen does: (((level x q_scale x qt[k]) >> 3)
 * << 4), then a half step (8) taken back towards zero. psx-spx does not have
 * the half step at all; MiSTer's rtl/mdec.vhd has none. Swept against the one
 * console photo there is (mdec/frame), 5 fits better than 8 by 900 of 76,800
 * pixels, and src/psx.js declines it because 5 has no derivation and 8 does.
 * mdec-round cannot see it: it sends every block at q_scale 0, where the
 * dequantiser is skipped.
 *
 * WHAT IT DOES. The blocks carry a q_scale, so each coefficient is
 * dequantised; ps1-tests' quantisation table is uploaded (DC 2, the ACs 16 to
 * 83). gen.js chose, for a DC and one AC coefficient each, blocks where a half
 * step of 8, of 5 and of 0 give different bytes after this core's IDCT (M224,
 * which mdec-round asks about); two are controls where all three agree.
 * Decoded in 8-bit mono and read back, as mdec-round does. No DC-only block
 * can tell them apart (gen.js says why).
 *
 * READ IT LIKE THIS. For each block, how many of its 64 bytes each half step
 * predicts. A console that reads 64/64 for 8 on every block confirms
 * mednafen's; 5 on every block, the photo's fit; 0, MiSTer's dequantiser. A
 * console that matches none on a block prints its bytes. If mdec-round showed
 * the console's IDCT is not this core's, read this one's bytes against that
 * rule instead: the dequantiser and the IDCT are separate questions here only
 * because this core's IDCT is assumed.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js mdec-qbias`): 8 exact on all 11
 * blocks, 5 and 0 on the 2 controls only. TESTED BOTH WAYS: with MDEC.bias
 * returning 5 in a scratch copy of the core the run reads 5 exact on all 11
 * and 8 on the controls.
 * CONSOLE DAY (2026-10-05): "words past a block's 16" - 0 here at v0.361. On the
 * SCPH-5501 the MDEC was still busy with output waiting after block 0's 16
 * words, and block 1 never started (drainExtra). The drain was tested with
 * mdec-round's: a copy reading 8 of 16 reads "then 8 more words" on every
 * block, the block's second half, the MDEC idle after.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>
#include "blocks.h"

static int match(const uint8_t* out, const uint8_t* want) {
    int n = 0;
    for (int i = 0; i < 64; i++) if (out[i] == want[i]) n++;
    return n;
}

/* After a block's 16 words, whatever else the MDEC still hands out (CONSOLE DAY, 2026-10-05): on the SCPH-5501 the
 * status read 6204FFFFh after a mono block's 16 words - out FIFO NOT empty, busy - and the next block's command,
 * which waits for busy to clear (mdec_decode), waited for ever. Each further word is read while the FIFO has one, the
 * first eight kept, until the MDEC is empty and idle, 4,096 words, or ~1M status reads with neither. Still busy then:
 * reset and the tables sent again, said on the line. */
static uint32_t extraW[8];
static int drainExtra(uint32_t* endStatus, bool* reset) {
    int n = 0;
    for (int idle = 0; idle < 1000000 && n < 4096; idle++) {
        const uint32_t s = read32(MDEC_STATUS);
        if (!(s & (1u << 31))) { const uint32_t v = read32(MDEC_DATA); if (n < 8) extraW[n] = v; n++; idle = 0; }
        else if (!(s & (1u << 29))) break;
    }
    *endStatus = read32(MDEC_STATUS);
    *reset = (*endStatus & (1u << 29)) != 0;
    if (*reset) { mdec_reset(); mdec_quantTable(quant, true); mdec_idctTable(idct); }
    return n;
}
static void showExtra(int n, uint32_t endStatus, bool reset) {
    if (!n && !reset) return;
    printf("     then %d more word%s:", n, n == 1 ? "" : "s");
    for (int i = 0; i < n && i < 8; i++) printf(" %08x", (int)extraW[i]);
    printf("%s (status %08x)%s\n", n > 8 ? " ..." : "", (int)endStatus, reset ? " - still busy: reset, tables sent again" : "");
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-qbias\n");
    printf("%d blocks at q_scale 1-4, 8-bit mono: is the dequantiser's half step 8 (this emulator), 5, or none?\n", NBLOCKS);
    printf("There are no assertions - compare against your emulator.\n\n");

    mdec_reset();
    mdec_quantTable(quant, true);
    mdec_idctTable(idct);

    int all[3] = { 0, 0, 0 }, controlsOk = 1, extraBlocks = 0, extraWords = 0;
    for (int b = 0; b < NBLOCKS; b++) {
        const Block& blk = blocks[b];
        mdec_decode(blk.rle, blk.words * 4, ColorDepth::bit_8, false, false);
        uint32_t w[16];
        for (int i = 0; i < 16; i++) w[i] = mdec_read();
        uint32_t endStatus; bool wasReset;
        const int extra = drainExtra(&endStatus, &wasReset);
        if (extra) { extraBlocks++; extraWords += extra; }
        const uint8_t* out = (const uint8_t*)w;
        const int m8 = match(out, blk.b8), m5 = match(out, blk.b5), m0 = match(out, blk.b0);
        if (m8 == 64) all[0]++;
        if (m5 == 64) all[1]++;
        if (m0 == 64) all[2]++;
        if (blk.role[0] == 'c' && m8 != 64) controlsOk = 0;
        printf("  %2d %-7s dc %4d  AC %2d = %4d  q %d   8: %2d/64  5: %2d/64  0: %2d/64\n", b, blk.role, blk.dc, blk.k, blk.v, blk.qs, m8, m5, m0);
        if (m8 != 64 && m5 != 64 && m0 != 64) {
            printf("     none: ");
            for (int i = 0; i < 64; i++) printf("%02x%s", out[i], (i & 7) == 7 ? (i == 63 ? "\n" : " ") : "");
        }
        showExtra(extra, endStatus, wasReset);
    }
    printf("\n  blocks each half step predicts exactly: 8 %d, 5 %d, 0 %d of %d\n", all[0], all[1], all[2], NBLOCKS);
    printf("  words past a block's 16: after %d of %d blocks, %d in all\n", extraBlocks, NBLOCKS, extraWords);
    if (!controlsOk) printf("  *** A CONTROL FAILED - this run says nothing about the dequantiser ***\n");
    mdec_reset();
    printf("\nmdec/mdec-qbias: Done.\n");
    for (;;) VSync(0);
}
