/*
 * spu/spu-dmaread2 - each halfword's fetch time in a DMA4 read of sound RAM, by where the ring's head falls in the read (2026-11-05)
 *
 * WHY. hwtests/spu-dmaread on the second SCPH-5501: switching the transfer to DMA read fills a 32-halfword FIFO at once (the ring
 * reads zero up to index 31 when the switch came before the voice sounded), and the head's index at ~40 steps where a refill fetch
 * meets the voice's write (v0.377.278). Two rules fit those rows and part on others: when the refill starts (as each word leaves,
 * or once a block has), the fill's pace (34.8 a halfword from the read's 69.6 a word, or less), and whether the DMA waits for the
 * FIFO in a read started as the mode appears (spu-dmaread's L rows at 16-word blocks, a sample apart). This test puts the head at
 * every fourth halfword of the read, so each halfword's fetch shows on its own.
 *
 * WHAT IT DOES. As spu-dmaread: interrupts off, voice 1 loops 4000h, keyed on at a falling bit-11 edge (the ring's write position at
 * 0), sounding from ring index ~8. D = 40 x 768 + e cycles on (e 0..736 by 32) a DMA4 read of 64 halfwords starts at ring index
 * 40 - k, so the write head (~index 40) sits at halfword k of the read (k 0, 4 .. 60). Blocks of 16 and 4 words; mode E switches the
 * transfer to DMA read at the key-on, mode L at D (polled, then CHCR). A line: mode, B, k, e, the cycles from the edge to CHCR, the
 * last nonzero halfword of the read (the head as fetched), the count of nonzero halfwords.
 *
 * READ IT LIKE THIS. For a halfword k past the FIFO's first 32 (E) or anywhere (L), the head as fetched steps from k - 1 to k where
 * that halfword's fetch passes its ring position's write: e at the step, against k, is the fetch time against the read's start.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-dmaread2 --frames=40000 --quiet=3000`.
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
static uint32_t edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
    return now();
}
static uint32_t dcBlock[16] __attribute__((aligned(16)));
static uint16_t cap[64] __attribute__((aligned(16)));

/* the read starts at ring index 40 - k (the transfer address counts 8 bytes: k a multiple of 4); past k 40 it starts in the
 * CD-right capture area below 0800h, zero without a disc */
static void one(char mode, int bs, int k, uint32_t e) {
    for (int i = 0; i < 64; i++) cap[i] = 0xEEEE;
    const int start = 40 - k;                                       /* ring index; the head at halfword k */
    spuMode(0);
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);           /* voice 1 off */
    idle(768u * 600u);                                              /* the ring goes silent */
    write16(SPU(0x1A6), (uint16_t)((0x0800 + start * 2) / 8));      /* the read's address, set before the edge */
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    const uint32_t t0 = edge(0);                                    /* the ring's write position wraps to 0 */
    write16(SPU(0x188), 0x0002);                                    /* voice 1 on */
    if (mode == 'E') spuModeNoWait(3);                              /* E: DMA read from the key-on */
    const uint32_t D = 40u * 768u + e;
    write32(D4_MADR, (uint32_t)cap & 0x1FFFFF);
    write32(D4_BCR, ((uint32_t)(32 / bs) << 16) | (uint32_t)bs);
    uint32_t tc;
    while (now() - t0 < D) {}
    if (mode == 'L') spuMode(3);                                    /* L: the switch, polled, then CHCR */
    tc = now() - t0;
    write32(D4_CHCR, 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(D4_CHCR) & 0x01000000); i++) {}
    spuMode(0);
    int last = -1, n = 0;
    for (int i = 0; i < 64; i++) if (cap[i] != 0) { last = i; n++; }
    printf("%c %d %d %u %u %d %d\n", mode, bs, k, (unsigned)e, (unsigned)tc, last, n);
}

int main() {
    printf("\nspu/spu-dmaread2\n");
    printf("The ring's head at halfword k of a 64-halfword DMA4 read: mode, block words, k, e, cycles from the edge to CHCR, last nonzero, count. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                                /* the stable read */
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    for (int v = 0; v < 24; v++) { write16(SPU((uint32_t)v * 0x10), 0); write16(SPU((uint32_t)v * 0x10 + 2), 0); }
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    spuDmaWrite(0x1000, dcBlock, 16);
    write16(SPU(0x10 + 0x4), 0x1000);
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x8), 0x000F);
    write16(SPU(0x10 + 0xA), 0x1FC0);
    const int BS[2] = { 16, 4 };
    for (int q = 0; q < 2; q++)
        for (int k = 0; k < 64; k += 4)
            for (uint32_t e = 0; e < 768; e += 32) { one('E', BS[q], k, e); one('L', BS[q], k, e); }
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-dmaread2: Done.\n");
    for (;;) VSync(0);
}
