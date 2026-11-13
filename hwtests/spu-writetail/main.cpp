/*
 * spu/spu-writetail - does the end of a DMA4 write always reach sound RAM? (2026-11-13)
 *
 * WHY. The whole-console fuzzer's host (hwtests/fuzz-all) refills sound RAM 1000h-1FFFh before every case by DMA4 (1,024 words
 * in 16-word blocks), waits for the channel, for SPUSTAT bit 10, 4,000 cycles more, then sets the transfer mode to 0. With its
 * readback made stable (1F801014h = 220931E1h) three cases of 63 still differ from this emulator, the same three on the second
 * SCPH-5501, the PSone and the SCPH-7502, none with an SPU op of its own - and in the one traced the console's LAST 20 HALFWORDS
 * (1FD8h-1FFFh) still hold the case before's data: the end of the write never landed (STATUS "The fuzzer's sound-RAM readback").
 *
 * WHAT IT DOES. Interrupts off, sound muted, stable reads. 96 rounds of the host's own refill and readback. Round r writes
 * 1000h-1FFFh with halfwords that name the round (pattern rounds: (r << 11) | index; random rounds: a xorshift stream, as the
 * host's), waiting W cycles after SPUSTAT bit 10 clears before mode 0 (W = 4000 as the host; some rounds 200, 1000, 20000),
 * then reads 1000h-1FFFh back and counts the halfwords that are not this round's, printing where the first and last are and
 * whether they hold the round before's value. A line:
 *   W r kind wait dmaCycles busyCycles stale first last prev
 *     kind P / R (pattern / random); dmaCycles: CHCR write to the channel's end; busyCycles: from there to bit 10 clear;
 *     stale: halfwords not this round's; first / last: their indices (0-2047, -1 none); prev: how many of them hold the
 *     round before's halfword.
 * Every wait is bounded; a channel still live at its wait's end is stopped and printed ("T chcr"). No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-writetail`.
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
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4)); write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static uint32_t dmaT, busyT;
static void spuDma(uint32_t spuAddr, uint32_t* ram, uint32_t words, bool toSpu, uint32_t wait) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(D4_MADR, (uint32_t)ram & 0xFFFFFF);
    write32(D4_BCR, ((words / 16) << 16) | 16);
    const uint32_t t0 = now();
    write32(D4_CHCR, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 200000 && (read32(D4_CHCR) & 0x01000000); i++) { now(); }
    const uint32_t t1 = now();
    if (read32(D4_CHCR) & 0x01000000) { printf("T %x\n", (unsigned)read32(D4_CHCR)); write32(D4_CHCR, 0); }
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x0400); i++) { now(); }
    const uint32_t t2 = now();
    if (toSpu) { dmaT = t1 - t0; busyT = t2 - t1; idle(wait); }
    spuMode(0);
}
static uint32_t buf[1024] __attribute__((aligned(16)));
static uint16_t want[2048], before[2048];
static uint32_t rng = 0x2545F491;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main() {
    printf("\nspu/spu-writetail\n");
    printf("96 refills of sound RAM 1000h-1FFFh by DMA4 as the fuzzer's host makes them, each read back: W round kind wait dmaCycles busyCycles stale first last prev. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x1AC), 0x0004);
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 2048; i++) before[i] = 0;
    static const uint32_t WAITS[8] = { 4000, 4000, 4000, 200, 4000, 1000, 4000, 20000 };
    for (uint32_t r = 1; r <= 96; r++) {
        const bool pat = (r & 1) != 0;
        uint16_t* h = (uint16_t*)buf;
        for (uint32_t i = 0; i < 2048; i++) { const uint16_t v = pat ? (uint16_t)(((r & 31) << 11) | i) : (uint16_t)rnd(); h[i] = v; want[i] = v; }
        const uint32_t wait = WAITS[r & 7];
        write32(0x1F801014, spuDelay);                              /* the write at the console's own delay, as the host */
        spuDma(0x1000, buf, 1024, true, wait);
        idle(20000u + (rnd() & 0x3FFF));                            /* the host: 200,000 and a case; here a changing gap */
        for (uint32_t i = 0; i < 1024; i++) buf[i] = 0;
        write32(0x1F801014, 0x220931E1);
        spuDma(0x1000, buf, 1024, false, 0);
        int stale = 0, first = -1, last = -1, prev = 0;
        for (int i = 0; i < 2048; i++) if (h[i] != want[i]) { stale++; if (first < 0) first = i; last = i; if (h[i] == before[i]) prev++; }
        printf("W %u %c %u %u %u %d %d %d %d\n", (unsigned)r, pat ? 'P' : 'R', (unsigned)wait, (unsigned)dmaT, (unsigned)busyT, stale, first, last, prev);
        for (int i = 0; i < 2048; i++) before[i] = h[i];
    }
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-writetail: Done.\n");
    for (;;) VSync(0);
}
