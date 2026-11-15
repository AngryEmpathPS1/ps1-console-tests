/*
 * dma/dma-otc-three: three DMA6 starts in warm code, the second and third written while the first still holds the bus
 * (the stream refit session, 2026-11-15).
 *
 * WHY. The fuzzer's finding 5202-3 (three OTC starts, warm): the consoles write all three tables in order (146 cycles), this
 * emulator loses the second - its one pending-start flag (otcAgain) runs it with the third's MADR / BCR, written before the first
 * ended (78 cycles). dma-otc-latch measures two starts only. Here: start 1 (MADR A, BCR 30h), k1 nops, start 2 (MADR B, BCR 18h,
 * CHCR), k2 nops, start 3 (MADR C, BCR 0Ch, CHCR); then poll CHCR idle. All warm (the block run once on dummy words first).
 * READ IT. Rows "k1 a k2 b: A n B n C n  t T": words written downward from A, B and C's starts (30h / 18h / 0Ch when that
 * start ran with its own registers), T cycles from the first CHCR store to CHCR6 idle.
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

extern "C" { void ip_flush(void); }
__asm__(".text\n.set push\n.set noreorder\n.global ip_flush\nip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");

static uint32_t tbl[256] __attribute__((aligned(16)));
static volatile uint32_t dummy[4];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

/* a0 = DMA6 base, a1 = CHCR value, a2 = MADR B, a3 = MADR C; t0 = BCR 18h, t1 = BCR 0Ch (set by the block) */
static void gen(int k1, int k2) {
    int i = 0;
    CODE[i++] = 0x24080018u;               /* addiu t0, zero, 18h */
    CODE[i++] = 0x2409000Cu;               /* addiu t1, zero, 0Ch */
    CODE[i++] = 0xAC850008u;               /* sw a1, 8(a0)   start 1 */
    for (int q = 0; q < k1; q++) CODE[i++] = 0;
    CODE[i++] = 0xAC860000u;               /* sw a2, 0(a0)   MADR B */
    CODE[i++] = 0xAC880004u;               /* sw t0, 4(a0)   BCR 18h */
    CODE[i++] = 0xAC850008u;               /* sw a1, 8(a0)   start 2 */
    for (int q = 0; q < k2; q++) CODE[i++] = 0;
    CODE[i++] = 0xAC870000u;               /* sw a3, 0(a0)   MADR C */
    CODE[i++] = 0xAC890004u;               /* sw t1, 4(a0)   BCR 0Ch */
    CODE[i++] = 0xAC850008u;               /* sw a1, 8(a0)   start 3 */
    for (int q = 0; q < 4; q++) CODE[i++] = 0;
    CODE[i++] = 0x03E00008u; CODE[i++] = 0;
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
    const uint16_t t0 = read16(T2_COUNT);
    ((Blk)BASE)((volatile uint32_t*)DMA6, chcr, mb, mc);
    for (int i = 0; i < 200000 && (read32(DMA6 + 8) & 0x01000000u); i++) {}
    const uint16_t t1 = read16(T2_COUNT);
    if (read32(DMA6 + 8) & 0x01000000u) write32(DMA6 + 8, 0);
    waitT2(3000);
    /* A's table is overwritten from B's start down where they overlap: count each from its own start */
    printf("k1 %d k2 %d: A %2d B %2d C %2d  t %u\n", k1, k2, runDown(IA), runDown(IB), runDown(IC), (unsigned)(uint16_t)(t1 - t0));
}

int main() {
    printf("\ndma/dma-otc-three\n");
    printf("Three DMA6 starts in warm code: start 1 (MADR A, BCR 30h), k1 nops, start 2 (MADR B, BCR 18h), k2 nops, start 3\n");
    printf("(MADR C, BCR 0Ch). Words written downward from A, B and C; cycles to CHCR6 idle. No assertions.\n\n");
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
    printf("\ndma/dma-otc-three: Done.\n");
    for (;;) VSync(0);
}
