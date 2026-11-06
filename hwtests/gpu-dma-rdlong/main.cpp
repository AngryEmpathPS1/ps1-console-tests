/*
 * gpu/gpu-dma-rdlong - a long VRAM-to-RAM transfer's pace by where it reads, how wide, the display and the blanking
 * (2026-11-06 small hours, for the second SCPH-5501)
 *
 * WHY. gpu/bandwidth's vramToCpu - 400 reads of 320x240 at (0,0), the picture itself, by DMA2 in 16-word blocks - reads
 * 15,754-15,760 hblanks on every new-GPU console (35.33 cycles a block, its setup and all), and gpu-scan6 - one 128x64
 * read at (512,300), outside the picture - 8,926-8,940 cycles for its 256 blocks on the SCPH-7501 and 9001, setup and
 * all: at most ~34.5 a block. hwtests/gpu-dma-rowdown on the second SCPH-5501 (8,192 words at (0,256), 128 wide, in
 * Unirom's 512x240) reads 34.99 a block by its block count's slope; dma-len (the display off) 34.92 between its 64- and
 * 128-block rows. So a block costs ~0.7 cycles more in bandwidth: by its place (the picture's own lines and columns), its
 * length (2,400 blocks against 256), its width (320 against 128) or its display. The emulator this was written for
 * charges every read 41 cycles of every line, the blanking's as well (the scan-out's pause).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For each case - a display mode, on or off (GP1(03h)), a
 * rectangle's place and width, a block size - a short and a long GP0(C0h) read to RAM by DMA2 (SyncMode 1, GP1(04h)=3),
 * each timed from CHCR2's store to its bit 24 clear, polled with Timer 2: the short one 20,000 cycles after the VBlank
 * IRQ (the blanking) and 200,000 after (active lines), the long one 200,000 after; four of each, in that order. And the
 * setup: from the first GP0 word to GPUSTAT bit 27. The short read is 5,120 words (320 blocks of 16), the long one
 * 38,400 (2,400) at 320 wide and 30,720 (1,920) at 128. A line per case: C <case> then sb (short, blanking), sa (short,
 * active), la (long, active) and su (the long one's setup), four values each.
 *
 * READ IT LIKE THIS. (la - sa) / (the long one's blocks - the short one's): a block's pace on active lines without the
 * setup; sa - sb: the scan-out's toll on the short read. A-B-C differ in the place (the picture, the lines below it, the
 * columns beside it), C-D in the width, A-E in the display (off), A-F-G in the mode (320x240, 512x240, 640x240 24-bit),
 * A-H in the block size (8). No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdlong --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t big[38400] __attribute__((aligned(1024)));

static void toTime(uint32_t x) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < x) {} }
}

struct Case { const char* name; uint32_t mode, off; int x, y, w, hs, hl, bs; };
static const Case CASES[] = {
    { "A 320x240 on   picture  w320 b16", 0x01, 0,   0,   0, 320, 32, 240, 16 },
    { "B 320x240 on   below    w320 b16", 0x01, 0,   0, 256, 320, 32, 240, 16 },
    { "C 320x240 on   beside   w320 b16", 0x01, 0, 512,   0, 320, 32, 240, 16 },
    { "D 320x240 on   beside   w128 b16", 0x01, 0, 512,   0, 128, 80, 480, 16 },
    { "E 320x240 off  picture  w320 b16", 0x01, 1,   0,   0, 320, 32, 240, 16 },
    { "F 512x240 on   below    w320 b16", 0x02, 0,   0, 256, 320, 32, 240, 16 },
    { "G 640x240x24 on picture w320 b16", 0x13, 0,   0,   0, 320, 32, 240, 16 },
    { "H 320x240 on   picture  w320 b8 ", 0x01, 0,   0,   0, 320, 32, 240,  8 },
};
#define NC (int)(sizeof(CASES) / sizeof(CASES[0]))
static uint32_t res[NC][4][4];   /* [case][sb sa la su][rep] */

static uint32_t one(const Case& c, int lng, uint32_t x, uint32_t* su) {
    const int h = lng ? c.hl : c.hs, words = c.w * h / 2;
    waitReady();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000003u);   /* VRAM to CPU */
    toTime(x);
    const uint32_t t0 = now();
    write32(GP0, 0xC0000000u); write32(GP0, xy(c.x, c.y)); write32(GP0, xy(c.w, h));
    while (!(read32(GP1) & 0x08000000u) && now() - t0 < 40000000u) {}
    const uint32_t t1 = now();
    write32(D2_MADR, (uint32_t)big & 0xFFFFFCu);
    write32(D2_BCR, ((uint32_t)(words / c.bs) << 16) | (uint32_t)c.bs);
    const uint32_t t2 = now();
    write32(D2_CHCR, 0x01000200u);   /* to RAM, SyncMode 1 */
    while ((read32(D2_CHCR) & 0x01000000u) && now() - t2 < 40000000u) {}
    const uint32_t t3 = now();
    waitReady();
    write32(GP1, 0x04000000u);
    if (su) *su = t1 - t0;
    return t3 - t2;
}

int main() {
    printf("\ngpu/gpu-dma-rdlong\n");
    printf("C case then sb sa la su (cycles, four each: a short read 20,000 cycles after the VBlank IRQ, one 200,000 after, a long one\n"
           "200,000 after, the long one's setup - its first GP0 word to GPUSTAT bit 27). Short 320 blocks; long 2,400 at w320, 1,920 at\n"
           "w128 (640 / 4,800 at b8). Timed from CHCR2's store to its bit 24 clear. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 38400; i++) big[i] = 0;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    for (int k = 0; k < NC; k++) {
        const Case& c = CASES[k];
        write32(GP1, 0x08000000u | c.mode); write32(GP1, 0x03000000u | c.off);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        toTime(20000); toTime(20000);   /* the new mode's frames settle */
        for (int r = 0; r < 4; r++) {
            res[k][0][r] = one(c, 0, 20000, 0);
            res[k][1][r] = one(c, 0, 200000, 0);
            res[k][2][r] = one(c, 1, 200000, &res[k][3][r]);
        }
    }
    write32(GP1, 0x04000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    static const char* const TN[4] = { "sb", "sa", "la", "su" };
    for (int k = 0; k < NC; k++) {
        printf("C %s", CASES[k].name);
        for (int j = 0; j < 4; j++) { printf(" %s", TN[j]); for (int r = 0; r < 4; r++) printf(" %u", (unsigned)res[k][j][r]); }
        printf("\n");
    }
    printf("\ngpu/gpu-dma-rdlong: Done.\n");
    for (;;) VSync(0);
}
