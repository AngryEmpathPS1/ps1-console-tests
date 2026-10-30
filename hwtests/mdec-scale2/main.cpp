/*
 * mdec/mdec-scale2 - which scale-table entries the MDEC's IDCT multiplies into which output pixels: impulse tables
 * (2026-10-12 night).
 *
 * WHY. hwtests/mdec-scale on the SCPH-7501 (2026-10-12 night, six runs): halving every scale-table entry quarters the
 * picture (the table is used, in both passes), clearing its low 1-3 bits changes nothing - but under a FLAT table
 * (every entry 7FFFh) one AC coefficient at k = 1 does NOT give a flat block, where any matrix-multiply IDCT (out[x][y] =
 * sum c[u][v] T[u][x] T[v][y]) must; the DC alone does. So the hardware does not multiply the coefficients by the table
 * as a matrix, both passes - as every emulator and psx-spx's real_idct_core do - and this core's 284 bytes off on
 * mdec-idct2 are that. Something else uses the table's entries: a fast IDCT whose constants come from it, or a matrix in
 * one pass and something else in the other. This maps it.
 *
 * WHAT IT DOES. Everything by CPU writes and reads, 8 bpp SIGNED (0 = no output), a reset decoder before each block,
 * quant table all 1s (q_scale 32: a level L is 4L exactly). Probe blocks: DC alone (level 400), and one AC coefficient
 * of level 120 at k = 1, 2, 3, 5 and 9. Tables:
 *   IMPULSE j   every entry 0 but entry j (0..63) = 4000h (1.0 in its 14 fraction bits)
 *   FLAT        every entry 4000h
 * For each table and probe, the 64 output bytes (two hex digits, signed) - or "zero" when all are 0.
 *
 * READ IT LIKE THIS. A matrix multiply puts a DC through IMPULSE (0,b) on pixel (b,b) alone, and an AC at (u,v) through
 * IMPULSE (a,b) only if u = v = a. Anything else is the structure. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-scale2`.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>

static bool waitClear(uint32_t bit) { for (int i = 0; i < 1000000; i++) if (!(read32(MDEC_STATUS) & bit)) return true; return false; }
static void command(uint32_t w) { waitClear(1u << 29); write32(MDEC_CMD, w); }
static void data(uint32_t w) { waitClear(1u << 30); write32(MDEC_CMD, w); }
static int16_t table[64];
static void fresh() {
    write32(MDEC_CTRL, 0x80000000u);
    command(0x40000000u);
    for (int i = 0; i < 16; i++) data(0x01010101u);           /* quant table: all 1 */
    command(0x60000000u);
    for (int i = 0; i < 32; i++) data((uint16_t)table[i * 2] | ((uint32_t)(uint16_t)table[i * 2 + 1] << 16));
}
static uint32_t outBuf[64];
static int nOut;
static void decode(const uint16_t* hw, int halfwords) {
    const int words = halfwords / 2;
    command((1u << 29) | (1u << 27) | (1u << 26) | (uint32_t)words);   /* 8 bpp, SIGNED */
    for (int i = 0; i < words; i++) data(hw[2 * i] | ((uint32_t)hw[2 * i + 1] << 16));
    nOut = 0;
    for (int idle = 0; idle < 1000000 && nOut < 64; idle++) {
        const uint32_t s = read32(MDEC_STATUS);
        if (!(s & (1u << 31))) { const uint32_t v = read32(MDEC_DATA); if (nOut < 16) outBuf[nOut] = v; nOut++; idle = 0; }
        else if (!(s & (1u << 29))) break;
    }
}
/* probes: DC alone (level 400), or DC 0 and one AC at k (q_scale 32, level 120: 480 in the coefficient's units) */
static int probe(int k, uint16_t* hw) {
    int n = 0;
    if (k == 0) { hw[n++] = (uint16_t)((32 << 10) | 400); hw[n++] = 0xFE00; return n; }
    hw[n++] = (uint16_t)(32 << 10);
    hw[n++] = (uint16_t)(((k - 1) << 10) | 120);
    if (!(n & 1)) hw[n++] = 0x0000;                           /* a zero AC code: the count even with the end code */
    hw[n++] = 0xFE00;
    return n;
}
static void row(const char* name, int j, int k) {
    static uint16_t hw[8];
    const int n = probe(k, hw);
    fresh(); decode(hw, n);
    const uint8_t* out = (const uint8_t*)outBuf;
    bool any = false;
    for (int i = 0; i < 64; i++) if (out[i]) any = true;
    printf("  %s %2d k%d:", name, j, k);
    if (!any) { printf(" zero\n"); return; }
    for (int i = 0; i < 64; i++) printf("%s%02x", (i & 7) == 0 ? " " : "", out[i]);
    printf("\n");
}

int main() {
    printf("\nmdec/mdec-scale2\n");
    printf("Impulse scale tables (one entry 4000h, the rest 0) and a flat one against DC-alone and one-AC blocks,\n");
    printf("8-bit signed, quant 1s, q_scale 32. No assertions.\n\n");
    static const int K[6] = { 0, 1, 2, 3, 5, 9 };
    for (int i = 0; i < 64; i++) table[i] = 0x4000;
    for (int p = 0; p < 6; p++) row("FLAT   ", 0, K[p]);
    for (int j = 0; j < 64; j++) {
        for (int i = 0; i < 64; i++) table[i] = 0;
        table[j] = 0x4000;
        for (int p = 0; p < 6; p++) row("IMPULSE", j, K[p]);
    }
    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-scale2: Done.\n");
    for (;;) {}
}
