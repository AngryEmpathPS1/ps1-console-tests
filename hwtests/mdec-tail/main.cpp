/*
 * mdec/mdec-tail - blocks behind a picture's last macroblock (2026-10-31, for the SCPH-7502).
 *
 * WHY. FF7's BIKEGET.MOV hung 19 frames of 751 on the SCPH-7502, the same 19 at every depth and timing, status A405FFFFh, cured
 * by a reset before every frame. mdec-winddown, a second picture at any distance behind the first, never hangs. But 31 of
 * the 751 frames have a word behind their last real block where the rest have FE00h, 0DFFh, 15FFh or 19FFh: the
 * end-of-frame code, 1FFh, read by bsdec as a block's DC. 17 of the 19 are two frames behind one of those (the rest of
 * the 31 fell in the frames dropped behind a hang). And with bsdec reading 8 bytes further, every frame had one: the end
 * status was A405FFFFh or A400FFFFh on 50 of 52, and every frame with no reset before it hung. Block 5 is one block in, block 0 two.
 * So the reading: the words run out with the MDEC part way through a macroblock, it stays busy for the rest, and the next
 * command goes in as data. psx-spx does not say so.
 *
 * WHAT IT DOES. A picture of 8 macroblocks as mdec-winddown makes it, with K more blocks behind the last (each a DC of 0DFFh
 * and an end of block), padded with FE00h to a 32-word block, K from 0 to 7. DMA1 is armed for the 8 macroblocks only. Then
 * the status, at once and 2,000 cycles on; a second picture with no reset; whether it finishes in 2 ms, the status and
 * what DMA0 and DMA1 have left if not. At 24 and 15 bits.
 *
 * READ IT LIKE THIS. K 0 is the control. A busy status with FFFFh words to come and a hung second picture for K 1 to 5 is
 * the mechanism; K 6 fills a macroblock, and shows what the MDEC does with a whole one nobody reads. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR0    0x1F801080u
#define BCR0     0x1F801084u
#define CHCR0    0x1F801088u
#define MADR1    0x1F801090u
#define BCR1     0x1F801094u
#define CHCR1    0x1F801098u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static const int MBS = 8, BLOCKS = MBS * 6;
static uint16_t rl[2048] __attribute__((aligned(4)));
static uint32_t out[MBS * 192] __attribute__((aligned(4)));
static uint32_t quant[32] __attribute__((aligned(4)));
static int16_t scale[64] __attribute__((aligned(4)));
static uint32_t words;

/* Picture data: each block a DC, a few run-levels and the end code, then FE00h to the padded length. */
static void makeStream(int extraBlocks) {
    int n = 0;
    for (int b = 0; b < BLOCKS; b++) {
        rl[n++] = (uint16_t)(0x0800 | ((b * 17) & 0x1FF));
        for (int i = 0; i < 4; i++) rl[n++] = (uint16_t)(((i & 3) << 10) | ((b + i * 5) & 0x3F));
        rl[n++] = 0xFE00;
    }
    for (int b = 0; b < extraBlocks; b++) { rl[n++] = 0x0DFF; rl[n++] = 0xFE00; }
    uint32_t w = (uint32_t)(n + 1) / 2;
    w = (w + 31) & ~31u;
    for (uint32_t i = (uint32_t)n; i < w * 2; i++) rl[i] = 0xFE00;
    words = w;
}

static bool waitDone(uint32_t chcr, uint32_t limit) {
    for (uint32_t t0 = now(); now() - t0 < limit;) if (!(read32(chcr) & 0x01000000u)) return true;
    return false;
}

/* One picture as StrPlayer starts one. */
static void startPicture(uint32_t depthBits, uint32_t outWords) {
    write32(MADR1, (uint32_t)out & 0x1FFFFFu);
    write32(BCR1, ((outWords / 32) << 16) | 32u);
    write32(CHCR1, 0x01000200u);
    write32(MDEC0, 0x30000000u | depthBits | (words & 0xFFFFu));
    write32(MADR0, (uint32_t)rl & 0x1FFFFFu);
    write32(BCR0, ((words / 32) << 16) | 32u);
    write32(CHCR0, 0x01000201u);
}

static void resetAll() {
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    waitCycles(40);
    write32(MDEC1, 0x60000000u);
    write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)quant & 0x1FFFFFu); write32(BCR0, (1u << 16) | 32u); write32(CHCR0, 0x01000201u);
    waitDone(CHCR0, 20000);
    write32(MDEC0, 0x60000000u);
    write32(MADR0, (uint32_t)scale & 0x1FFFFFu); write32(BCR0, (1u << 16) | 32u); write32(CHCR0, 0x01000201u);
    waitDone(CHCR0, 20000);
}

struct Row { uint8_t k, ok, hung; uint32_t at0, at2000, after, bcr0, bcr1; };
static const int KS = 8, TRIES = 8;
static Row rows[2][KS];

static void sweep(Row* r, uint32_t depthBits, uint32_t outWords) {
    for (int k = 0; k < KS; k++) {
        makeStream(k);
        r[k].k = (uint8_t)k; r[k].ok = r[k].hung = 0; r[k].after = r[k].bcr0 = r[k].bcr1 = 0;
        for (int t = 0; t < TRIES; t++) {
            resetAll();
            startPicture(depthBits, outWords);
            if (!waitDone(CHCR1, 67737u)) continue;
            r[k].at0 = read32(MDEC1);
            waitCycles(2000);
            r[k].at2000 = read32(MDEC1);
            startPicture(depthBits, outWords);
            if (waitDone(CHCR1, 67737u)) r[k].ok++;
            else { if (!r[k].hung) { r[k].after = read32(MDEC1); r[k].bcr0 = read32(BCR0); r[k].bcr1 = read32(BCR1); } r[k].hung++; }
        }
    }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-tail\n");
    printf("K blocks behind a picture's last macroblock, then a second picture with no reset. %d tries each. No assertions.\n\n", TRIES);
    for (int i = 0; i < 32; i++) quant[i] = 0x10101010u;
    for (int i = 0; i < 64; i++) scale[i] = (int16_t)(i == 0 ? 0x5A82 : (i % 9 == 0 ? 0x4000 : 0));
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x88u);
    sweep(rows[0], 0x10000000u, MBS * 192);
    sweep(rows[1], 0x18000000u, MBS * 128);
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    static const char* NAMES[2] = { "24 bits", "15 bits" };
    for (int s = 0; s < 2; s++) {
        printf("%s\n", NAMES[s]);
        for (int k = 0; k < KS; k++)
            printf("  K %d: then %08x, 2000 on %08x; second picture done %d hung %d  status %08x  BCR0 %08x BCR1 %08x\n", (int)rows[s][k].k, (unsigned)rows[s][k].at0, (unsigned)rows[s][k].at2000,
                   (int)rows[s][k].ok, (int)rows[s][k].hung, (unsigned)rows[s][k].after, (unsigned)rows[s][k].bcr0, (unsigned)rows[s][k].bcr1);
    }
    printf("\nmdec/mdec-tail: Done.\n");
    for (;;) {}   /* park after Done. */
}
