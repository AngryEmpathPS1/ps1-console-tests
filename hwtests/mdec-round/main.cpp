/*
 * mdec/mdec-round - how does the MDEC's IDCT round?
 *
 * WHY (ROAD B; STATUS M130, M224). mdec/8bit decodes one block and a console
 * puts 8 of its 64 bytes one step away from this core. M130 searched 49
 * per-pass rounding modes and found none that wins on real video. M224 found
 * that carrying pass 1's fraction and rounding ONCE beats rounding each pass
 * on every MDEC oracle - but no oracle ever held a block built to sit on the
 * rounding boundaries, so the rule itself is still inferred from video.
 *
 * WHAT IT DOES. Sixteen single blocks, each decoded on its own in 8-bit mono
 * (unsigned) and read back. Every block is sent at q_scale 0, where a
 * coefficient is its level times 2 (psx-spx; x32 in this core's units) with no
 * quantisation table and no rounding - so whatever differs is the IDCT's. The
 * blocks were chosen (gen.js, which writes blocks.h) so three rules give
 * different bytes:
 *   A  this core since M224: pass 1 carried at full precision, the product
 *      rounded once (+2^35, >> 36);
 *   B  DuckStation and this core before M224: each pass rounded to nearest
 *      (>> 18) and cut to 16 bits between them;
 *   C  MiSTer's rtl/mdec.vhd: pass 1 carried, the product FLOORED once.
 * Two blocks are CONTROLS, where all three agree: a console that gets them
 * wrong has sent the test something else wrong, and the run says nothing.
 * Four are DC-only (A against B and C), ten a DC and one AC coefficient
 * (each rule against both others, at 16 to 64 of their bytes).
 * Each block is sent with its exact word count and no padding words: FE00h
 * padding past a block can stall a console's MDEC (nugget b7d9561, STATUS M236).
 *
 * READ IT LIKE THIS. For each block, how many of its 64 bytes each rule
 * predicts. A console that is one of the three reads 64/64 for it on every
 * block; a console that is none of them prints the block's bytes, and those
 * are the next thing to fit.
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js mdec-round`): A exact on all
 * 16 blocks (B on 2, C on 4 - the controls and the flat blocks they share).
 * The predictions were checked against the rules themselves: with the core's
 * idct() swapped for B's (the core before M224) the run reads B exact on 16,
 * and for C's (MiSTer's floor) C exact on 16.
 * CONSOLE DAY (2026-10-05): "words past a block's 16" - 0 here at v0.361. On the
 * SCPH-5501 the MDEC was still busy with output waiting after block 0's 16
 * words, and block 1 never started (drainExtra). TESTED: a copy reading 8 of
 * 16 reads "then 8 more words" on every block - the block's second half - and
 * the MDEC idle after (status 8204FFFFh).
 * ps1-tests' MDEC helpers log every call (their log.h defines DEBUG); the
 * lines between the rows are theirs.
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
 * reset and the tables sent again, said on the line. mdec-qbias has the same. */
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
    printf("\nmdec/mdec-round\n");
    printf("16 blocks at q_scale 0, 8-bit mono: does the IDCT round once (A, this emulator), each pass (B), or floor once (C)?\n");
    printf("There are no assertions - compare against your emulator.\n\n");

    mdec_reset();
    mdec_quantTable(quant, true);       /* the command order a game uses; q_scale 0 never reads it */
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
        const int a = match(out, blk.a), bb = match(out, blk.b), c = match(out, blk.c);
        if (a == 64) all[0]++;
        if (bb == 64) all[1]++;
        if (c == 64) all[2]++;
        if (blk.role[0] == 'c' && a != 64) controlsOk = 0;
        printf("  %2d %-7s dc %4d  AC %2d = %4d   A %2d/64  B %2d/64  C %2d/64\n", b, blk.role, blk.dc, blk.k, blk.v, a, bb, c);
        if (a != 64 && bb != 64 && c != 64) {
            printf("     none: ");
            for (int i = 0; i < 64; i++) printf("%02x%s", out[i], (i & 7) == 7 ? (i == 63 ? "\n" : " ") : "");
        }
        showExtra(extra, endStatus, wasReset);
    }
    printf("\n  blocks each rule predicts exactly: A %d, B %d, C %d of %d\n", all[0], all[1], all[2], NBLOCKS);
    printf("  words past a block's 16: after %d of %d blocks, %d in all\n", extraBlocks, NBLOCKS, extraWords);
    if (!controlsOk) printf("  *** A CONTROL FAILED - this run says nothing about the rounding ***\n");
    mdec_reset();
    printf("\nmdec/mdec-round: Done.\n");
    for (;;) VSync(0);
}
