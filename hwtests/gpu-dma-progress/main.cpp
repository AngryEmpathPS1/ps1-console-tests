/*
 * gpu/gpu-dma-progress - how far a GPU DMA upload has got, sampled with the CPU off the bus (2026-11-03 night, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-quiet on the SCPH-5501: an upload of 4-word blocks takes 7,280 cycles - 512 blocks x 14 plus its DRAM
 * refreshes, no wait at all - where this core parks the DMA on the GPU's intake once a scanline (the intake waits out a buffer write
 * the line's scan-out fetch holds, the FIFO is not empty, and in DMA mode 2 the request is "FIFO empty": psx-spx GPUSTAT.25/28), 110
 * cycles a transfer. From 16-word blocks up the totals agree within ~30, but no intake setting fits both, so the totals do not say
 * WHERE the console waits. This samples it: the upload's progress against time.
 *
 * WHAT IT DOES. chopping's upload (64 x 64, 2,048 words, GP0(A0h), GP1(04h) = 2, SyncMode 1) at blocks of 4 and of 32, started a
 * fixed offset after an hblank edge (Timer 1 counting hblanks), interrupts off. Each probe: CHCR2's store, a cached spin (no bus
 * access), then one Timer 2 read and one BCR2 read (its block count counts down as blocks finish), then the rest of the upload
 * waited out. The spin grows 16 cycles a probe across the whole upload. Two start offsets per block size, a line apart in phase.
 * Printed per probe: the cycles from the store to the Timer 2 read, and the blocks done.
 *
 * READ IT LIKE THIS. The blocks done against time is the DMA's pace: 4-word blocks every 14 cycles is a straight line; a wait shows
 * as a flat step, once a line where the scan-out holds the GPU. Each machine's curve against the other's. No assertions. It prints
 * as it goes (a line a probe group): `node hwtests/run.js gpu-dma-progress --ntsc --frames=6000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static inline void spin(uint32_t n) {
    __asm__ volatile(".set push\n.set noreorder\n1: addiu %0, %0, -1\nbnez %0, 1b\nnop\n.set pop\n" : "+r"(n));
}

static uint32_t buf[2048] __attribute__((aligned(16)));
static uint32_t bsNow;

static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);
    settle();
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 512u); write32(GP0, (64u << 16) | 64u);
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, ((2048u / bsNow) << 16) | bsNow);
}
static void atLine(uint32_t off) {
    const uint16_t h = read16(T1_COUNT);
    for (unsigned k = 0; k < 100000 && read16(T1_COUNT) == h; k++) {}
    waitCycles(off);
}
static __attribute__((noinline, noclone)) uint16_t probe(uint32_t n, uint32_t* left) {
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    spin(n);
    const uint16_t a1 = read16(T2_COUNT);
    *left = read32(BCR2) >> 16;
    return (uint16_t)(a1 - a0);
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-progress\n");
    printf("A 2,048-word GPU DMA upload's blocks done against cycles since CHCR2's store, sampled with the CPU off the bus. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 2048; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 4; setup(); { uint32_t l; probe(10, &l); } settle();   /* cache it */
    static const uint32_t SIZES[2] = { 4, 32 }, OFFS[2] = { 40, 1116 }, SPAN[2] = { 7600, 3100 };
    for (int s = 0; s < 2; s++) {
        bsNow = SIZES[s];
        const uint32_t total = 2048 / bsNow;
        for (int o = 0; o < 2; o++) {
            printf("blocks of %u, start %u after an hblank: cycles:blocks done\n", (unsigned)bsNow, (unsigned)OFFS[o]);
            unsigned col = 0;
            for (uint32_t want = 16; want < SPAN[s]; want += 16) {
                uint32_t left = 0;
                setup(); atLine(OFFS[o]);
                const uint16_t e = probe(want / 3 + 1, &left);
                settle();
                printf("%u:%u%s", (unsigned)e, (unsigned)(total - (left > total ? total : left)), (++col % 12) ? " " : "\n");
            }
            printf("\n");
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-progress: Done.\n");
    for (;;) {}
}
