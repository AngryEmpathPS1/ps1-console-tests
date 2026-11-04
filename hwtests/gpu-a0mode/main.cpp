/*
 * gpu/gpu-a0mode - a GP0(A0h) transfer's time and losses by DISPLAY MODE: does the scan-out set the DMA's own pace? (2026-11-04, for
 * the SCPH-5501)
 *
 * WHY. hwtests/gpu-a0chop on the second SCPH-5501, at 320x240 with CHCR2 polled: 4-word chopped windows with a 4-cycle CPU window take
 * ~15.1 cycles a window and SyncMode 1 4-word blocks ~15.2 a block, where this emulator takes ~14.4 for both - a chopped transfer has
 * no data request, so whatever it is acts on the DMA's own pace - the scan-out on its writes, or the CPU's polls in the gaps
 * (gpu-dma-quiet's quiet-timed 4-word blocks, the CPU off the bus, fit). One program at every width says whether the display moves them, and what the scan-out costs a
 * block upload at each width (STATUS v0.377.268's KNOWN-OPEN).
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks. For each display - 256, 320, 368,
 * 512 and 640 wide (GP1(08h), NTSC, 240 lines, 15-bit, the reset's ranges), and 512 wide with the transfer started inside vblank -
 * 2,048 words (word i's pixels i + 1) by DMA2 into GP0(A0h) at (0,256), 1,024 wide (four rows), started 40 hblanks below vblank's
 * start and 500 cycles past an hblank edge (vblank's: its first hblank edge), by: chopped SyncMode 0, 4-word windows at 4 cycles and
 * 16 at 16; SyncMode 1 blocks of 4 and of 16 (request mode 2); a burst. Four times each. The time to 32 bits (the poll loop adds
 * Timer 2 up), then 3,000 cycles, zeros until GPUSTAT bit 26, the rectangle read back through GP0(C0h): lost words and words read
 * twice.
 *
 * READ IT LIKE THIS. M width vb kind a b rep t left lost dup: width the display's, vb 1 when started in vblank, kind P chopped (a words,
 * b cycles), S SyncMode 1 blocks of a, B burst. No assertions - compare against your emulator.
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

/* 500 cycles past the 40th hblank edge below vblank's start, or (vb) past vblank's own first one */
static void toLine(int vb) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    if (!vb) while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
}

static void one(int width, uint32_t mode, int vb, char kind, int a, int b, int rep) {
    const int w = 1024, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? (((uint32_t)(WORDS / a) << 16) | (uint32_t)a) : (uint32_t)WORDS);
    const uint32_t chcr = kind == 'B' ? 0x11000001u : kind == 'P' ? (0x11000101u | ((uint32_t)a << 16) | ((uint32_t)b << 20)) : 0x01000201u;
    toLine(vb);
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 8192) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 8192) write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int i = 0; i <= WORDS; i++) seen[i] = 0;
    int dup = 0;
    for (int k = 0; k < WORDS; k++) { const uint32_t v = read32(GP0); const int x = v & 0xFFFF; if (x >= 1 && x <= WORDS) { if (seen[x]) dup++; if (seen[x] < 255) seen[x]++; } }
    int lost = 0;
    for (int i = 1; i <= WORDS; i++) if (!seen[i]) lost++;
    printf("M %d %d %c %d %d %d %u %u %d %d\n", width, vb, kind, kind == 'P' ? 1 << a : a, kind == 'P' ? 1 << b : b, rep, (unsigned)t, (unsigned)left, lost, dup);
    (void)mode;
}

int main() {
    printf("\ngpu/gpu-a0mode\n");
    printf("M width vb kind a b rep t left lost dup (2,048 words, 1,024 wide; P chopped a words / b cycles, S SyncMode 1 blocks of a, "
           "B burst; vb 1: started in vblank). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    struct D { int width; uint32_t mode; int vb; };
    static const D DS[6] = { { 256, 0x00, 0 }, { 320, 0x01, 0 }, { 368, 0x40, 0 }, { 512, 0x02, 0 }, { 640, 0x03, 0 }, { 512, 0x02, 1 } };
    struct K { char k; int a, b; };
    static const K KS[5] = { { 'P', 2, 2 }, { 'P', 4, 4 }, { 'S', 4, 2 }, { 'S', 16, 2 }, { 'B', 0, 0 } };
    for (int d = 0; d < 6; d++) {
        write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);   /* the reset's ranges, as the runner leaves them */
        write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
        write32(GP1, 0x08000000u | DS[d].mode);
        for (int s = 0; s < 5; s++) for (int r = 0; r < 4; r++) one(DS[d].width, DS[d].mode, DS[d].vb, KS[s].k, KS[s].a, KS[s].b, r);
    }
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0mode: Done.\n");
    for (;;) VSync(0);
}
