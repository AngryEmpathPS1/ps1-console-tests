/*
 * dma/dma-otc-three2: dma-otc-three's three DMA6 starts, with the bus holds stamped (the stream refit session, 2026-11-15).
 *
 * WHY. dma-otc-three (7502 and PSone identical): the console loses the second start in most rows, and its time to CHCR6 idle is
 * ~190-206 cycles nearly everywhere - ~50 more than this emulator's, whichever start survives, and nearly the same for 36, 60 or 84
 * words written. How many transfers run, and when does each hold end? Here the block goes on, after start 3 and 4 nops, with 8
 * pairs of reads - Timer 2, then CHCR6 - into registers (no store: a RAM store would queue behind the hold too). A read waits for
 * a DMA hold, so a gap between two stamps is a hold, and the CHCR6 read beside it says whether the channel was still busy.
 * READ IT. Rows "k1 a k2 b: A n B n C n  t T |" (as dma-otc-three; T from the block's first Timer 2 read, just before start 1),
 * then 8 pairs "s/b": s the Timer 2 stamp from that same read, b 1 when CHCR6's busy bit was set.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR 0x1F8010F0u
#define DMA6 0x1F8010E0u
#define SENT 0x5A000000u
#define IA 0x50
#define IB 0xA0
#define IC 0xF0
#define CODE ((volatile uint32_t *)0x80030000)
#define BASE 0x80030000u
#define NP 8

extern "C" { void ip_flush(void); }
__asm__(".text\n.set push\n.set noreorder\n.global ip_flush\nip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");

static uint32_t tbl[256] __attribute__((aligned(16)));
static volatile uint32_t dummy[32];
static volatile uint32_t out[2 * NP + 1];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

/* the registers the stamps are read into: t2-t7, t8, t9, s0-s7 */
static const int R[2 * NP] = { 10, 11, 12, 13, 14, 15, 24, 25, 16, 17, 18, 19, 20, 21, 22, 23 };

/* a0 = DMA6 base (Timer 2 at 40h from it), a1 = CHCR value, a2 = MADR B, a3 = MADR C; t0 = BCR 18h, t1 = BCR 0Ch */
static void gen(int k1, int k2) {
    int i = 0;
    const uint32_t o = (uint32_t)&out[0];
    CODE[i++] = 0x27BDFFE0u;                                        /* addiu sp, sp, -32 */
    for (int q = 0; q < 8; q++) CODE[i++] = 0xAFA00000u | ((16u + q) << 16) | (4u * q);   /* sw s0..s7, 4q(sp) */
    CODE[i++] = 0x24080018u;                                        /* addiu t0, zero, 18h */
    CODE[i++] = 0x2409000Cu;                                        /* addiu t1, zero, 0Ch */
    CODE[i++] = 0x8C820040u;                                        /* lw v0, 40h(a0)   Timer 2: the start */
    CODE[i++] = 0xAC850008u;                                        /* sw a1, 8(a0)   start 1 */
    for (int q = 0; q < k1; q++) CODE[i++] = 0;
    CODE[i++] = 0xAC860000u;                                        /* sw a2, 0(a0)   MADR B */
    CODE[i++] = 0xAC880004u;                                        /* sw t0, 4(a0)   BCR 18h */
    CODE[i++] = 0xAC850008u;                                        /* sw a1, 8(a0)   start 2 */
    for (int q = 0; q < k2; q++) CODE[i++] = 0;
    CODE[i++] = 0xAC870000u;                                        /* sw a3, 0(a0)   MADR C */
    CODE[i++] = 0xAC890004u;                                        /* sw t1, 4(a0)   BCR 0Ch */
    CODE[i++] = 0xAC850008u;                                        /* sw a1, 8(a0)   start 3 */
    for (int q = 0; q < 4; q++) CODE[i++] = 0;
    for (int p = 0; p < NP; p++) {
        CODE[i++] = 0x8C800040u | ((uint32_t)R[2 * p] << 16);       /* lw rS, 40h(a0)   Timer 2 */
        CODE[i++] = 0x8C800008u | ((uint32_t)R[2 * p + 1] << 16);   /* lw rB, 8(a0)     CHCR6 */
    }
    CODE[i++] = 0x3C010000u | (o >> 16);                            /* lui at, hi(out) */
    CODE[i++] = 0x34210000u | (o & 0xFFFF);                         /* ori at, at, lo(out) */
    for (int q = 0; q < 2 * NP; q++) CODE[i++] = 0xAC200000u | ((uint32_t)R[q] << 16) | (4u * q);   /* sw r, 4q(at) */
    CODE[i++] = 0xAC220000u | (4u * 2 * NP);                        /* sw v0, 64(at) */
    for (int q = 0; q < 8; q++) CODE[i++] = 0x8FA00000u | ((16u + q) << 16) | (4u * q);   /* lw s0..s7, 4q(sp) */
    CODE[i++] = 0x03E00008u;                                        /* jr ra */
    CODE[i++] = 0x27BD0020u;                                        /* addiu sp, sp, 32 */
    for (int q = 0; q < 8; q++) CODE[i++] = 0;
}
typedef void (*Blk)(volatile uint32_t*, uint32_t, uint32_t, uint32_t);

static int runDown(int from) {
    int c = 0;
    for (int i = from; i >= 0; i--) {
        const uint32_t v = tbl[i];
        if (v == 0x00FFFFFFu) { c++; break; }
        if (v != (((uint32_t)&tbl[i] - 4u) & 0x00FFFFFFu)) break;
        c++;
    }
    return c;
}
static void one(int k1, int k2) {
    const uint32_t chcr = 0x11000002u, ma = (uint32_t)&tbl[IA] & 0x00FFFFFCu, mb = (uint32_t)&tbl[IB] & 0x00FFFFFCu, mc = (uint32_t)&tbl[IC] & 0x00FFFFFCu;
    for (int i = 0; i < 256; i++) tbl[i] = SENT | (uint32_t)i;
    gen(k1, k2);
    ip_flush();
    ((Blk)BASE)(&dummy[0], chcr, mb, mc);  /* the block's lines in */
    write32(DMA6 + 0, ma);
    write32(DMA6 + 4, 0x30);
    waitT2(2000);
    ((Blk)BASE)((volatile uint32_t*)DMA6, chcr, mb, mc);
    for (int i = 0; i < 200000 && (read32(DMA6 + 8) & 0x01000000u); i++) {}
    const uint16_t t1 = read16(T2_COUNT);
    if (read32(DMA6 + 8) & 0x01000000u) write32(DMA6 + 8, 0);
    waitT2(3000);
    const uint16_t t0 = (uint16_t)out[2 * NP];
    printf("k1 %d k2 %d: A %2d B %2d C %2d  t %u |", k1, k2, runDown(IA), runDown(IB), runDown(IC), (unsigned)(uint16_t)(t1 - t0));
    for (int p = 0; p < NP; p++) printf(" %u/%u", (unsigned)(uint16_t)((uint16_t)out[2 * p] - t0), (unsigned)((out[2 * p + 1] >> 24) & 1));
    printf("\n");
}

int main() {
    printf("\ndma/dma-otc-three2\n");
    printf("dma-otc-three's three DMA6 starts, then 8 pairs of reads (Timer 2, CHCR6) in the block: a gap between stamps is a bus hold.\n");
    printf("Rows: tables written from A, B, C; t to CHCR6 idle; each pair's stamp / busy. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t dp = read32(DPCR);
    write32(DPCR, dp | 0x08000000u);
    static const int K[] = { 0, 2, 4, 8, 16, 30, 60 };
    for (int a = 0; a < 7; a++) for (int b = 0; b < 7; b++) one(K[a], K[b]);
    write32(DPCR, dp);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-otc-three2: Done.\n");
    for (;;) VSync(0);
}
