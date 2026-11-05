/*
 * gpu/gpu-a0stall - the GPU's intake at its line-start stall, word by word: how long GPUSTAT bit 28 ("Write FIFO empty", the mode-2 DMA
 * request) stays low after each word of a CPU-fed GP0(A0h) transfer, across five line starts (2026-11-05 evening, for the SCPH-1001).
 *
 * WHY. On the SCPH-1001 (the old GPU) a SyncMode 1 upload's bus goes idle ~40-60 cycles about once a scanline (hwtests/gpu-dma-stoiter2's
 * W 0 rows: the block count still while two reads go through) and its blocks over 16 words lose words (gpu-a0pace's K rows). Two
 * readings: the GPU's VRAM write is held through its line-start stall while its intake goes on taking words into its buffers (the
 * request drops only once they are full), or its intake takes NO word through the stall (a word due in it waits in the FIFO, so bit 28 -
 * the request - drops at once). Single words a few cycles apart tell them apart: under the first each is taken at once, under the second
 * the words in the stall wait for its end - and their waits give the stall's place after the hblank edge and its length.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks; the display 320x240 (GP1(08h) 01h, NTSC
 * 15-bit, as gpu-a0lsf). GP1(04h) = 2. A GP0(A0h) transfer of one 1,024-pixel row (512 words, no row change) at (0,256), its words sent by
 * the CPU from o cycles past the 40th hblank edge below vblank (o = 0, 5 ... 45) in steps: a Timer 2 read, the step's words to GP0, GPUSTAT
 * read until bit 28 is 1 (at most 4,000 reads); the next step's Timer 2 read ends it. Kinds: A one word a step (512 steps), B four back to
 * back (128 steps). Twice each.
 *
 * READ IT LIKE THIS. S kind o rep steps total base | step@t:time ... : base is the most common step time (Timer 2 read to the next step's:
 * the words' stores, the polls of bit 28 and the loop inside), then every step over base + 12 (a DRAM refresh is ~10) as its index, its
 * start t (cycles after the hblank edge; an NTSC line is ~2,152.6) and its time (at most 120 a pass). No assertions - compare against
 * your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-a0stall`.
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
#define WORDS 512

static uint16_t ta[WORDS + 1];
static uint16_t hist[256];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void toLine(uint32_t phase) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t l = read16(T1_COUNT);
    while ((uint16_t)(read16(T1_COUNT) - l) < 40) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    if (phase) waitT2(phase);
}

static void one(char kind, uint32_t o, int rep) {
    const int per = kind == 'A' ? 1 : 4, steps = WORDS / per;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (1u << 16) | 1024u);
    toLine(o);
    int wd = 0;
    for (int s = 0; s < steps; s++) {
        ta[s] = read16(T2_COUNT);
        if (per == 1) write32(GP0, (uint32_t)wd++);
        else { write32(GP0, (uint32_t)wd); write32(GP0, (uint32_t)wd + 1); write32(GP0, (uint32_t)wd + 2); write32(GP0, (uint32_t)wd + 3); wd += 4; }
        for (int n = 0; n < 4000 && !(read32(GP1) & (1u << 28)); n++) {}
    }
    ta[steps] = read16(T2_COUNT);
    gpuIdle();
    write32(GP1, 0x04000000u);
    for (int i = 0; i < 256; i++) hist[i] = 0;
    uint32_t total = 0;
    for (int s = 0; s < steps; s++) { const uint16_t d = (uint16_t)(ta[s + 1] - ta[s]); hist[d < 255 ? d : 255]++; total += d; }
    int base = 0;
    for (int i = 1; i < 255; i++) if (hist[i] > hist[base]) base = i;
    printf("S %c %u %d %d %u %d |", kind, (unsigned)o, rep, steps, (unsigned)total, base);
    uint32_t t = o;
    int shown = 0;
    for (int s = 0; s < steps && shown < 120; s++) {
        const uint16_t d = (uint16_t)(ta[s + 1] - ta[s]);
        if (d > base + 12) { printf(" %d@%u:%u", s, (unsigned)t, (unsigned)d); shown++; }
        t += d;
    }
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-a0stall\n");
    printf("S kind o rep steps total base | step@t:time (one 512-word row by the CPU from o cycles past the 40th hblank edge; A a word "
           "a step, B four; a step = the words' stores and GPUSTAT read until bit 28; listed: steps over base + 12). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u);
    write32(GP1, 0x07000000u | (0x100u << 10) | 0x010u);
    write32(GP1, 0x08000001u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int k = 0; k < 2; k++) for (uint32_t o = 0; o < 50; o += 5) for (int r = 0; r < 2; r++) one(k ? 'B' : 'A', o, r);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0stall: Done.\n");
    for (;;) VSync(0);
}
