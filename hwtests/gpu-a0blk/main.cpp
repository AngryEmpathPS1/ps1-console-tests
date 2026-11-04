/*
 * gpu/gpu-a0blk - a SyncMode 1 GP0(A0h) upload's time by the rectangle's width and place, its block size and its length, inside vblank
 * (2026-11-04, for the SCPH-5501).
 *
 * WHY. The second SCPH-5501: 2,048 words by 4-word blocks take 7,280 cycles into a 64-wide rectangle at (512,256) (hwtests/gpu-dma-quiet)
 * and 7,564-7,577 into a 1,024-wide one at (0,256) (hwtests/gpu-a0mode, inside vblank too, the same poll) - ~0.57 a block, which this
 * emulator does not charge (it reads 7,317-7,350). gpu-dma-rowx found the row's length alone sets the cost for rows of 4-32 words; this
 * asks whether a wide rectangle, its place in VRAM, or the transfer's length makes the difference, with no scan-out in the way.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks. Each upload starts 500 cycles after
 * vblank's first hblank edge (no scan-out through it) and is timed to 32 bits by a poll of CHCR2 and Timer 2. Words are i + 1 in both
 * pixels. Cells: W words into a rectangle w x h at (x,256) by B-word blocks (GP1(04h)=2) - 2,048 words of 4-word blocks at w 64, 128,
 * 256, 512 and 1,024 (64, 256 and 512 at x 0 and 512, the rest at 0), 2,048 of 16-word blocks at 64 (x 512) and 1,024 wide, and
 * 512 / 1,024 / 2,048 words of 4-word blocks at 1,024 wide (the length). Four times each, then the rectangle read back: words lost.
 *
 * READ IT LIKE THIS. K W B w x rep t lost: t the transfer's cycles. No assertions - compare against your emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static uint8_t seen[WORDS + 1];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void toVblank() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}

static void one(int words, int bs, int w, int x, int rep) {
    const int h = (words * 2 + w - 1) / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | (uint32_t)x); write32(GP0, ((uint32_t)h << 16) | (uint32_t)((w + 15) & ~15));
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | (uint32_t)x); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(words / bs) << 16) | (uint32_t)bs);
    toVblank();
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, 0x01000201u);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    for (int i = 0; i < 8192 && !(read32(GP1) & (1u << 26)); i++) { write32(GP0, 0); for (volatile int k = 0; k < 8; k++) {} }
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | (uint32_t)x); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int i = 0; i <= WORDS; i++) seen[i] = 0;
    const int back = w * h / 2;
    for (int k = 0; k < back; k++) { const uint32_t v = read32(GP0); const int a = v & 0xFFFF; if (a >= 1 && a <= words) seen[a] = 1; }
    int lost = 0;
    for (int i = 1; i <= words; i++) if (!seen[i]) lost++;
    printf("K %d %d %d %d %d %u %d\n", words, bs, w, x, rep, (unsigned)t, lost);
}

int main() {
    printf("\ngpu/gpu-a0blk\n");
    printf("K W B w x rep t lost (W words by B-word SyncMode 1 blocks into a w-wide rectangle at (x,256), started in vblank). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    struct C { int words, bs, w, x; };
    static const C CS[] = {
        { 2048, 4, 64, 512 }, { 2048, 4, 64, 0 }, { 2048, 4, 128, 0 }, { 2048, 4, 256, 0 }, { 2048, 4, 256, 512 },
        { 2048, 4, 512, 0 }, { 2048, 4, 512, 512 }, { 2048, 4, 1024, 0 },
        { 2048, 16, 64, 512 }, { 2048, 16, 1024, 0 },
        { 512, 4, 1024, 0 }, { 1024, 4, 1024, 0 },
    };
    for (unsigned c = 0; c < sizeof CS / sizeof CS[0]; c++) for (int r = 0; r < 4; r++) one(CS[c].words, CS[c].bs, CS[c].w, CS[c].x, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0blk: Done.\n");
    for (;;) VSync(0);
}
