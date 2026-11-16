/*
 * dma/dma-otc-skip3: is the skip the registers' freshness, or the I/O read before the start? (the stream refit session, 2026-11-16.)
 * dma-otc-skip2 on the 7502 and the PSone (alike): the read skips the hold at every BCR -> CHCR distance tried, 0..16 nops, and
 * with MADR 40 nops older - past land_otcskip's DMA_OTC_FRESH (12 cycles). dma-otc-ioread, where nothing skips, stored MADR / BCR
 * 2,000 cycles before AND put a Timer 2 read right before the CHCR store. Here: MADR, BCR, a loop of n (0, 4 ... 1024; about
 * three cycles a turn), v 1: lhu Timer 2 right before the start; CHCR (200 words), k nops (0..5), lhu Timer 2. Rows "v V n N k K:
 * t T idle I" (T includes the loop: read the step from k to k + 1, ~220 when the read waits). What follows is dma-otc-skip's note.
 *
 * dma/dma-otc-skip: when does a read behind a DMA6 start skip the transfer's hold? (the stream refit session, 2026-11-16).
 *
 * WHY. dma-otc-ioread2: with MADR, BCR and CHCR stored back to back a Timer 2 read 1-2 instructions behind the start returns in 12
 * cycles (no wait), from 3 behind it waits; with MADR / BCR stored long before nothing skips; the fuzzer's 5901-0 / 5902-7 case
 * (an instruction between each store, the read 3 behind) skips on its warm run. Here the three are crossed, warm: MADR, g1 nops
 * (0..1), BCR, g2 nops (0..4), CHCR (the start, 200 words), k nops (0..4), lhu Timer 2.
 * READ IT. Rows "g1 a g2 b k c: t T  idle I": T Timer 2 from before the MADR store to just after the read (small = skipped), I the
 * cycles to CHCR6 idle.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR 0x1F8010F0u
#define DMA6 0x1F8010E0u
#define CODE ((volatile uint32_t *)0x80030000)
#define BASE 0x80030000u

extern "C" { void ip_flush(void); }
__asm__(".text\n.set push\n.set noreorder\n.global ip_flush\nip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");

static uint32_t tbl[256] __attribute__((aligned(16)));
static volatile uint32_t dummy[64];
static volatile uint32_t out[4];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

/* a0 = DMA6 base (or dummy), a1 = CHCR value, a3 = out; t3 = MADR, t4 = BCR made in the block */
static void gen(uint32_t ma, int n, int v, int k) {
    int i = 0;
    CODE[i++] = 0x3C0B0000u | (ma >> 16); CODE[i++] = 0x356B0000u | (ma & 0xFFFF); CODE[i++] = 0x240C00C8u;   /* t3 = MADR, t4 = 200 */
    CODE[i++] = 0x94890040u;               /* lhu t1, 40h(a0)   Timer 2: the start */
    CODE[i++] = 0xAC8B0000u;               /* sw t3, 0(a0)      MADR */
    CODE[i++] = 0xAC8C0004u;               /* sw t4, 4(a0)      BCR */
    if (n) {
        CODE[i++] = 0x240D0000u | n;       /* addiu t5, zero, n */
        CODE[i++] = 0x25ADFFFFu;           /* L: addiu t5, t5, -1 */
        CODE[i++] = 0x15A0FFFEu;           /* bnez t5, L */
        CODE[i++] = 0;
    }
    if (v) CODE[i++] = 0x948E0040u;        /* lhu t6, 40h(a0)   an I/O read right before the start */
    CODE[i++] = 0xAC850008u;               /* sw a1, 8(a0)      CHCR: the start */
    for (int q = 0; q < k; q++) CODE[i++] = 0;
    CODE[i++] = 0x948A0040u;               /* lhu t2, 40h(a0)   Timer 2 after it */
    CODE[i++] = 0xACE90000u;               /* sw t1, 0(a3) */
    CODE[i++] = 0xACEA0008u;               /* sw t2, 8(a3) */
    CODE[i++] = 0x03E00008u; CODE[i++] = 0;
    for (int q = 0; q < 8; q++) CODE[i++] = 0;
}
typedef void (*Blk)(volatile uint32_t*, uint32_t, volatile uint32_t*, volatile uint32_t*);

static void one(int n, int v, int k) {
    const uint32_t chcr = 0x11000002u, ma = (uint32_t)&tbl[200] & 0x00FFFFFCu;
    gen(ma, n, v, k);
    ip_flush();
    ((Blk)BASE)(&dummy[0], chcr, &dummy[8], out);   /* the block's lines in */
    waitT2(2000);
    ((Blk)BASE)((volatile uint32_t*)DMA6, chcr, &dummy[8], out);
    const uint16_t t0 = (uint16_t)out[0];
    for (int i = 0; i < 200000 && (read32(DMA6 + 8) & 0x01000000u); i++) {}
    const uint16_t t1 = read16(T2_COUNT);
    if (read32(DMA6 + 8) & 0x01000000u) write32(DMA6 + 8, 0);
    waitT2(3000);
    printf("v %d n %d k %d: t %u  idle %u\n", v, n, k, (unsigned)(uint16_t)((uint16_t)out[2] - t0), (unsigned)(uint16_t)(t1 - t0));
}

int main() {
    printf("\ndma/dma-otc-skip3\n");
    printf("MADR, BCR, a loop of n, (v 1: lhu Timer 2), CHCR (a 200-word DMA6 start), k nops, lhu Timer 2; warm. t: cycles from before MADR to after\n");
    printf("the read (small: it skipped the hold); idle: cycles to CHCR6 idle. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t dp = read32(DPCR);
    write32(DPCR, dp | 0x08000000u);
    static const int N[] = { 0, 4, 16, 64, 256, 1024 }; for (int v = 0; v <= 1; v++) for (int j = 0; j < 6; j++) for (int k = 0; k <= 5; k++) one(N[j], v, k);
    write32(DPCR, dp);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-otc-skip3: Done.\n");
    for (;;) VSync(0);
}
