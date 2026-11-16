/*
 * mdec/mdec-tail2 - the MDEC's status, change by change, from just before DMA1's end to 3,000 cycles after it, with K blocks behind the
 * last macroblock DMA1 reads (2026-11-16, the MDEC session; STATUS "mdectaillost").
 *
 * WHY. hwtests/mdec-tail reads the status twice after the picture: just as DMA1 ends ("then") and 2,000 cycles on. The PSone's reads
 * (seven consoles alike since 2026-10-31): at DMA1's end every K from 2 up holds the next macroblock's words - E404FFFFh, data-in full,
 * block Cr - and K 1 (a Cr and padding) does not, A404FFFFh; 2,000 cycles on K 5 (Cr, Cb, Y1-Y3 and padding) has eaten its padding,
 * 2C01FFFFh, and K 6 (Y4's words next) still holds them, 6C01FFFFh. This core's decoder takes the next block the instant the slot frees,
 * and so reads A400FFFFh / A405FFFFh where the consoles read E404FFFFh. Between ~20 cycles and 2,000 the decoder takes its next block,
 * and the stage eats padding behind a finished block: when, and in what steps, is what this times.
 *
 * WHAT IT DOES. mdec-tail's picture (8 macroblocks, K more blocks of DC 0DFFh and an end of block, FE00h to a 32-word block), DMA1
 * armed for the 8 macroblocks only, DMA0 for the whole stream, as StrPlayer starts one. From the command a tight loop reads the
 * status, DMA1's CHCR and Timer 2 (the system clock) and keeps each change (status or DMA1's busy bit) with its time; it runs
 * until 3,000 cycles after DMA1's busy bit drops. Printed: the changes from 600 cycles before DMA1's end, times FROM DMA1's end
 * (the first sample that saw it clear). K 0 to 7, 24 and 15 bits, two tries each (the second a repeat), a reset before each.
 *   T <bits> K <k> try <n> loop <cycles a sample, average>
 *   C <time from DMA1's end> <status> <dma1 busy>
 * No second picture is sent (mdec-tail has that). No assertions. Run it here first: node tools/hwrunner.js mdec-tail2 --emu
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

/* mdec-tail's picture data, unchanged: each block a DC, a few run-levels and the end code, then FE00h to the padded length. */
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

static const int NREC = 512;
static uint32_t rt[NREC], rs[NREC]; static uint8_t rb[NREC];

/* One picture; returns the changes kept, *end the index of the first sample with DMA1 clear (or -1), *loop the average sample. */
static int timeline(uint32_t depthBits, uint32_t outWords, int* end, uint32_t* loop) {
    write32(MADR1, (uint32_t)out & 0x1FFFFFu);
    write32(BCR1, ((outWords / 32) << 16) | 32u);
    write32(CHCR1, 0x01000200u);
    write32(MDEC0, 0x30000000u | depthBits | (words & 0xFFFFu));
    write32(MADR0, (uint32_t)rl & 0x1FFFFFu);
    write32(BCR0, ((words / 32) << 16) | 32u);
    write32(CHCR0, 0x01000201u);
    const uint32_t t0 = now();
    int n = 0; *end = -1; uint32_t lastS = 0xFFFFFFFFu; uint8_t lastB = 2; uint32_t tEnd = 0, samples = 0;
    for (;;) {
        const uint32_t s = read32(MDEC1);
        const uint8_t b = (read32(CHCR1) >> 24) & 1;
        const uint32_t t = now() - t0;
        samples++;
        if (s != lastS || b != lastB) {
            if (n < NREC) { rt[n] = t; rs[n] = s; rb[n] = b; if (!b && *end < 0) { *end = n; tEnd = t; } n++; }
            else if (!b && *end < 0) { rt[NREC - 1] = t; rs[NREC - 1] = s; rb[NREC - 1] = b; *end = NREC - 1; tEnd = t; }
            lastS = s; lastB = b;
        }
        if (*end >= 0 && t - tEnd >= 3000) break;
        if (t > 4000000u) break;
    }
    *loop = samples ? (now() - t0) / samples : 0;
    return n;
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-tail2\n");
    printf("The MDEC's status change by change around DMA1's end, K blocks behind its last macroblock (Timer 2, the system clock). No assertions.\n\n");
    for (int i = 0; i < 32; i++) quant[i] = 0x10101010u;
    for (int i = 0; i < 64; i++) scale[i] = (int16_t)(i == 0 ? 0x5A82 : (i % 9 == 0 ? 0x4000 : 0));
    static const uint32_t DEPTH[2] = { 0x10000000u, 0x18000000u };
    static const uint32_t OUTW[2] = { MBS * 192, MBS * 128 };
    static const char* const NAMES[2] = { "24", "15" };
    for (int s = 0; s < 2; s++) {
        for (int k = 0; k < 8; k++) {
            makeStream(k);
            for (int tr = 0; tr < 2; tr++) {
                EnterCriticalSection();
                const uint16_t m2 = read16(T2_MODE);
                write16(T2_MODE, 0);
                lastT = read16(T2_COUNT); clk = 0;
                write32(DPCR, read32(DPCR) | 0x88u);
                resetAll();
                int end; uint32_t loop;
                const int n = timeline(DEPTH[s], OUTW[s], &end, &loop);
                write32(CHCR0, 0); write32(CHCR1, 0);
                write32(MDEC1, 0x80000000u);
                write16(T2_MODE, m2);
                ExitCriticalSection();
                printf("T %s K %d try %d loop %u\n", NAMES[s], k, tr, (unsigned)loop);
                if (end < 0) { printf("  DMA1 never ended\n"); continue; }
                const uint32_t te = rt[end];
                for (int i = 0; i < n; i++) {
                    if (rt[i] + 600 < te) continue;
                    printf("  C %d %08x %d\n", (int)rt[i] - (int)te, (unsigned)rs[i], (int)rb[i]);
                }
            }
        }
    }
    printf("\nmdec/mdec-tail2: Done.\n");
    for (;;) {}   /* park after Done. */
}
