/*
 * dma/dma-otc-ioread22: dma-otc-ioread with two more reads - lhu of Timer 2 (the fuzz host's own end stamp) and lhu after the
 * start's MADR / BCR stores back to back (the fuzz case's shape) - and the start of the block timed by lhu too. Does a read issued k instructions after a DMA6 start, before the transfer takes the bus, wait for the
 * transfer? (the stream refit session, 2026-11-16).
 *
 * WHY. The fuzzer's 5901-0 / 5902-7 trails: a DMA6 start alone, then the harness's Timer 2 read a few instructions later - the
 * consoles' warm run 14 cycles, this core 66 (it holds every I/O read to the transfer's end). hwtests/dma-rates (v0.377.35): a
 * CHCR read straight behind the start comes back at the transfer's end reading Busy. Which reads wait, and from how far behind?
 * Here, warm: start (MADR A, BCR 200), k nops (0..12), one read of R - Timer 2, I_STAT, a RAM word (uncached? no: a cached-out
 * RAM line, lw from a line just invalidated by the table it is not in), DMA6's CHCR - then Timer 2 again, then CHCR polled idle.
 * READ IT. Rows "R r k n: a A b B c C": A Timer 2 just after the read (cycles from the CHCR store), B CHCR's busy bit as the read
 * returned it (1 / 0; - for other registers), C the cycles to CHCR6 idle.
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
static volatile uint32_t ramw[4] __attribute__((aligned(16)));
static volatile uint32_t dummy[64];
static volatile uint32_t out[4];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

/* a0 = DMA6 base (or dummy), a1 = CHCR value, a2 = the read's address, a3 = out; Timer 2 at a0 + 40h */
static uint32_t gMa;
static void gen(int k, int r) {
    int i = 0;
    if (r == 5) { CODE[i++] = 0x3C0B0000u | (gMa >> 16); CODE[i++] = 0x356B0000u | (gMa & 0xFFFF); CODE[i++] = 0x240C00C8u; }   /* t3 = MADR A, t4 = 200 */
    CODE[i++] = r >= 4 ? 0x94890040u : 0x8C890040u;   /* lhu / lw t1, 40h(a0)   Timer 2: the start */
    if (r == 5) { CODE[i++] = 0xAC8B0000u; CODE[i++] = 0xAC8C0004u; }   /* sw t3, 0(a0) MADR; sw t4, 4(a0) BCR, back to back */
    CODE[i++] = 0xAC850008u;               /* sw a1, 8(a0)     the start */
    for (int q = 0; q < k; q++) CODE[i++] = 0;
    CODE[i++] = r >= 4 ? 0x94C80000u : 0x8CC80000u;   /* lhu / lw t0, 0(a2)     THE READ */
    CODE[i++] = r >= 4 ? 0x948A0040u : 0x8C8A0040u;   /* lhu / lw t2, 40h(a0)   Timer 2 after it */
    CODE[i++] = 0xACE90000u;               /* sw t1, 0(a3) */
    CODE[i++] = 0xACE80004u;               /* sw t0, 4(a3) */
    CODE[i++] = 0xACEA0008u;               /* sw t2, 8(a3) */
    CODE[i++] = 0x03E00008u; CODE[i++] = 0;
    for (int q = 0; q < 8; q++) CODE[i++] = 0;
}
typedef void (*Blk)(volatile uint32_t*, uint32_t, volatile uint32_t*, volatile uint32_t*);

static void one(int r, int k) {
    const uint32_t chcr = 0x11000002u, ma = (uint32_t)&tbl[200] & 0x00FFFFFCu;
    static const char *const NM[] = { "t2", "istat", "ram", "chcr", "t2h", "t2hm" };
    volatile uint32_t *const addr[] = { (volatile uint32_t *)T2_COUNT, (volatile uint32_t *)0x1F801070u, &ramw[0], (volatile uint32_t *)(DMA6 + 8), (volatile uint32_t *)T2_COUNT, (volatile uint32_t *)T2_COUNT };
    gMa = ma;
    gen(k, r);
    ip_flush();
    ((Blk)BASE)(&dummy[0], chcr, &dummy[8], out);   /* the block's lines in */
    write32(DMA6 + 0, ma);
    write32(DMA6 + 4, 200);
    waitT2(2000);
    ((Blk)BASE)((volatile uint32_t*)DMA6, chcr, addr[r], out);
    const uint16_t t0 = (uint16_t)out[0];
    for (int i = 0; i < 200000 && (read32(DMA6 + 8) & 0x01000000u); i++) {}
    const uint16_t t1 = read16(T2_COUNT);
    if (read32(DMA6 + 8) & 0x01000000u) write32(DMA6 + 8, 0);
    waitT2(3000);
    printf("R %s k %d: a %u b %s c %u\n", NM[r], k, (unsigned)(uint16_t)((uint16_t)out[2] - t0),
        r == 3 ? ((out[1] & 0x01000000u) ? "1" : "0") : "-", (unsigned)(uint16_t)(t1 - t0));
}

int main() {
    printf("\ndma/dma-otc-ioread2\n");
    printf("A DMA6 start (200 words), k nops, one read (Timer 2, I_STAT, RAM, CHCR6), Timer 2 again; warm. a: cycles to after the read;\n");
    printf("b: CHCR6's busy bit as read; c: cycles to CHCR6 idle. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t dp = read32(DPCR);
    write32(DPCR, dp | 0x08000000u);
    for (int r = 0; r < 6; r++) for (int k = 0; k <= 8; k++) one(r, k);
    write32(DPCR, dp);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-otc-ioread2: Done.\n");
    for (;;) VSync(0);
}
