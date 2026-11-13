/*
 * gpu/gpu-dma-arm - a one-block GPU DMA and the CPU's own polygon stored around its start: whose words does the GPU take first,
 * and do the block's words become the polygon's corners? (2026-11-13)
 *
 * WHY. The whole-console fuzzer's finding 202-9 on the second SCPH-5501: a 12-word SyncMode 1 block (GP1(04h) = 2) started by its
 * store to CHCR, and 2, 3 or 4 instructions later the CPU's store of a quad's command word to GP0. The console's VRAM held the
 * real quad AND a quad of its colour whose corners were the BLOCK's first four words - the GPU took the CPU's command word and
 * then the block's words as its vertices. The fuzzer runs a case twice, first with its code not in the I-cache, and VRAM keeps
 * both, so the dump could not say which pass drew which. This test runs the race one pass at a time.
 *
 * WHAT IT DOES. Per row: an 128 x 8 strip at (0,256) cleared; GP1(04h) = 2; MADR / BCR for ONE block of four words - a dark red
 * flat triangle (0,0) (8,0) (0,8), command word 20040040h; then, nothing between that the compiler can add:
 *   K >= 0   the store to CHCR2, K nops, the store of the quad's command word 2800F800h (green) to GP0
 *   K < 0    the store of the quad's command word, -K - 1 nops, the store to CHCR2
 * and behind either, two nops apart, the quad's four vertex stores: (96,0) (104,0) (96,8) (104,8). Then the strip is read back.
 * m 0: the same instructions ran once on RAM words first, so their cache lines are in. m 1: the BIOS's FlushCache() is called
 * between that and the real pass, so every line is fetched again (the fuzzer's first pass).
 *
 * READ IT LIKE THIS. R m K A B W g r busy: A the pixel at (2,2) - 0008 the triangle, 03e0 the quad's colour; B at (98,2) - 03e0
 * the CPU's quad as stored; W at (34,2) - 03e0 only if a polygon had the triangle's command word as a corner (64,4); g / r the
 * strip's pixels of the quad's / the triangle's colour; busy = CHCR2 still read busy after the wait (the transfer never ended).
 *   A 0008  B 03e0  W 0000   each drew as stored (the block first, or held until the quad was complete)
 *   A 03e0  B 0000  W 03e0   the quad took the block's four words as its corners; the CPU's vertex words ran as commands
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-dma-arm --emu`.
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
#define SW 128
#define SH 8

static uint32_t buf[4] __attribute__((aligned(16)));
static uint16_t back[SW * SH];
static volatile uint32_t dummy[2];
/* the BIOS's A(44h) FlushCache: every I-cache line invalid on return */
static void __attribute__((noinline)) biosFlushCache() { __asm__ volatile("li $9, 0x44\nli $8, 0xA0\njalr $8\nnop\n" : : : "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9", "$10", "$11", "$12", "$13", "$14", "$15", "$24", "$25", "$31", "memory"); }
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

#define N1 "nop\n"
#define N2 N1 N1
#define N4 N2 N2
#define N8 N4 N4
#define TAIL N2 "sw %4, 0(%3)\n" N2 "sw %5, 0(%3)\n" N2 "sw %6, 0(%3)\n" N2 "sw %7, 0(%3)\n"
/* the store that starts the block, K nops, the quad's command word, its vertices */
#define AFTER(NOPS) __asm__ volatile(".set push\n.set noreorder\nsw %0, 0(%1)\n" NOPS "sw %2, 0(%3)\n" TAIL ".set pop\n" : : "r"(chcr), "r"(pc), "r"(c0), "r"(pg), "r"(v1), "r"(v2), "r"(v3), "r"(v4) : "memory")
/* the quad's command word, J nops, the store that starts the block, the vertices */
#define BEFORE(NOPS) __asm__ volatile(".set push\n.set noreorder\nsw %2, 0(%3)\n" NOPS "sw %0, 0(%1)\n" TAIL ".set pop\n" : : "r"(chcr), "r"(pc), "r"(c0), "r"(pg), "r"(v1), "r"(v2), "r"(v3), "r"(v4) : "memory")
static void __attribute__((noinline)) fire(volatile uint32_t* pc, volatile uint32_t* pg, uint32_t chcr, int k) {
    const uint32_t c0 = 0x2800F800u, v1 = 0x00000060u, v2 = 0x00000068u, v3 = 0x00080060u, v4 = 0x00080068u;
    switch (k) {
        case -4: BEFORE(N2 N1); break;
        case -3: BEFORE(N2); break;
        case -2: BEFORE(N1); break;
        case -1: BEFORE(""); break;
        case 0: AFTER(""); break;
        case 1: AFTER(N1); break;
        case 2: AFTER(N2); break;
        case 3: AFTER(N2 N1); break;
        case 4: AFTER(N4); break;
        case 5: AFTER(N4 N1); break;
        case 6: AFTER(N4 N2); break;
        case 7: AFTER(N4 N2 N1); break;
        case 8: AFTER(N8); break;
        case 9: AFTER(N8 N1); break;
        case 10: AFTER(N8 N2); break;
        case 11: AFTER(N8 N2 N1); break;
        default: AFTER(N8 N4); break;
    }
}

static void one(int m, int k) {
    const uint32_t at = (256u << 16) | 0u, size = ((uint32_t)SH << 16) | (uint32_t)SW;
    const uint32_t chcr = 0x01000201u;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP1, 0x04000000u);
    write32(GP0, 0x02000000u); write32(GP0, at); write32(GP0, size);
    gpuIdle();
    fire(&dummy[0], &dummy[1], chcr, k);                         /* the same instructions on two words of RAM: their cache lines are in */
    if (m) biosFlushCache();                                         /* ... or out again: every line of the real pass is fetched */
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (1u << 16) | 4u);
    waitT2(2000);
    fire((volatile uint32_t*)CHCR2, (volatile uint32_t*)GP0, chcr, k);
    for (int i = 0; i < 200000 && (read32(CHCR2) & 0x01000000u); i++) {}   /* bounded: a transfer that never ends is a row, not a hang */
    const int busy = (read32(CHCR2) & 0x01000000u) ? 1 : 0;
    if (busy) write32(CHCR2, 0);
    waitT2(3000);
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* whatever is left of a command half received */
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, at); write32(GP0, size);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int j = 0; j < SW * SH / 2; j++) { const uint32_t v = read32(GP0); back[2 * j] = (uint16_t)v; back[2 * j + 1] = (uint16_t)(v >> 16); }
    int g = 0, r = 0;
    for (int j = 0; j < SW * SH; j++) { if (back[j] == 0x03E0) g++; if (back[j] == 0x0008) r++; }
    printf("R %d %d %04x %04x %04x %d %d %d\n", m, k, (unsigned)back[2 * SW + 2], (unsigned)back[2 * SW + 98], (unsigned)back[2 * SW + 34], g, r, busy);
}

int main() {
    printf("\ngpu/gpu-dma-arm\n");
    printf("R m K A B W g r busy: a one-block GPU DMA (a dark red triangle, four words) started by its store to CHCR2 and the CPU's green quad:\n");
    printf("K >= 0 its command word K nops BEHIND that store, K < 0 the command word first and the store -K - 1 nops behind it; m 1 with the\n");
    printf("code fetched again (FlushCache). A (2,2): 0008 the triangle; B (98,2): 03e0 the quad as stored; W (34,2): 03e0 only if the quad took\n");
    printf("the block's words as its corners. g / r pixels of each colour in the 128 x 8 strip. No assertions.\n\n");
    buf[0] = 0x20040040u; buf[1] = 0x00000000u; buf[2] = 0x00000008u; buf[3] = 0x00080000u;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u | (256u << 10));
    write32(GP0, 0xE4000000u | (263u << 10) | 127u); write32(GP0, 0xE5000000u | (256u << 11)); write32(GP0, 0xE6000000u);
    for (int p = 0; p < 3; p++) for (int m = 0; m < 2; m++) for (int k = -4; k <= 12; k++) one(m, k);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-arm: Done.\n");
    for (;;) VSync(0);
}
