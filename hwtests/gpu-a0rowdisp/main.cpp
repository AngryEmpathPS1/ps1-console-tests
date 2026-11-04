/*
 * gpu/gpu-a0rowdisp - a GPU upload's row cost by DISPLAY: SyncMode 1 blocks into rows of 16 to 512 words with the display off, at
 * 320x240 and at 640x480 interlaced (2026-11-04, for the SCPH-5501).
 *
 * WHY. ps1-tests' dma/chopping (640x480 interlaced) uploads 2,048 words into a 64x64 rectangle - 64 rows of 32 words - and its 16-word
 * blocks take 3,607 cycles on the console where gpu-a0mode's 2,048 into a 1,024-wide one (4 rows) take 3,483-3,502: ~2 cycles a row
 * start. hwtests/gpu-dma-gaps2 measured +1 a row start for a 16-word block, as this emulator charges it, and this emulator reads 3,526
 * there. Since v0.377.269 its SyncMode 1 rows read ~1% fast at every block size (STATUS v0.377.269). Rows and the scan-out side by side
 * say which one the extra belongs to.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks. For each display - off (GP1(03h) 1),
 * 320x240 (GP1(08h) 01h) and 640x480 interlaced (27h), the reset's ranges - and each row of W = 16, 32, 64, 128 and 512 words (32 to
 * 1,024 pixels, 2,048 / W rows), 2,048 words (word i's pixels i + 1) by DMA2 into GP0(A0h) at (0,256), SyncMode 1 blocks of 16 and of
 * 8 (request mode 2), started 500 cycles past the 40th hblank edge below vblank's start. Four times each. The time to 32 bits (the
 * poll loop adds Timer 2 up); then zeros until GPUSTAT bit 26 (none should be wanted).
 *
 * READ IT LIKE THIS. R disp W bs rep t left: disp 0 off, 1 320x240, 2 640x480i. No assertions - compare against your emulator.
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
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* 500 cycles past the 40th hblank edge below vblank's start */
static void toLine() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}

static void one(int disp, int wWords, int bs, int rep) {
    const int w = wWords * 2, h = WORDS / wWords;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((uint32_t)(WORDS / bs) << 16) | (uint32_t)bs);
    toLine();
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, 0x01000201u);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    printf("R %d %d %d %d %u %u\n", disp, wWords, bs, rep, (unsigned)t, (unsigned)left);
}

int main() {
    printf("\ngpu/gpu-a0rowdisp\n");
    printf("R disp W bs rep t left (2,048 words into rows of W words by SyncMode 1 blocks of bs; disp 0 off, 1 320x240, 2 640x480i). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const uint32_t MODE[3] = { 0x01u, 0x01u, 0x27u };   /* off (at 320x240), 320x240, 640x480 interlaced */
    static const int WS[5] = { 16, 32, 64, 128, 512 };
    static const int BS[2] = { 16, 8 };
    for (int d = 0; d < 3; d++) {
        write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);   /* the reset's ranges, as the runner leaves them */
        write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
        write32(GP1, 0x08000000u | MODE[d]);
        write32(GP1, 0x03000000u | (d == 0 ? 1u : 0u));       /* the display off, or on */
        for (int wi = 0; wi < 5; wi++) for (int b = 0; b < 2; b++) for (int r = 0; r < 4; r++) one(d, WS[wi], BS[b], r);
    }
    write32(GP1, 0x03000000u);
    write32(GP1, 0x08000000u | 0x01u);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0rowdisp: Done.\n");
    for (;;) VSync(0);
}
