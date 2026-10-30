/*
 * mdec/mdec-color - the MDEC's YUV -> RGB stage, swept on flat blocks whose values are already known.
 *
 * WHY (2026-10-08). hwtests/mdec-idct pinned the IDCT on all three consoles: every DC value -512..511 at
 * q_scale 0 gives one flat byte, the same on the SCPH-5501, 7501 and 9001 (src/psx.js idct, v0.377.31). The colour
 * stage after it is still a fit to one photo: the chroma products truncated (mdec/frame on ps1-tests' author's
 * console, 15 bpp), against which mdec-step24 on three consoles (24 bpp, all three byte-identical) prefers
 * them ROUNDED - 825 of its 3,072 bytes off truncated, 382 rounded - while the author's 15-bit log of the same four
 * macroblocks prefers truncated, 32 halfwords off against 88. Both are pictures, where every pixel mixes an IDCT's
 * rounding into the colour's, so neither can say which part is wrong. This one feeds the colour stage values the IDCT
 * already answered: every block of a macroblock DC-only at q_scale 0, so Y, Cr and Cb are each flat and each is the
 * byte mdec-idct's DC0 row gives for its DC. Then R depends on Y and Cr alone, B on Y and Cb, G on all three.
 *
 * WHAT IT DOES. Both tables sent once, then macroblocks of six DC-only blocks (Cr, Cb, Y1-Y4 the same), one command
 * per sweep, every output word read by the CPU and the first pixel of each macroblock kept:
 *   CR  Y fixed (DC -512, -256, 0, 256, 508: Y about -128, -64, 0, 64, 127), Cb DC 0, Cr's DC every value -512..511 -
 *       R and G printed; and at Y DC 0 with Cb DC -400, -200, 200, 400 - G
 *   CB  the same with Cb swept and Cr fixed - B and G
 * first at 24 bpp, then the same sweeps at 15 bpp (R, G or B as its 5 bits), unsigned.
 * One line per channel per sweep: "CR 24 Y=0 Cb=0 R" then 1,024 bytes as hex in 8 lines of 128.
 *
 * READ IT LIKE THIS. Map each DC to its flat value through mdec-idct's DC0 row (same console); then R - Y against Cr
 * is the red product's whole rule (its coefficient, its rounding, where it saturates or wraps), B - Y the blue one, and
 * the G rows its two products and how they are summed (DuckStation masks them: (-88 Cb) AND NOT 1Fh plus (-183 Cr) AND
 * NOT 7). The 15-bit rows say whether 15 bpp rounds the 8-bit result to 5 bits ((v + 4) >> 3, this core) or the full-
 * precision sum, which would let both consoles' pictures be right.
 *
 * v0.377.40 TAKES BOTH PICTURES AT THEIR WORD: rounded at 24 bpp, truncated at 15 bpp (src/psx.js yuvToRGB) - with
 * v0.377.38's dequantiser the split read better on every MDEC log of both console groups (mdec-step24 2,850 -> 1,113
 * numbers off, the author's 24-bit DMA photo 78,306 -> 27,809 halfwords, his 15-bit ones unchanged). These rows say
 * whether that is the rule or two halves of another one.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-color --frames=8000`.
 */
#include <common.h>
#include <io.h>
#include <mdec.h>

static bool waitClear(uint32_t bit) { for (int i = 0; i < 1000000; i++) if (!(read32(MDEC_STATUS) & bit)) return true; return false; }
static bool command(uint32_t w) { if (!waitClear(1u << 29)) return false; write32(MDEC_CMD, w); return true; }
static void data(uint32_t w) { waitClear(1u << 30); write32(MDEC_CMD, w); }

static void sendQuant() { command(0x40000001u); for (int i = 0; i < 32; i++) data(quant[i * 4] | (quant[i * 4 + 1] << 8) | (quant[i * 4 + 2] << 16) | ((uint32_t)quant[i * 4 + 3] << 24)); }
static void sendIdct() { command(0x60000000u); for (int i = 0; i < 32; i++) data((uint16_t)idct[i * 2] | ((uint32_t)(uint16_t)idct[i * 2 + 1] << 16)); }

#define N 1024
static uint32_t first[N];          /* the first output word of each macroblock */

/*
 * One sweep: N macroblocks, block DCs given per block slot (Cr, Cb, Y), the swept slot taking DC -512 + i.
 * ONE LOOP, OUTPUT FIRST (2026-10-08). Until then it waited for room in the input FIFO before it drained the output, and
 * on the SCPH-5501 (the overnight runner batch, its first run on a console) it printed nothing for four minutes: if the
 * decoder stops while its output is full, every input word waited out waitClear's million reads. Now each turn takes an
 * output word if there is one, else gives an input word if there is room; a sweep that moves nothing for 2,000,000
 * turns says where it stopped and the MDEC's status, resets the decoder, and the test goes on (three such: the rest
 * skipped). The words the decoder gives are the same either way.
 */
static int stalls = 0;
static void sweep(int depth, int crDc, int cbDc, int yDc, int swept) {
    const int words = N * 6;                        /* six blocks of [DC, FE00] */
    const int perMb = depth == 2 ? 192 : 128;       /* 24 bpp: 768 bytes; 15 bpp: 512 */
    for (int i = 0; i < N; i++) first[i] = 0xFFFFFFFFu;
    if (stalls >= 3) { printf("  (skipped: the decoder stopped three times)\n"); return; }
    if (!command((1u << 29) | ((uint32_t)depth << 27) | (uint32_t)words)) { printf("  STALLED before the command: MDEC status %08x\n", (unsigned)read32(MDEC_STATUS)); stalls++; return; }
    int mb = 0, inMb = 0, fed = 0, idle = 0;
    while (mb < N) {
        const uint32_t st = read32(MDEC_STATUS);
        if (!(st & (1u << 31))) {                   /* an output word */
            const uint32_t o = read32(MDEC_DATA);
            if (inMb == 0) first[mb] = o;
            if (++inMb == perMb) { inMb = 0; mb++; }
            idle = 0;
        } else if (fed < words && !(st & (1u << 30))) {   /* room for an input word */
            const int i = fed / 6, b = fed % 6, v = -512 + i;
            const int d = b < 2 ? (swept == b ? v : (b == 0 ? crDc : cbDc)) : (swept == 2 ? v : yDc);
            write32(MDEC_CMD, (uint32_t)(d & 0x3FF) | 0xFE000000u);
            fed++; idle = 0;
        } else if (++idle > 2000000) {
            printf("  STALLED: %d of %d words in, %d of %d macroblocks out (+%d words), MDEC status %08x\n",
                   fed, words, mb, N, inMb, (unsigned)st);
            stalls++;
            write32(MDEC_CTRL, 0x80000000u);
            sendQuant(); sendIdct();
            return;
        }
    }
}

static void show(const char *name, int depth, int ch) {
    printf("%s\n", name);
    for (int i = 0; i < N; i++) {
        int v;
        if (depth == 2) v = (first[i] >> (ch * 8)) & 0xFF;                /* 24 bpp: R, G, B bytes */
        else v = (first[i] >> (ch * 5)) & 0x1F;                           /* 15 bpp: the first pixel's 5 bits */
        if ((i & 127) == 0) printf("  %4d:", -512 + i);
        printf("%02x", v);
        if ((i & 127) == 127) printf("\n");
    }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-color\n");
    printf("The MDEC's colour stage on flat DC-only macroblocks (q_scale 0), unsigned. No assertions.\n\n");
    write32(MDEC_CTRL, 0x80000000u);
    sendQuant(); sendIdct();
    static const int ys[5] = { -512, -256, 0, 256, 508 }, others[4] = { -400, -200, 200, 400 };
    char name[64];
    for (int depth = 2; depth <= 3; depth++) {
        const int bits = depth == 2 ? 24 : 15;
        for (int k = 0; k < 5; k++) {
            sweep(depth, 0, 0, ys[k], 0);
            sprintf(name, "CR %d Y=%d Cb=0 R", bits, ys[k]); show(name, depth, 0);
            sprintf(name, "CR %d Y=%d Cb=0 G", bits, ys[k]); show(name, depth, 1);
        }
        for (int k = 0; k < 4; k++) {
            sweep(depth, 0, others[k], 0, 0);
            sprintf(name, "CR %d Y=0 Cb=%d G", bits, others[k]); show(name, depth, 1);
        }
        for (int k = 0; k < 5; k++) {
            sweep(depth, 0, 0, ys[k], 1);
            sprintf(name, "CB %d Y=%d Cr=0 B", bits, ys[k]); show(name, depth, 2);
            sprintf(name, "CB %d Y=%d Cr=0 G", bits, ys[k]); show(name, depth, 1);
        }
        for (int k = 0; k < 4; k++) {
            sweep(depth, others[k], 0, 0, 1);
            sprintf(name, "CB %d Y=0 Cr=%d G", bits, others[k]); show(name, depth, 1);
        }
    }
    write32(MDEC_CTRL, 0x80000000u);
    printf("\nmdec/mdec-color: Done.\n");
    for (;;) VSync(0);
}
