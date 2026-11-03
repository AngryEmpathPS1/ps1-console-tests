/*
 * gpu/gpu-a0rate2 - what the scan-out's fetch costs a GP0(A0h) transfer at each DMA pace, against the scanline (2026-11-03)
 *
 * hwtests/gpu-a0rate with the poll DITHERED. That test's poll (40 cached nops between CHCR reads, a ~45-cycle step) starts at the CHCR
 * write, so the step sits at the same place against the transfer's end wherever the transfer starts, and an end a few cycles later
 * reads a whole step longer (the SCPH-1001's 16-word blocks: 235 on the console, 271 on our emulator; 8-word blocks 316 against 315).
 * Here a counted wait of D = 0..11 short loops (about 4 cycles each) stands between the CHCR write and the first poll, a different D
 * for each place and run, so over a bin of places the step averages out. Everything else is gpu-a0rate's.
 *
 * WHY. Our emulator's GPU intake (v0.377.256) stops writing its 16-word buffer while the scan-out fetches, and that was fitted to
 * hwtests/gpu-a0row's bursts, which lose words. hwtests/dma-rows (the first SCPH-5501, 2026-11-02 after its RESET) says the slow
 * paces pay nothing for the scan-out: 2,048 words by blocks of 2 read 12,513-12,534 cycles at every width, where the emulator pays
 * 36-163 more, and gpu/bandwidth's uploads (blocks of 16) run 1.9% long here. Whether the fetch stops the buffer's write or only slows
 * it, and how much, decides both. This test puts one short transfer at 64 places along a scanline, at five paces.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks, GP1(00h) first (so the scan-out is
 * the reset's: 256 x 240, which still fetches with the display off - psx-spx GP1(03h)). 128 words by DMA2 (sync 1) into GP0(A0h) at
 * (0,256), by blocks of 1, 2, 4, 8 and 16 words, into one row 256 pixels wide and into rows 32 wide (16 words each). Each transfer
 * waits for vblank (I_STAT bit 0, polled), for 40 hblanks after it, for the next hblank edge, then `phase` cycles more (0, 34, 68
 * ... 2,142: about one line) before CHCR2 is written, and is timed until CHCR's busy bit falls, polled with 40 cached nops between
 * reads so the bus is left to the DMA (as hwtests/dma-rows). Twice each. Blocks of 16 or fewer never lose a word (dma-loss).
 *
 * READ IT LIKE THIS. R bs w phase t0 t1: bs the block size, w the rectangle's width, phase the cycles after the hblank edge, t0 t1
 * the transfer's cycles, two runs (run 0 waits D = (phase step x 5) mod 12 loops before its first poll, run 1 D + 6 mod 12). Where the fetch falls inside a transfer that pays for it, t rises for a run of phases; how far
 * it rises at each block size is the price. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0rate2 --frames=3000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0      0x1F801810u
#define GP1      0x1F801814u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define WORDS    128
#define POLLS    60000

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static int timeouts;
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuReady() { for (int i = 0; i < 30000; i++) if ((read32(GP1) & 0x14000000u) == 0x14000000u) return; timeouts++; printf("TIMEOUT gpu %x\n", read32(GP1)); write32(GP1, 0x01000000u); write32(GP1, 0x02000000u); }

/* To `phase` cycles after the edge of the 40th hblank past vblank's start; bounded (rule 1: every wait has a timeout). */
static void toLine(uint32_t phase) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    for (int i = 0; i < 2000000 && (uint16_t)(read16(T1_COUNT) - l) < 40; i++) {}
    const uint16_t e = read16(T1_COUNT);
    for (int i = 0; i < 200000 && read16(T1_COUNT) == e; i++) {}
    if (phase) waitT2(phase);
}

__attribute__((noinline)) static uint32_t one(uint32_t bs, uint32_t w, uint32_t phase, uint32_t dither) {
    const uint32_t h = 2 * WORDS / w;
    gpuReady();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (h << 16) | w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, ((WORDS / bs) << 16) | bs);
    toLine(phase);
    uint32_t p = 0;
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    for (uint32_t d = 0; d < dither; d++) __asm__ volatile("nop");   // registers only: nothing on the bus
    while ((read32(CHCR2) & 0x01000000u) && p < POLLS) { p++; __asm__ volatile(".rept 40\nnop\n.endr\n"); }
    const uint16_t t1 = read16(T2_COUNT);
    if (p >= POLLS) { write32(CHCR2, 0); timeouts++; }
    waitT2(3000);
    write32(GP1, 0x04000000u);
    return (uint16_t)(t1 - t0);
}

int main() {
    static const uint32_t sizes[5] = { 1, 2, 4, 8, 16 }, widths[2] = { 256, 32 };
    printf("\ngpu/gpu-a0rate2\n");
    printf("R bs w phase t0 t1 (128 words by DMA2 blocks of bs into a w-wide GP0(A0h) rectangle, `phase` cycles past an hblank edge "
           "40 lines below vblank; the poll dithered). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = 0x9E3779B9u * (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(GP1, 0);
    write32(DPCR, dp0 | (1u << 11));
    for (int b = 0; b < 5; b++)
        for (int c = 0; c < 2; c++)
            for (uint32_t ph = 0; ph < 64; ph++) {
                const uint32_t t0 = one(sizes[b], widths[c], ph * 34u, (ph * 5u) % 12u), t1 = one(sizes[b], widths[c], ph * 34u, (ph * 5u + 6u) % 12u);
                printf("R %u %u %u %u %u\n", sizes[b], widths[c], ph * 34u, t0, t1);
            }
    write32(DPCR, dp0);
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\n%d timeouts\ngpu/gpu-a0rate2: Done.\n", timeouts);
    for (;;) VSync(0);
}
