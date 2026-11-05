/*
 * spu/spu-dmaread - WHEN a DMA4 read takes each halfword out of sound RAM: the capture ring's write head in the data (2026-11-05)
 *
 * WHY. PCSX-Redux's irq_voice_read_pointer_block_aligned captures voice 1's ring by DMA4 read right after an IRQ; the second
 * SCPH-5501 reads its last nonzero at index 34 where this emulator reads 35 - everything else in the line alike (STATUS "A lead
 * from the same run"). This emulator reads a block's halfwords from sound RAM when the DMA moves the block (v0.377.246's pace: 66
 * cycles to the first block, 69.6 a word). The pace says the SPU fills a FIFO a halfword every ~35 cycles; if it does, each
 * halfword leaves RAM before its block moves, and where it leaves is what the ring's head shows.
 *
 * WHAT IT DOES. Interrupts off, no disc, sound out muted. Voice 1 loops a block of 4000h. For each case: voice 1 keyed off and the
 * ring left to go silent (600 samples), then at a falling SPUSTAT bit-11 edge (the ring's write position wrapping to index 0)
 * voice 1 is keyed on - it sounds from ring index ~8 - and D = 40 x 768 + e cycles after the edge (e swept 0..752 by 16) a DMA4
 * read takes 64 halfwords from 0800h (indices 0-63) in blocks of B words (4, 8, 16). Mode E switches the transfer to DMA read at the
 * key-on (before any sound: a FIFO filled then holds zeros for the first block); mode L at D, polling SPUSTAT for it, then CHCR as
 * Redux's test does. A line: mode, B, e, the cycles from the edge to the CHCR write, the first and last nonzero index, the count.
 *
 * READ IT LIKE THIS. The write head sits ~D / 768 into the ring. Read at the moving block's time, the last nonzero index steps one
 * per 768 cycles of e with the CHCR time; read into a FIFO earlier, it steps earlier, by where in its block the head's halfword is.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-dmaread --frames=6000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define D4_MADR 0x1F8010C0u
#define D4_BCR  0x1F8010C4u
#define D4_CHCR 0x1F8010C8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuModeNoWait(int m) { cnt = (uint16_t)((cnt & ~0x30) | (m << 4)); write16(SPU(0x1AA), cnt); }
static void spuMode(int m) {
    spuModeNoWait(m);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void spuDmaWrite(uint32_t spuAddr, uint32_t* ram, int words) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(2);
    write32(D4_MADR, (uint32_t)ram & 0x1FFFFF);
    write32(D4_BCR, ((uint32_t)(words / 16) << 16) | 16);
    write32(D4_CHCR, 0x01000201u);
    for (int i = 0; i < 4000000 && (read32(D4_CHCR) & 0x01000000); i++) {}
    idle(4000);
    spuMode(0);
}
/* a falling (want 0) or rising (want 1) edge of SPUSTAT bit 11, each wait bounded */
static uint32_t edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
    return now();
}
static uint32_t dcBlock[16] __attribute__((aligned(16)));
static uint16_t cap[64] __attribute__((aligned(16)));

static void one(char mode, int bs, uint32_t e) {
    for (int i = 0; i < 64; i++) cap[i] = 0xEEEE;
    spuMode(0);
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);           /* voice 1 off */
    idle(768u * 600u);                                              /* the ring goes silent */
    write16(SPU(0x1A6), (uint16_t)(0x0800 / 8));                    /* the read's address, set before the edge */
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    const uint32_t t0 = edge(0);                                    /* the ring's write position wraps to 0 */
    write16(SPU(0x188), 0x0002);                                    /* voice 1 on */
    if (mode == 'E') spuModeNoWait(3);                              /* E: DMA read from the key-on */
    const uint32_t D = 40u * 768u + e;
    write32(D4_MADR, (uint32_t)cap & 0x1FFFFF);
    write32(D4_BCR, ((uint32_t)(32 / bs) << 16) | (uint32_t)bs);
    uint32_t tc;
    if (mode == 'E') {
        while (now() - t0 < D) {}
        tc = now() - t0;
        write32(D4_CHCR, 0x01000200u);
    } else {
        while (now() - t0 < D) {}
        spuMode(3);                                                 /* L: the switch, polled, then CHCR (Redux's order) */
        tc = now() - t0;
        write32(D4_CHCR, 0x01000200u);
    }
    for (int i = 0; i < 4000000 && (read32(D4_CHCR) & 0x01000000); i++) {}
    spuMode(0);
    int first = -1, last = -1, n = 0;
    for (int i = 0; i < 64; i++) if (cap[i] != 0) { if (first < 0) first = i; last = i; n++; }
    printf("%c %d %u %u %d %d %d\n", mode, bs, (unsigned)e, (unsigned)tc, first, last, n);
}

int main() {
    printf("\nspu/spu-dmaread\n");
    printf("Voice 1's capture ring DMA4-read near its write head: mode, block words, e, cycles from the edge to CHCR, first, last, count. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                                /* the stable read (psx-spx: bits 24-27 nonzero) */
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    for (int v = 0; v < 24; v++) { write16(SPU((uint32_t)v * 0x10), 0); write16(SPU((uint32_t)v * 0x10 + 2), 0); }
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);                                    /* bit 11 needs bit 2 or 3 (psx-spx) */
    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    spuDmaWrite(0x1000, dcBlock, 16);
    write16(SPU(0x10 + 0x4), 0x1000);                               /* voice 1: the 4000h block, 1x, fastest attack, held */
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x8), 0x000F);
    write16(SPU(0x10 + 0xA), 0x1FC0);
    const int BS[3] = { 16, 8, 4 };
    for (int k = 0; k < 3; k++)
        for (uint32_t e = 0; e < 768; e += 16) { one('E', BS[k], e); one('L', BS[k], e); }
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-dmaread: Done.\n");
    for (;;) VSync(0);
}
