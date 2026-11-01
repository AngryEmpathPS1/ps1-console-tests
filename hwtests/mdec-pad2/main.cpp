/*
 * mdec/mdec-pad2 - mdec-pad at picture sizes of 2, 4, 8 and 16 macroblocks: is the padding that ends the hang a count of its own or the frame's length? (2026-10-31)
 *
 * WHY. mdec-tail: one block behind a picture, DC 0DFFh, a few FE00h behind it, ends A405FFFFh, busy, and the next picture hangs.
 * The movie player with a block of DC 0000h put behind every frame of The X-Files' FOXLOGO.STR, ~6,500 words of FE00h behind it,
 * ends 8405FFFFh, 8400FFFFh, 2C01FFFFh by turns: not busy on most, and the pictures come out scrambled. FF7's BIKEGET.MOV, whose
 * extra word is the end code, ends busy (STATUS "An MDEC a block out of step is not busy"). So is it the padding or the word?
 *
 * WHAT IT DOES. mdec-tail's picture of 8 macroblocks at 24 bits, K blocks behind it (1 or 2) with DC 0DFFh or 0000h, padded with
 * FE00h to P words past the 32-word boundary (P = 0 to 6,400), and K 0 as the control at each P. DMA1 armed for the 8
 * macroblocks only. The status at once and when DMA0 has finished, 2,000 cycles on; a second picture with no reset; whether it
 * finishes in 2 ms; the words of its output that differ from the clean picture's. 8 tries each, the last try's numbers printed.
 *
 * READ IT LIKE THIS. Busy and hung is what mdec-tail showed. Not busy, done and words different is what the player showed.
 * The rows say which of the word and the padding decides it. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js mdec-pad`.
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

static int MBS = 8, BLOCKS = 48, OUTW = 1536;
static const int MAXOUT = 16 * 192;
static uint16_t rl[15000] __attribute__((aligned(4)));
static uint32_t out[MAXOUT] __attribute__((aligned(4)));
static uint32_t clean[MAXOUT] __attribute__((aligned(4)));
static uint32_t quant[32] __attribute__((aligned(4)));
static int16_t scale[64] __attribute__((aligned(4)));
static uint32_t words;

static void makeStream(int extraBlocks, uint16_t dc, uint32_t padWords) {
    int n = 0;
    for (int b = 0; b < BLOCKS; b++) {
        rl[n++] = (uint16_t)(0x0800 | ((b * 17) & 0x1FF));
        for (int i = 0; i < 4; i++) rl[n++] = (uint16_t)(((i & 3) << 10) | ((b + i * 5) & 0x3F));
        rl[n++] = 0xFE00;
    }
    for (int b = 0; b < extraBlocks; b++) { rl[n++] = dc; rl[n++] = 0xFE00; }
    uint32_t w = (uint32_t)(n + 1) / 2;
    w = ((w + 31) & ~31u) + padWords;
    for (uint32_t i = (uint32_t)n; i < w * 2; i++) rl[i] = 0xFE00;
    words = w;
}

static bool waitDone(uint32_t chcr, uint32_t limit) {
    for (uint32_t t0 = now(); now() - t0 < limit;) if (!(read32(chcr) & 0x01000000u)) return true;
    return false;
}

static void startPicture() {
    write32(MADR1, (uint32_t)out & 0x1FFFFFu);
    write32(BCR1, ((OUTW / 32) << 16) | 32u);
    write32(CHCR1, 0x01000200u);
    write32(MDEC0, 0x30000000u | 0x10000000u | (words & 0xFFFFu));
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

struct Row { uint16_t dc; uint8_t mbs, k, ok, hung; uint32_t pad, at0, feed, at2000, after, differ; };
static const int TRIES = 8;
static Row rows[40];
static int nRows = 0;

static void cell(int k, uint16_t dc, uint32_t pad) {
    Row& r = rows[nRows++];
    r.mbs = (uint8_t)MBS; r.k = (uint8_t)k; r.dc = dc; r.pad = pad; r.ok = r.hung = 0; r.after = 0; r.differ = 0;
    makeStream(k, dc, pad);
    for (int t = 0; t < TRIES; t++) {
        resetAll();
        startPicture();
        if (!waitDone(CHCR1, 67737u)) continue;
        r.at0 = read32(MDEC1);
        r.feed = read32(CHCR0) >> 24;
        waitDone(CHCR0, 200000u);
        waitCycles(2000);
        r.at2000 = read32(MDEC1);
        startPicture();
        if (waitDone(CHCR1, 67737u)) {
            r.ok++;
            r.after = read32(MDEC1);
            uint32_t d = 0;
            for (int i = 0; i < OUTW; i++) if (out[i] != clean[i]) d++;
            r.differ = d;
        } else { r.after = read32(MDEC1); r.hung++; }
    }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-pad2\n");
    printf("K blocks of DC d behind a picture, P more words of padding, then a second picture with no reset. %d tries each. No assertions.\n\n", TRIES);
    for (int i = 0; i < 32; i++) quant[i] = 0x10101010u;
    for (int i = 0; i < 64; i++) scale[i] = (int16_t)(i == 0 ? 0x5A82 : (i % 9 == 0 ? 0x4000 : 0));
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x88u);
    static const int SIZES[4] = { 2, 4, 8, 16 };
    static const uint32_t PADS[9] = { 0, 32, 64, 96, 128, 160, 192, 256, 512 };
    for (int z = 0; z < 4; z++) {
        MBS = SIZES[z]; BLOCKS = MBS * 6; OUTW = MBS * 192;
        makeStream(0, 0, 0);
        resetAll();
        startPicture();
        waitDone(CHCR1, 67737u);
        for (int i = 0; i < OUTW; i++) clean[i] = out[i];
        for (int p = 0; p < 9; p++) cell(1, 0x0DFF, PADS[p]);
    }
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int i = 0; i < nRows; i++) {
        const Row& r = rows[i];
        printf("  MB %2d words %4d P %4d K %d DC %04x: then %08x DMA0 %02x, settled %08x; second picture done %d hung %d, then %08x, words off %d\n", (int)r.mbs, (int)((r.mbs * 36 + 1 + 31) / 32 * 32 + r.pad), (int)r.pad, (int)r.k,
               (unsigned)r.dc, (unsigned)r.at0, (unsigned)r.feed, (unsigned)r.at2000, (int)r.ok, (int)r.hung, (unsigned)r.after, (int)r.differ);
    }
    printf("\nmdec/mdec-pad2: Done.\n");
    for (;;) {}   /* park after Done. */
}
