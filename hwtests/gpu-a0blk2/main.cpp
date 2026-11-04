/*
 * gpu/gpu-a0blk2 - gpu-a0blk's uploads under the display writes gpu-a0chop, -a0lsf and -a0mode make (GP1(06h), 07h, 08h), inside
 * vblank (2026-11-04, for the SCPH-5501).
 *
 * WHY. On the second SCPH-5501, 2,048 words of 4-word SyncMode 1 blocks into a 1,024-wide rectangle at (0,256) inside vblank take
 * 7,327 cycles in hwtests/gpu-a0blk - this emulator's 7,326 - and 7,564-7,575 in hwtests/gpu-a0mode, the same upload, the same poll
 * loop (both register-only, the disassemblies compared). gpu-a0chop's 4-word blocks were slow too (15.2 cycles a block); gpu-a0blk
 * and gpu-dma-quiet are fast. What the slow programs share is writing the display's ranges and mode themselves (GP1(06h) 200h-C00h,
 * GP1(07h) 10h-100h, GP1(08h)); the fast ones leave the runner's (Unirom's) display or set it through the SDK. This puts the same
 * uploads under each of those writes.
 *
 * WHAT IT DOES. As gpu-a0blk (interrupts off, each upload 500 cycles past vblank's first hblank edge, timed to 32 bits by a poll of
 * CHCR2 and Timer 2), cells 2,048 words of 4-word blocks at 1,024 wide (x 0) and 64 wide (x 512) and of 16-word blocks at 1,024 wide,
 * four times each, under: E0 the display as the runner left it; E1 GP1(08h) 01h alone (320x240); E2 GP1(06h) 200h-C00h and GP1(07h)
 * 10h-100h and 08h 01h (gpu-a0chop's and gpu-a0lsf's); E3 the ranges and 08h 02h (512 wide); E4 the ranges and 08h 00h (256 wide). In
 * that order: a write stays.
 *
 * READ IT LIKE THIS. E env W B w x rep t lost. No assertions - compare against your emulator.
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

static void one(int env, int words, int bs, int w, int x, int rep) {
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
    printf("E %d %d %d %d %d %d %u %d\n", env, words, bs, w, x, rep, (unsigned)t, lost);
}

int main() {
    printf("\ngpu/gpu-a0blk2\n");
    printf("E env W B w x rep t lost (gpu-a0blk's uploads under display writes E0-E4, started in vblank). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    struct C { int words, bs, w, x; };
    static const C CS[3] = { { 2048, 4, 1024, 0 }, { 2048, 4, 64, 512 }, { 2048, 16, 1024, 0 } };
    for (int env = 0; env < 5; env++) {
        if (env == 1) write32(GP1, 0x08000001u);
        if (env >= 2) {
            write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);
            write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
            write32(GP1, 0x08000000u | (env == 2 ? 0x01u : env == 3 ? 0x02u : 0x00u));
        }
        for (int c = 0; c < 3; c++) for (int r = 0; r < 4; r++) one(env, CS[c].words, CS[c].bs, CS[c].w, CS[c].x, r);
    }
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0blk2: Done.\n");
    for (;;) VSync(0);
}
