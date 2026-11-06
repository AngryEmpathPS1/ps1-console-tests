/*
 * gpu/gpu-dma-rdbs - a VRAM-to-RAM read's pace by block size, rectangle width and display, and dma-len's own conditions
 * (2026-11-06 small hours, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-rdlong on the second SCPH-5501: a long read by DMA2 in 16-word blocks runs 34.72 cycles a block
 * wherever it reads and whatever the display shows, and in 8-word blocks 19.69 (both 320 wide, the display on). dma-len on a
 * SCPH-5501 (the first unit, 2026-11-01 and -02; a 64-wide rectangle at (0,256), the display off after GP1(00h), 6,000
 * cycles between GP0(C0h) and the start) reads 16-word blocks at ~34.7 as well, but 8-word ones at 16.5 - the bus's own pace -
 * with no word left over (no block read ahead of the GPU). So the width, the display, the wait before the start or the unit
 * decides an 8-word block's pace.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For each case a short and a long GP0(C0h) read to RAM by DMA2
 * (SyncMode 1, GP1(04h)=3), each timed from CHCR2's store to its bit 24 clear (polled with Timer 2 reads between): the short
 * one 20,000 cycles after the VBlank IRQ (the blanking) and 200,000 after, the long one 200,000 after; four of each. The
 * cases: block sizes 4 / 8 / 16 / 32 at 320 and 64 wide (at (512,0), beside the picture; 320x240 15-bit on); 8-word blocks
 * with the display off (GP1(03h)=1) at both widths, after GP1(00h) and off (dma-len's display), and with 6,000 cycles from
 * GPUSTAT bit 27 to the start (dma-len's wait); and dma-len's own row - 64x64 at (0,256), 8-word blocks, after GP1(00h) and
 * off, the 6,000-cycle wait - against a 64x16 one. A line per case: C <case> ws <short words> wl <long words> bs <block>,
 * then sb, sa, la and su (the long one's setup: the first GP0 word to bit 27), four values each.
 *
 * READ IT LIKE THIS. (la - sa) / ((wl - ws) / bs): a block's pace without the setup. No assertions - compare against your
 * emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdbs --frames=3000 --quiet=1500`.
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

/* disp 0: 320x240 on, 1: off, 2: GP1(00h) then off */
struct Case { const char* name; int disp, x, y, w, hs, hl, bs; uint32_t pre; };
static const Case CASES[] = {
    { "A w320 b16 on       ", 0, 512,   0, 320,  32, 240, 16, 0 },
    { "B w320 b8  on       ", 0, 512,   0, 320,  32, 240,  8, 0 },
    { "C w320 b4  on       ", 0, 512,   0, 320,  32, 240,  4, 0 },
    { "D w320 b32 on       ", 0, 512,   0, 320,  32, 240, 32, 0 },
    { "E w64  b16 on       ", 0, 512,   0,  64, 128, 512, 16, 0 },
    { "F w64  b8  on       ", 0, 512,   0,  64, 128, 512,  8, 0 },
    { "G w64  b4  on       ", 0, 512,   0,  64, 128, 512,  4, 0 },
    { "H w64  b32 on       ", 0, 512,   0,  64, 128, 512, 32, 0 },
    { "I w320 b8  off      ", 1, 512,   0, 320,  32, 240,  8, 0 },
    { "J w64  b8  off      ", 1, 512,   0,  64, 128, 512,  8, 0 },
    { "K w64  b8  reset    ", 2, 512,   0,  64, 128, 512,  8, 0 },
    { "L w64  b8  on wait  ", 0, 512,   0,  64, 128, 512,  8, 6000 },
    { "M dma-len's 8x256   ", 2,   0, 256,  64,  16,  64,  8, 6000 },
};
#define NC (int)(sizeof(CASES) / sizeof(CASES[0]))
static uint32_t res[NC][4][4];   /* [case][sb sa la su][rep] */

static void display(int d) {
    if (d == 2) { write32(GP1, 0x00000000u); write32(GP1, 0x03000001u); return; }
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u | (uint32_t)d);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
}

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
    if (c.pre) while (now() - t1 < c.pre) {}
    write32(D2_MADR, (uint32_t)big & 0xFFFFFCu);
    write32(D2_BCR, ((uint32_t)(words / c.bs) << 16) | (uint32_t)c.bs);
    const uint32_t t2 = now();
    write32(D2_CHCR, 0x01000200u);   /* to RAM, SyncMode 1 */
    while ((read32(D2_CHCR) & 0x01000000u) && now() - t2 < 40000000u) {}
    const uint32_t t3 = now();
    /* what a block size over 16 left in the GPU (read ahead of it): drain GPUREAD while bit 27 holds */
    for (int i = 0; i < 40000 && (read32(GP1) & 0x08000000u); i++) (void)read32(GP0);
    waitReady();
    write32(GP1, 0x04000000u);
    if (su) *su = t1 - t0;
    return t3 - t2;
}

int main() {
    printf("\ngpu/gpu-dma-rdbs\n");
    printf("C case ws wl bs then sb sa la su (cycles, four each: a short read 20,000 cycles after the VBlank IRQ, one 200,000 after, a\n"
           "long one 200,000 after, the long one's setup - its first GP0 word to GPUSTAT bit 27). Timed from CHCR2's store to its bit 24\n"
           "clear. No assertions.\n\n");
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
        display(c.disp);
        toTime(20000); toTime(20000);
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
        const Case& c = CASES[k];
        printf("C %s ws %d wl %d bs %d", c.name, c.w * c.hs / 2, c.w * c.hl / 2, c.bs);
        for (int j = 0; j < 4; j++) { printf(" %s", TN[j]); for (int r = 0; r < 4; r++) printf(" %u", (unsigned)res[k][j][r]); }
        printf("\n");
    }
    printf("\ngpu/gpu-dma-rdbs: Done.\n");
    for (;;) VSync(0);
}
