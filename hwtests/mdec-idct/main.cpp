/*
 * mdec/mdec-idct - the MDEC's IDCT and dequantiser read out wide enough to fit a rule to, and where its phantom
 * block comes from.
 *
 * WHY (console day, 2026-10-05, the SCPH-5501 and SCPH-7501). mdec-mono-out decoded 27 blocks from a reset decoder
 * on both consoles, byte for byte the same, and no simple rounding rule fits them all: pass 1 floored keeping 17
 * fraction bits and pass 2 rounded fits 15 of 16 at q_scale 0 and 8 of 11 at q_scale 1-4; this emulator's rule (round
 * once, M224) 12 of 16 since M362; MiSTer's (floor once) only the flat blocks; psx-spx's real_idct_core none. 27
 * blocks are too few to tell the candidates' last differences apart. And every short command was followed by ONE
 * phantom block (80857F88 857C8073 ...), none after a feed padded to 32 words with FE00h: stale input decoded, or
 * something else?
 *
 * WHAT IT DOES. Every block from a reset decoder (both tables sent again), decoded at 8-bit mono, every output word
 * read by the CPU while the status says one is there, and the block's bytes kept (the phantom after it read and
 * dropped).
 *   DC0      the DC alone at q_scale 0, every value -512..511: a flat block, so ONE byte each (a '*' after a block
 *            whose 64 bytes are not all equal), 32 to a line.
 *   DC1      the same at q_scale 1 (the DC through qt[0] = 2 and the dequantiser's half step).
 *   AC       q_scale 0 (no zigzag: position k itself), a DC and ONE coefficient: k = 1, 2, 9, 63, DC -100 / 0 / 37,
 *            level -300 / -51 / -7 / 7 / 51 / 300 - every byte, one block a line.
 *   RING     the phantom: (a) the tables sent quant then IDCT (as every program here), then [0340 FE00]; (b) IDCT
 *            then quant, then the same; (c) a 4-word command first ([0155 1C2D FE00 0333] - a block, then a DC that
 *            never ends), then the same. Each stream as runs of one value.
 *   QZERO    a luma table with qt[5] = 0, then one coefficient at k = 5 (run 4, level 100) on a DC at q_scale 1:
 *            psx-spx and MiSTer add nothing (level x qt x q_scale / 8 = 0), mednafen and this emulator level x 2.
 *            Beside it the same block through the normal table, and the DC alone.
 *
 * READ IT LIKE THIS. DC0/DC1 are a rule's DC path at every boundary; AC its products; a candidate must give every
 * byte. RING (b) or (c) with a different phantom than (a) says the phantom is decoded from what the input held.
 * QZERO's first block equal to the DC alone says a zero table entry gives 0.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-idct --frames=6000`. ON THIS CORE AT v0.362 (rule A, M362's
 *   placement, mednafen's zero-entry value): DC0 a step every 4 values, 00 at -512 and -511, 01 from -510; RING
 *   one block's worth in every case, no phantom - (c)'s unfinished DC dropped; QZERO's zero-entry block
 *   927F645050647F92 rows (level x 2 placed), the normal table's 9881614A4A618198, the DC alone 71 throughout.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>

static bool waitClear(uint32_t bit) { for (int i = 0; i < 1000000; i++) if (!(read32(MDEC_STATUS) & bit)) return true; return false; }
static void command(uint32_t w) { waitClear(1u << 29); write32(MDEC_CMD, w); }
static void data(uint32_t w) { waitClear(1u << 30); write32(MDEC_CMD, w); }

static uint8_t qtab[128];
static void sendQuant() { command(0x40000001u); for (int i = 0; i < 32; i++) data(qtab[i * 4] | (qtab[i * 4 + 1] << 8) | (qtab[i * 4 + 2] << 16) | ((uint32_t)qtab[i * 4 + 3] << 24)); }
static void sendIdct() { command(0x60000000u); for (int i = 0; i < 32; i++) data((uint16_t)idct[i * 2] | ((uint32_t)(uint16_t)idct[i * 2 + 1] << 16)); }
/* Reset (abort, status 80040000h) and both tables, quant first unless asked. */
static void fresh(bool idctFirst = false) {
    write32(MDEC_CTRL, 0x80000000u);
    if (idctFirst) { sendIdct(); sendQuant(); } else { sendQuant(); sendIdct(); }
}

static uint32_t outBuf[4096];
static int nOut;
static void decode(const uint16_t* hw, int halfwords) {
    const int words = halfwords / 2;
    command((1u << 29) | (1u << 27) | (uint32_t)words);    /* decode, 8-bit mono, unsigned */
    for (int i = 0; i < words; i++) data(hw[2 * i] | ((uint32_t)hw[2 * i + 1] << 16));
    nOut = 0;
    for (int idle = 0; idle < 1000000 && nOut < 4096; idle++) {
        const uint32_t s = read32(MDEC_STATUS);
        if (!(s & (1u << 31))) { outBuf[nOut++] = read32(MDEC_DATA); idle = 0; }
        else if (!(s & (1u << 29))) break;
    }
}
static const uint8_t* bytes() { return (const uint8_t*)outBuf; }
static bool flat() { for (int i = 1; i < 64; i++) if (bytes()[i] != bytes()[0]) return false; return true; }

static void dcSweep(int q) {
    for (int dc = -512; dc < 512; dc++) {
        if ((dc & 31) == 0) printf("  %4d:", dc);
        uint16_t hw[2] = { (uint16_t)((q << 10) | (dc & 0x3FF)), 0xFE00 };
        fresh(); decode(hw, 2);
        printf(" %02x%s", nOut >= 16 ? bytes()[0] : 0xFF, nOut >= 16 && flat() ? "" : "*");
        if ((dc & 31) == 31) printf("\n");
    }
}

static void showBlock() {
    for (int i = 0; i < 64; i++) printf("%02x%s", nOut >= 16 ? bytes()[i] : 0, (i & 7) == 7 ? (i == 63 ? "\n" : " ") : "");
}

static void showStream(const char* name) {
    printf("  %-40s %4d words out\n", name, nOut);
    int runs = 0;
    for (int i = 0; i < nOut;) {
        int j = i;
        while (j + 1 < nOut && outBuf[j + 1] == outBuf[i]) j++;
        if (runs < 20) printf("    [%4d-%4d] %08x\n", i, j, (int)outBuf[i]);
        runs++;
        i = j + 1;
    }
    if (runs > 20) printf("    ... %d runs in all\n", runs);
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-idct\n");
    printf("The MDEC's IDCT and dequantiser, every block from a reset decoder, 8-bit mono. No assertions.\n\n");
    for (int i = 0; i < 128; i++) qtab[i] = quant[i];

    printf("DC0 (q_scale 0): one byte a value, from -512\n");
    dcSweep(0);
    printf("DC1 (q_scale 1): one byte a value, from -512\n");
    dcSweep(1);

    printf("AC (q_scale 0, no zigzag): k dc level: 64 bytes\n");
    static const int ks[4] = { 1, 2, 9, 63 }, dcs[3] = { -100, 0, 37 }, lv[6] = { -300, -51, -7, 7, 51, 300 };
    for (int a = 0; a < 4; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 6; c++) {
        const int k = ks[a];
        uint16_t hw[4] = { (uint16_t)(dcs[b] & 0x3FF), (uint16_t)(((k - 1) << 10) | (lv[c] & 0x3FF)), 0x0000, 0xFE00 };
        /* a zero AC code (run 0, level 0) at k + 1 keeps the halfword count even and adds nothing - unless k = 63,
         * where it would start a new block: then the end code comes at once and the padding is FE00 (padding) */
        if (k == 63) { hw[2] = 0xFE00; hw[3] = 0xFE00; }
        fresh(); decode(hw, 4);
        printf("  %2d %4d %4d  ", k, dcs[b], lv[c]);
        showBlock();
    }

    printf("RING (the phantom after a one-word command)\n");
    static const uint16_t dcBlock[2] = { 0x0340, 0xFE00 };
    fresh(); decode(dcBlock, 2); showStream("(a) quant then IDCT, [0340 FE00]");
    fresh(true); decode(dcBlock, 2); showStream("(b) IDCT then quant, [0340 FE00]");
    static const uint16_t first[4] = { 0x0155, 0x1C2D, 0xFE00, 0x0333 };
    fresh(); decode(first, 4); showStream("(c) first [0155 1C2D FE00 0333]");
    decode(dcBlock, 2); showStream("    then [0340 FE00], no reset");

    printf("QZERO (q_scale 1; luma qt[5] = 0 or the normal %d)\n", quant[5]);
    static const uint16_t withK5[4] = { (1 << 10) | (0x3C4 & 0x3FF), (4 << 10) | 100, 0x0000, 0xFE00 };   /* DC -60, k = 5 level 100 */
    static const uint16_t dcOnly[2] = { (1 << 10) | (0x3C4 & 0x3FF), 0xFE00 };
    qtab[5] = 0;
    fresh(); decode(withK5, 4); printf("  qt[5]=0, DC -60 + k5 100   "); showBlock();
    qtab[5] = quant[5];
    fresh(); decode(withK5, 4); printf("  qt[5]=%2d, DC -60 + k5 100  ", quant[5]); showBlock();
    fresh(); decode(dcOnly, 2); printf("  DC -60 alone               "); showBlock();

    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-idct: Done.\n");
    for (;;) VSync(0);
}
