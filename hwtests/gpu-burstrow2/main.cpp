/*
 * gpu/gpu-burstrow2 - where a RAM-to-VRAM burst's overrun lands in rows that are NOT a multiple of 16 words: widths of 4 to 120
 * words, widths in odd pixels, odd x (2026-11-11)
 *
 * WHY. hwtests/gpu-burstrow on the second SCPH-5501 (six runs): a burst that overruns GP0's FIFO while the scan-out holds VRAM loses
 * its words where the upload intake stood, and that is the rectangle's row words 2, 18, 34, 51, 67 ... at every width that is a
 * multiple of 16 words - but 10, 26, 34 in a 40-word row, never 18, and seldom 2 as a burst's first gap. One reading fits every
 * width run so far: the intake stands a fixed COUNT of words (34, or 35 with no row start among them) ahead of what has been written
 * to VRAM, and VRAM is written in units of 16 words counted from each row's first word, the row's last unit short. (40-word row:
 * written 0, 16, 32, 40 -> the intake at 34, 50, 66, 74 = row words 34, 10, 26, 34.) The other readings (units counted from the
 * row's end; the intake two UNITS ahead) do not fit 40 but had no other width to fail on. This asks on the widths where they part,
 * and in pixels: a width of an odd number of pixels starts every other row in the middle of a DMA word, and an odd x makes psx-spx's
 * odd case ("store 16+1 ... drain 15"), so the count and the row start's cost can be read to the pixel.
 *
 * WHAT IT DOES. As hwtests/gpu-burstrow (interrupts off, no disc, Timer 2 at the system clock, the display as the runner leaves it,
 * a GPUSTAT bit-31 change = a line edge, then d = 0, 32 .. 2,144 cycles, then DMA2 as one burst to GP0(A0h) at (x0,256); 3,000
 * cycles on, zeros to GP0 until bit 26 sets, the rectangle read back through GP0(C0h)). Each word's two pixels are its own index.
 * A configuration is a width in PIXELS and an x0; its height is the most rows that keep the rectangle within 1,024 words and 256
 * rows (an even number of rows when the width is odd, so the last word is whole). The table is CF[] below; the C lines print it.
 *
 * READ IT LIKE THIS. C c wpx x0 h: configuration c is wpx pixels wide at x0, h rows. R c d t left lost dup ng | position:size ...:
 * as gpu-burstrow - t the transfer's cycles, left the zeros it still took, lost the indices never found, dup the read-back words
 * lower than expected and not zero (stored twice), ng the gaps, then up to 12 gaps as (read-back position in words, indices
 * skipped). Twice a gap's position, less the pixels of the rows before it, is the row pixel at which the intake stood.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-burstrow2 --frames=8000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define MAXW 1024
#define MAXG 12

struct Cfg { short wpx, x0; };
static const Cfg CF[] = {
    /*  0 controls         */ { 256, 0 }, { 80, 0 }, { 64, 0 }, { 160, 0 },
    /*  4 words = 8 mod 16 */ { 48, 0 }, { 112, 0 }, { 144, 0 }, { 176, 0 }, { 208, 0 }, { 240, 0 },
    /* 10 words = 4 mod 8  */ { 24, 0 }, { 40, 0 }, { 56, 0 }, { 72, 0 }, { 88, 0 }, { 104, 0 },
    /* 16 the edges: 17 18 19, 33 34 35, 49 50 51 words */
    { 34, 0 }, { 36, 0 }, { 38, 0 }, { 66, 0 }, { 68, 0 }, { 70, 0 }, { 98, 0 }, { 100, 0 }, { 102, 0 },
    /* 25 narrow           */ { 8, 0 }, { 16, 0 }, { 32, 0 },
    /* 28 odd x            */ { 256, 1 }, { 256, 3 }, { 256, 5 }, { 512, 1 }, { 128, 1 }, { 96, 1 }, { 80, 1 }, { 64, 1 }, { 112, 1 },
    /* 37 odd widths       */ { 255, 0 }, { 257, 0 }, { 127, 0 }, { 129, 0 }, { 95, 0 }, { 97, 0 }, { 79, 0 }, { 81, 0 },
                              { 63, 0 }, { 65, 0 }, { 31, 0 }, { 33, 0 },
    /* 49 odd both         */ { 255, 1 }, { 257, 1 }, { 79, 1 }, { 81, 1 },
};
#define NCF ((int)(sizeof(CF) / sizeof(CF[0])))

static uint32_t buf[MAXW] __attribute__((aligned(16)));
static uint16_t back[MAXW * 2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static int rowsOf(int wpx) { int h = (MAXW * 2) / wpx; if (h > 256) h = 256; if (wpx & 1) h &= ~1; return h; }

static void one(int c, uint32_t d) {
    const Cfg& f = CF[c];
    const int w = f.wpx, h = rowsOf(w), words = (w * h + 1) >> 1;
    const uint32_t at = (256u << 16) | (uint32_t)f.x0, size = ((uint32_t)h << 16) | (uint32_t)w;
    int fw = (f.x0 + w + 15) & ~15; if (fw > 1024) fw = 1024;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)fw);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, at); write32(GP0, size);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (uint32_t)words);
    /* a line edge: GPUSTAT bit 31 changes (bounded), then d cycles */
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, 0x11000001u);
    uint32_t polls = 0;
    while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    if (polls >= 200000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 4096) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 4096) { write32(GP1, 0x01000000u); }
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, at); write32(GP0, size);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int k = 0; k < words; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    int lost = 0, dup = 0, ng = 0, g[MAXG], n[MAXG], expect = 0;
    for (int k = 0; k < words; k++) {                           /* past the last index too: a tail stored twice counts as dup */
        const int got = back[2 * k];
        if (got == expect) { expect++; continue; }
        if (got > expect && got < words) {
            const int skip = got - expect;
            lost += skip;
            if (ng < MAXG) { g[ng] = k; n[ng] = skip; }
            ng++;
            expect = got + 1;
        } else if (got) dup++;
    }
    if (expect < words) lost += words - expect;
    printf("R %d %u %u %u %d %d %d |", c, (unsigned)d, (unsigned)t, (unsigned)left, lost, dup, ng);
    for (int i = 0; i < ng && i < MAXG; i++) printf(" %d:%d", g[i], n[i]);
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-burstrow2\n");
    printf("C c wpx x0 h: configuration c is a rectangle wpx PIXELS wide at (x0,256), h rows, sent as one DMA burst\n");
    for (int c = 0; c < NCF; c++) printf("C %d %d %d %d\n", c, CF[c].wpx, CF[c].x0, rowsOf(CF[c].wpx));
    printf("R c d t left lost dup ng | position:size ... (d = cycles after a line edge; position in words of two pixels). No assertions.\n\n");
    for (int i = 0; i < MAXW; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int c = 0; c < NCF; c++) for (uint32_t d = 0; d < 2150; d += 32) one(c, d);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-burstrow2: Done.\n");
    for (;;) VSync(0);
}
