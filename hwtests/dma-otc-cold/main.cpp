/*
 * dma/dma-otc-cold: does a DMA6 start take a MADR stored k nops behind its CHCR, by where the CHCR store sits in a cold line?
 * (the stream refit session, 2026-11-15).
 *
 * WHY. DMA_OTC_LATCH (hwtests/dma-otc-latch, four consoles): a store to MADR or BCR up to 4 instructions behind a start's CHCR is
 * used. But that test's fire() code is compiler-placed, and the fuzzer's finding 5202-1 (two OTC starts four instructions apart,
 * cold) reads the first table at its own MADR on five consoles where this emulator takes the second's - it holds the cold CHCR store
 * behind the next line's fill and counts the latch from its landing. dma-otc-latch's own K 5 row reads the second's (B) on the
 * consoles, K 4 and 6 the first's (A). So: the CHCR store at word W (0..3) of a line-aligned block, k nops (0..8), the store of
 * MADR = B; run COLD (the I-cache flushed just before) and WARM (the block run once on dummy words first).
 * HALF (rows "half"): only the lines from the MADR store's line on warmed (the block entered there on dummy words), the CHCR's
 * line cold - dma-otc-latch's K 5 layout, which reads B on the consoles where every cold row here reads A.
 * READ IT. Rows "W w k n cold|warm: from A a from B b": which MADR the first transfer used (A: its own, B: the late store's).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR 0x1F8010F0u
#define DMA6 0x1F8010E0u
#define SENT 0x5A000000u
#define IA 0xB5
#define IB 0xF8
#define NA 0x36
#define CODE ((volatile uint32_t *)0x80030000)
#define BASE 0x80030000u

extern "C" {
void ip_flush(void);
}
__asm__(".text\n.set push\n.set noreorder\n.global ip_flush\nip_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");

static uint32_t tbl[256] __attribute__((aligned(16)));
static volatile uint32_t dummy[4];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

/* the block: W nops, sw a1,8(a0) (CHCR), k nops, sw a2,0(a0) (MADR = B), 4 nops, jr ra, nop - from a line boundary */
static void gen(int w, int k) {
    int i = 0;
    for (int q = 0; q < w; q++) CODE[i++] = 0;
    CODE[i++] = 0xAC850008u;               /* sw a1, 8(a0) */
    for (int q = 0; q < k; q++) CODE[i++] = 0;
    CODE[i++] = 0xAC860000u;               /* sw a2, 0(a0) */
    for (int q = 0; q < 4; q++) CODE[i++] = 0;
    CODE[i++] = 0x03E00008u; CODE[i++] = 0;
    for (int q = 0; q < 8; q++) CODE[i++] = 0;
}
typedef void (*Blk)(volatile uint32_t*, uint32_t, uint32_t);

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
static void one(int w, int k, int warm) {
    const uint32_t chcr = 0x11000002u, ma = (uint32_t)&tbl[IA] & 0x00FFFFFCu, mb = (uint32_t)&tbl[IB] & 0x00FFFFFCu;
    for (int i = 0; i < 256; i++) tbl[i] = SENT | (uint32_t)i;
    gen(w, k);
    ip_flush();
    if (warm == 1) ((Blk)BASE)(&dummy[0], chcr, mb);   /* the block's lines in, on RAM words */
    if (warm == 2) ((Blk)(BASE + ((uint32_t)(w + 1 + k) & ~3u) * 4u))(&dummy[0], chcr, mb);   /* half: only the lines from the MADR store's on */
    write32(DMA6 + 0, ma);
    write32(DMA6 + 4, NA);
    waitT2(2000);
    ((Blk)BASE)((volatile uint32_t*)DMA6, chcr, mb);
    for (int i = 0; i < 200000 && (read32(DMA6 + 8) & 0x01000000u); i++) {}
    const uint32_t c = read32(DMA6 + 8);
    if (c & 0x01000000u) write32(DMA6 + 8, 0);
    waitT2(3000);
    printf("W %d k %d %s: from A %2d from B %2d\n", w, k, warm == 2 ? "half" : warm ? "warm" : "cold", runDown(IA), runDown(IB));
}

int main() {
    printf("\ndma/dma-otc-cold\n");
    printf("A DMA6 start's CHCR store at word W of a line-aligned block, k nops, a store of MADR = B; cold (I-cache flushed) and warm.\n");
    printf("Words written downward from the first start's MADR A and from B. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t dp = read32(DPCR);
    write32(DPCR, dp | 0x08000000u);
    for (int w = 0; w < 4; w++) for (int k = 0; k <= 8; k++) for (int warm = 0; warm < 3; warm++) { if (warm == 2 && ((w + 1 + k) >> 2) == 0) continue; one(w, k, warm); }
    write32(DPCR, dp);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-otc-cold: Done.\n");
    for (;;) VSync(0);
}
