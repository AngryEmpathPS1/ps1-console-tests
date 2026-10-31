/*
 * mdec/mdec-winddown - a decode started as the last one winds down (2026-10-31, for the SCPH-5501).
 *
 * WHY. FF7's BIKEGET.MOV through psyqo's StrPlayer on the SCPH-7502: 19 of 751 frames never finished, status A405FFFFh, DMA0 3
 * blocks in, DMA1 armed and nothing out, and a reset before every frame cured all of it (STATUS "An MDEC hang the model does
 * not have"). This core has no such hang. The reading: with DMA0 done and the picture out the MDEC can still be busy, and a
 * decode started then hangs it. Nothing has measured how long that lasts or what sets it.
 *
 * WHAT IT DOES. A picture of 8 macroblocks at 24 bits, from run-levels with a few AC codes a block, padded to a whole number
 * of 32-word blocks with FE00h as bsdec pads them. Decode it the way StrPlayer does: the out channel armed for the whole
 * picture, then the command, then DMA0. When DMA1 has finished, wait k cycles and do it again, with no reset. k runs from 0
 * up. Each k from a clean reset: two pictures, 16 tries. How many second pictures finished in 2 ms, how many hung, and the
 * status and DMA0 count of the first hang. Then the same with no padding, and with 64 words of it, and at 15 bits.
 *
 * READ IT LIKE THIS. Hangs that stop at some k are the wind-down's length. Hangs that move with the padding are padding
 * still going in. No assertions.
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
static void makeStream(int padWords) {
    int n = 0;
    for (int b = 0; b < BLOCKS; b++) {
        rl[n++] = (uint16_t)(0x0800 | ((b * 17) & 0x1FF));
        for (int i = 0; i < 4; i++) rl[n++] = (uint16_t)(((i & 3) << 10) | ((b + i * 5) & 0x3F));
        rl[n++] = 0xFE00;
    }
    uint32_t w = (uint32_t)(n + 1) / 2;
    w = (w + 31) & ~31u;
    w += (uint32_t)padWords;
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

struct Row { uint16_t k; uint8_t ok, hung; uint32_t status, bcr0; };
static const uint16_t KS[] = { 0, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192 };
static const int NK = sizeof(KS) / sizeof(KS[0]), TRIES = 16;
static Row rows[4][NK];

static void sweep(Row* r, uint32_t depthBits, uint32_t outWords) {
    for (int i = 0; i < NK; i++) {
        r[i].k = KS[i]; r[i].ok = r[i].hung = 0; r[i].status = 0; r[i].bcr0 = 0;
        for (int t = 0; t < TRIES; t++) {
            resetAll();
            startPicture(depthBits, outWords);
            if (!waitDone(CHCR1, 67737u)) continue;
            if (KS[i]) waitCycles(KS[i]);
            startPicture(depthBits, outWords);
            if (waitDone(CHCR1, 67737u)) r[i].ok++;
            else { if (!r[i].hung) { r[i].status = read32(MDEC1); r[i].bcr0 = read32(BCR0); } r[i].hung++; }
        }
    }
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-winddown\n");
    printf("A second picture k cycles behind the first, no reset between. %d tries each. No assertions.\n\n", TRIES);
    for (int i = 0; i < 32; i++) quant[i] = 0x10101010u;
    for (int i = 0; i < 64; i++) scale[i] = (int16_t)(i == 0 ? 0x5A82 : (i % 9 == 0 ? 0x4000 : 0));
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x88u);
    makeStream(0);  sweep(rows[0], 0x10000000u, MBS * 192);
    makeStream(32); sweep(rows[1], 0x10000000u, MBS * 192);
    makeStream(64); sweep(rows[2], 0x10000000u, MBS * 192);
    makeStream(0);  sweep(rows[3], 0x18000000u, MBS * 128);
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    static const char* NAMES[4] = { "24 bits, padded as bsdec pads", "24 bits, 32 words more padding", "24 bits, 64 words more padding", "15 bits, padded as bsdec pads" };
    for (int s = 0; s < 4; s++) {
        printf("%s\n", NAMES[s]);
        for (int i = 0; i < NK; i++)
            printf("  k %4d: done %2d hung %2d  status %08x  BCR0 %08x\n", (int)rows[s][i].k, (int)rows[s][i].ok, (int)rows[s][i].hung, (unsigned)rows[s][i].status, (unsigned)rows[s][i].bcr0);
    }
    printf("\nmdec/mdec-winddown: Done.\n");
    for (;;) {}   /* park after Done. */
}
