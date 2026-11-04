/*
 * gpu/gpu-a0after - what makes a 4-word GPU upload read ~240 cycles slow in some programs: the same upload, measured the same way,
 * cold, with a VRAM read-back after each transfer, after chopped 16/16 transfers, after GP1(00h), and through gpu-a0mode's own
 * cells (2026-11-04, for the SCPH-5501).
 *
 * WHY. On the second SCPH-5501 2,048 words into a 1,024-wide rectangle at (0,256) - SyncMode 1 blocks of 4 (S4) or chopped 4-word DMA
 * windows at 4-cycle CPU windows (P4/4) - take either ~7,320-7,350 cycles or ~7,560-7,610, never between, run after run: S4 is the
 * quick one in gpu-a0blk, -a0blk2, -a0blk3, -a0phase and -a0vph and the slow one in gpu-a0mode and gpu-a0chop; P4/4 the slow one in
 * gpu-a0blk3 and gpu-a0mode and the quick one in gpu-a0phase and -a0vph. The transfer, its buffer, the rectangle, the fill before it,
 * the start in vblank and the poll loop are the same instruction for instruction (STATUS "The 4-word upload's half cycle a block").
 * What differs: gpu-a0blk3, -a0mode and -a0chop read the rectangle back through GP0(C0h) after every transfer (gpu-a0phase does not),
 * and gpu-a0mode and -a0chop run chopped transfers with 16-word and larger windows before their S4 (gpu-a0blk3 only 4/4). This
 * emulator reads every one of them quick.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks, the display as the runner left it. Each
 * transfer as gpu-a0blk3's: the GPU idle, GP1(01h), a fill of the rectangle, GP1(04h) = 2, GP0(A0h) at (0,256) 1,024x4, MADR2, BCR2,
 * then vblank's first hblank edge + 500 cycles, CHCR2's store and the CHCR2-and-Timer-2 poll; 3,000 cycles; zeros until GPUSTAT bit
 * 26; GP1(04h) = 0. "+rb" after it: the rectangle read back through GP0(C0h), all 2,048 words. Phases:
 *   0 cold: S4 S4 P4/4 P4/4
 *   1 a0blk3's way, a read-back after each: S4+rb S4+rb P4/4+rb P4/4+rb S4+rb S4+rb
 *   2 then without read-backs: S4 S4 P4/4 P4/4 S4 S4
 *   3 after one P16/16 (no read-back): S4 S4 P4/4 P4/4
 *   4 after one P16/16+rb: S4 S4 P4/4 P4/4
 *   5 after GP1(00h) and the E1h-E6h settings again: S4 S4 P4/4 P4/4
 *   6 gpu-a0mode's cells, in vblank, each +rb: P4/4 P4/4 P16/16 P16/16 S4 S4 S16 S16
 *   7 then without read-backs: S4 S4 P4/4 P4/4
 *   8 half a read-back (1,024 of 2,048 words, then GP1(01h)): S4 S4 P4/4 P4/4
 *
 * READ IT LIKE THIS. A phase kind a b rb t left: kind S (a the block's words) or P (a the DMA window's words, b the CPU window's
 * cycles); rb 1 a read-back after it; t the transfer's cycles (CHCR busy, the poll adding Timer 2 up); left the zeros taken (0). The
 * quick reading is ~7,320-7,350, the slow ~7,560-7,610. No assertions - compare against your emulator.
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

/* vblank's first hblank edge, then 500 cycles (gpu-a0blk3's toPlace(0)) */
static void toPlace() {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}

static void settings() {
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
}

/* the rectangle back through GP0(C0h): `words` of its 2,048 (fewer: GP1(01h) after) */
static void readBack(int words) {
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (4u << 16) | 1024u);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    volatile uint32_t sink = 0;
    for (int k = 0; k < words; k++) sink += read32(GP0);
    (void)sink;
    if (words < WORDS) write32(GP1, 0x01000000u);
}

/* kind 'S' SyncMode 1 blocks of a words; 'P' chopped, DMA window 2^a words, CPU window 2^b cycles */
static void one(int phase, char kind, int a, int b, int rb) {
    const int w = 1024, h = 4;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? (((uint32_t)(WORDS / a) << 16) | (uint32_t)a) : (uint32_t)WORDS);
    const uint32_t chcr = kind == 'S' ? 0x01000201u : (0x11000101u | ((uint32_t)a << 16) | ((uint32_t)b << 20));
    toPlace();
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int k = 0; k < 8; k++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    if (rb) readBack(WORDS);
    printf("A %d %c %d %d %d %u %u\n", phase, kind, kind == 'P' ? 1 << a : a, kind == 'P' ? 1 << b : 0, rb, (unsigned)t, (unsigned)left);
}

static void four(int phase) { one(phase, 'S', 4, 0, 0); one(phase, 'S', 4, 0, 0); one(phase, 'P', 2, 2, 0); one(phase, 'P', 2, 2, 0); }

int main() {
    printf("\ngpu/gpu-a0after\n");
    printf("A phase kind a b rb t left (2,048 words into 1,024x4 at (0,256) in vblank; S blocks of a, P chopped a words / b cycles; rb: read back after). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    settings();
    four(0);
    one(1, 'S', 4, 0, 1); one(1, 'S', 4, 0, 1); one(1, 'P', 2, 2, 1); one(1, 'P', 2, 2, 1); one(1, 'S', 4, 0, 1); one(1, 'S', 4, 0, 1);
    four(2); one(2, 'S', 4, 0, 0); one(2, 'S', 4, 0, 0);
    one(3, 'P', 4, 4, 0); four(3);
    one(4, 'P', 4, 4, 1); four(4);
    gpuIdle(); write32(GP1, 0x00000000u); settings(); four(5);
    one(6, 'P', 2, 2, 1); one(6, 'P', 2, 2, 1); one(6, 'P', 4, 4, 1); one(6, 'P', 4, 4, 1);
    one(6, 'S', 4, 0, 1); one(6, 'S', 4, 0, 1); one(6, 'S', 16, 0, 1); one(6, 'S', 16, 0, 1);
    four(7);
    readBack(WORDS / 2); four(8);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0after: Done.\n");
    for (;;) VSync(0);
}
