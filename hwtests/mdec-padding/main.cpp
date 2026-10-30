/*
 * mdec/mdec-padding - does the MDEC take FE00h padding after a command's last
 * macroblock?
 *
 * pcsx-redux nugget b7d9561, measured on an SCPH-1001: bsdec pads its run-level
 * data with FE00h to a 32-word DMA block, and the MDEC command's word count
 * covers the padding. After the MDEC emitted all 300 macroblocks, DMA1 had
 * landed 76,800 of 76,800 halfwords and DMA0 stayed busy for ever (CHCR
 * 01000201h: in-FIFO full, Data-In Request deasserted). psx-spx says FE00h at
 * the start of a block "is simply ignored by the hardware", which would drain
 * the padding - and does, on the emulator this was written for (STATUS M236).
 * This asks the console where DMA0 stops.
 *
 * Each case: N colour macroblocks of the smallest run-level data there is (a DC
 * word and an EOB for each block), then P words of FE00FE00h, all under one
 * decode command (15-bit, bit 15 set) whose word count covers them. DMA1 reads
 * exactly N macroblocks (N x 128 words), DMA0 sends N x 6 + P words in 32-word
 * blocks. When DMA1 has finished, and again about 200,000 loop turns later, it
 * prints DMA0's CHCR, how many words DMA0 has moved and MDEC's status.
 * There are no assertions - compare against your emulator.
 *
 * READ IT LIKE THIS. The first case has no padding and must complete
 * everywhere (a control: if it does not, the instrument is broken). In the
 * others, DMA0 moving every word with CHCR bit 24 clear means the padding was
 * consumed (psx-spx; the emulator). DMA0 stopped with words left and bit 24
 * set means the MDEC stopped taking input after the last macroblock, and HOW
 * MANY padding words it took names the mechanism - 6 would be one more
 * macroblock decoded from FE00h pairs, which nobody reads.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js mdec-padding`): every
 * case completes, control and padding alike - DMA0 CHCR 00000201h, all 1,824 /
 * 1,824 / 1,856 / 1,888 words moved, MDEC status 8684FFFFh. The SCPH-1001 of
 * nugget b7d9561 would leave the 24-word case busy.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define MAXW 2048
#define MAXMB 304

static uint32_t in[MAXW] __attribute__((aligned(16)));
static uint32_t out[MAXMB * 128] __attribute__((aligned(16)));

#define MDEC_CMD  0x1F801820
#define MDEC_CTRL 0x1F801824
#define D0_MADR 0x1F801080
#define D0_BCR  0x1F801084
#define D0_CHCR 0x1F801088
#define D1_MADR 0x1F801090
#define D1_BCR  0x1F801094
#define D1_CHCR 0x1F801098
#define DPCR    0x1F8010F0

static void runCase(int n, int p) {
    const int words = n * 6 + p;
    uint16_t* hw = (uint16_t*)in;
    for (int b = 0; b < n * 6; b++) { hw[b * 2] = 0x0400; hw[b * 2 + 1] = 0xFE00; }
    for (int i = n * 6; i < words; i++) in[i] = 0xFE00FE00;

    write32(MDEC_CTRL, 0x80000000);                                     /* reset */
    write32(MDEC_CTRL, 0x60000000);                                     /* DMA in and out */
    write32(MDEC_CMD, 0x40000001); for (int i = 0; i < 32; i++) write32(MDEC_CMD, 0x01010101);   /* quant, colour */
    write32(MDEC_CMD, 0x60000000); for (int i = 0; i < 32; i++) write32(MDEC_CMD, 0);            /* IDCT table */
    write32(MDEC_CMD, 0x20000000 | 0x18000000 | 0x02000000 | words);   /* decode, 15-bit, bit 15 set */

    write32(DPCR, read32(DPCR) | 0x88);                                 /* channels 0 and 1 on */
    write32(D1_MADR, (uint32_t)out); write32(D1_BCR, ((n * 128 / 32) << 16) | 32); write32(D1_CHCR, 0x01000200);
    write32(D0_MADR, (uint32_t)in);  write32(D0_BCR, ((words / 32) << 16) | 32);  write32(D0_CHCR, 0x01000201);

    uint32_t t = 0;
    while ((read32(D1_CHCR) & 0x01000000) && t < 5000000) t++;
    const uint32_t c1 = read32(D1_CHCR);
    const uint32_t c0a = read32(D0_CHCR), m0a = read32(D0_MADR), s0a = read32(MDEC_CTRL);
    for (volatile int i = 0; i < 200000; i++) {}
    const uint32_t c0b = read32(D0_CHCR), m0b = read32(D0_MADR), s0b = read32(MDEC_CTRL);
    const int movedA = (int)(((m0a & 0x1FFFFF) - ((uint32_t)in & 0x1FFFFF)) / 4);
    const int movedB = (int)(((m0b & 0x1FFFFF) - ((uint32_t)in & 0x1FFFFF)) / 4);
    printf("N=%3d P=%2d (%4d words)  DMA1 %s  DMA0 CHCR %08x -> %08x  moved %4d -> %4d  MDEC stat %08x -> %08x\n",
           n, p, words, (c1 & 0x01000000) ? "STILL BUSY" : "done", c0a, c0b, movedA, movedB, s0a, s0b);

    write32(D0_CHCR, 0); write32(D1_CHCR, 0);                           /* stop both, whatever state they are in */
    write32(MDEC_CTRL, 0x80000000);
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/mdec-padding\n");
    printf("Does the MDEC take FE00h padding past a command's last macroblock? (nugget b7d9561)\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    runCase(304, 0);    /* the control: 1,824 words, no padding - must complete */
    runCase(300, 24);   /* bsdec's shape: 1,800 + 24 to a 32-word block */
    runCase(300, 56);
    runCase(300, 88);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) VSync(0);
}
