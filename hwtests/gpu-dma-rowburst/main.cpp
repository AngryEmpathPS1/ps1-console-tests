/*
 * gpu/gpu-dma-rowburst - DMA bursts across main RAM's 1 KB rows (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-ramrow on the second SCPH-5501: a SyncMode 1 GPU upload's block whose words cross a 1 KB row of main RAM costs
 * ~4 cycles, a block that ends on the boundary nothing (psx-spx: 2,048 rows of 1 KB, a fresh row address on every transaction,
 * fast-page mode only within one burst). Whether a BURST - SyncMode 0, one transaction of the whole transfer - pays the same at a row
 * it crosses, and whether another channel does (DMA6, the ordering table's clear, writing RAM downwards), is not measured.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Transfers of 64 words placed to cross a 1 KB row after O words (O 0: on
 * the boundary, nothing crossed inside; 8, 16, 24, 40, 56):
 *   B  a GPU burst (SyncMode 0) from RAM into GP0(A0h), a rectangle one row of 64 words;
 *   S  the same as four SyncMode 1 blocks of 16 (the control: O 16 crosses between two blocks, the others inside one);
 *   O  DMA6 (OTC) writing 64 words downwards from MADR6 (its first O words above the boundary).
 * Each at 8 places of the DRAM refresh (Timer 2's count mod 256 = 32 k), after a dry run of all of it (the code cached), timed from
 * the Timer 2 read before the CHCR store to the Timer 2 read after the first CHCR read with its busy bit clear (a ~7-cycle poll).
 * A line per transfer: R X O p t.
 *
 * READ IT LIKE THIS. Per X, the mean t over the places by O, against O 0: a crossing's cost. S should read ~+4 at O 8 / 24 / 40 / 56 and
 * nothing at 16. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rowburst --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define MADR6 0x1F8010E0u
#define BCR6  0x1F8010E4u
#define CHCR6 0x1F8010E8u
#define WORDS 64

static uint32_t big[1024] __attribute__((aligned(1024)));   /* the boundary is big + 1 KB (word 256) */
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the kick and the poll: Timer 2 before the CHCR store, then CHCR read until its busy bit (24) is clear, then Timer 2 */
static __attribute__((noinline, noclone)) uint32_t timed(uint32_t chcrAddr, uint32_t chcrVal) {
    uint32_t t0, t1;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lhu   %0, 0x1120($t0)\n"
        "nop\n"
        "sw    %3, 0(%2)\n"
        "1:\n"
        "lw    $t2, 0(%2)\n"
        "nop\n"
        "srl   $t2, $t2, 24\n"
        "andi  $t2, $t2, 1\n"
        "bnez  $t2, 1b\n"
        "nop\n"
        "lhu   %1, 0x1120($t0)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(t0), "=&r"(t1)
        : "r"(chcrAddr), "r"(chcrVal)
        : "t0", "t2", "memory");
    return (t1 - t0) & 0xFFFFu;
}
static void place(uint32_t p) { for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; } }
static void armGpu(const uint32_t* src, int blocks) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (1u << 16) | (WORDS * 2));
    write32(MADR2, (uint32_t)src & 0xFFFFFCu);
    write32(BCR2, blocks ? ((WORDS / 16) << 16) | 16u : WORDS);
}
static const uint32_t OS[6] = { 0, 8, 16, 24, 40, 56 };
static uint32_t one(char x, uint32_t o, uint32_t p) {
    uint32_t t;
    if (x == 'O') {
        write32(MADR6, (uint32_t)&big[256 + o - 1] & 0xFFFFFCu);   /* downwards: its first O words at and above the boundary */
        write32(BCR6, WORDS);
        place(p);
        t = timed(CHCR6, 0x11000002u);
    } else {
        armGpu(&big[256 - o], x == 'S');
        place(p);
        t = timed(CHCR2, x == 'S' ? 0x01000201u : 0x11000001u);
        gpuIdle();
        write32(GP1, 0x04000000u);
    }
    return t;
}
int main() {
    printf("\ngpu/gpu-dma-rowburst\n");
    printf("R X O p t (64 words crossing a 1 KB row of main RAM after O words: X B a GPU burst, S four SyncMode 1 blocks of 16, O DMA6 "
           "writing downwards; p the place on the refresh grid; t from the Timer 2 read before the CHCR store to the one after it reads "
           "idle). No assertions.\n\n");
    for (int i = 0; i < 1024; i++) big[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x08000800u);   /* DMA2 and DMA6 enabled */
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static const char XS[3] = { 'B', 'S', 'O' };
    static uint16_t res[3][6][8];
    for (int xi = 0; xi < 3; xi++) for (int oi = 0; oi < 6; oi++) one(XS[xi], OS[oi], 0);   /* the code cached */
    for (int xi = 0; xi < 3; xi++) for (int oi = 0; oi < 6; oi++) for (uint32_t k = 0; k < 8; k++) res[xi][oi][k] = (uint16_t)one(XS[xi], OS[oi], k * 32u);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int xi = 0; xi < 3; xi++) for (int oi = 0; oi < 6; oi++) for (int k = 0; k < 8; k++)
        printf("R %c %u %u %u\n", XS[xi], (unsigned)OS[oi], (unsigned)(k * 32), (unsigned)res[xi][oi][k]);
    printf("\ngpu/gpu-dma-rowburst: Done.\n");
    for (;;) VSync(0);
}
