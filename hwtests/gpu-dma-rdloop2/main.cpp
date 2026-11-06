/*
 * gpu/gpu-dma-rdloop2 - gpu/bandwidth's vramToCpu transfer by where its buffer is and how it is polled (2026-11-06 small hours,
 * for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-rdloop on the second SCPH-5501: bandwidth's loop rebuilt (psn00bsdk's DrawPrim, ps1-tests' vramReadDMA)
 * spends 168 + 60 + ~150 + 53 + 135 cycles a call outside its 320x240 read - as the emulator it was written for, to the cycle -
 * and the read takes 83,350 (34.73 a block); gpu/bandwidth's own call reads 84,784 on every new-GPU console (15,754-15,760
 * hblanks for 400). Two things differ: its buffer (801A0000h, the second megabyte; that test's own is in its first) and its
 * poll (ps1-tests' waitForChannel: CHCR read back to back; that test's read Timer 2 between).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock / 8, the display 320x240 15-bit at (0,0). Ten 320x240 reads by
 * DMA2 in 16-word blocks back to back, bandwidth's way (DrawSync, GP0(C0h), DrawSync, GPUSTAT bit 27, the DMA's setup), two
 * untimed and eight timed - each transfer from CHCR2's store to its bit 24 clear, in units of 8 cycles - and the eight
 * calls' total; for each of four cases: the buffer here (the first megabyte) or at 801A0000h, CHCR2 polled with a Timer 2
 * read between or back to back (lw / srl / andi / bnez). A line per case: Q <case> then the eight transfers and the total,
 * in cycles.
 *
 * READ IT LIKE THIS. Each case's transfers against case A's: what the place of the buffer or the poll costs a 2,400-block
 * read. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdloop2 --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }   /* units of 8 cycles */
static uint32_t big[38400] __attribute__((aligned(1024)));

#define LIM 5000000u
static void drawSync() {
    const uint32_t t0 = now();
    if ((read32(GP1) >> 29) & 3) {
        while ((read32(D2_CHCR) & 0x01000000u) && now() - t0 < LIM) {}
        while (((read32(GP1) >> 26) & 5) != 5 && now() - t0 < LIM) {}
    } else while (!((read32(GP1) >> 28) & 1) && now() - t0 < LIM) {}
}
/* ps1-tests' waitForChannel: CHCR2 read back to back until bit 24 clears (bounded by a count, no timer between) */
static __attribute__((noinline, noclone)) void tightWait() {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "lui   $t1, 0x0100\n"
        "1:\n"
        "lw    $t2, 0x10a8($t0)\n"
        "addiu $t1, $t1, -1\n"
        "srl   $t2, $t2, 24\n"
        "andi  $t2, $t2, 1\n"
        "beqz  $t2, 2f\n"
        "nop\n"
        "bnez  $t1, 1b\n"
        "nop\n"
        "2:\n"
        ".set pop\n"
        ::: "t0", "t1", "t2", "memory");
}

struct Case { const char* name; int high, tight; };
static const Case CS[4] = { { "A buffer here      poll+timer", 0, 0 }, { "B buffer here      poll tight", 0, 1 },
                            { "C buffer 801A0000h poll+timer", 1, 0 }, { "D buffer 801A0000h poll tight", 1, 1 } };
static uint32_t dt[4][8], tot[4];

static uint32_t call(const Case& c) {
    drawSync();
    write32(GP1, 0x04000000u);
    write32(GP0, 0xC0000000u); write32(GP0, 0); write32(GP0, (240u << 16) | 320u);
    drawSync();
    write32(GP1, 0x04000003u);
    { const uint32_t t = now(); while (!(read32(GP1) & 0x08000000u) && now() - t < LIM) {} }
    write32(DPCR, read32(DPCR) | 0x00000800u);
    while (read32(D2_CHCR) & 0x01000000u) {}
    write32(D2_MADR, c.high ? 0x1A0000u : ((uint32_t)big & 0xFFFFFCu));
    write32(D2_BCR, (2400u << 16) | 16u);
    const uint32_t t5 = now();
    write32(D2_CHCR, 0x01000200u);
    if (c.tight) tightWait(); else while ((read32(D2_CHCR) & 0x01000000u) && now() - t5 < LIM) {}
    return now() - t5;
}

int main() {
    printf("\ngpu/gpu-dma-rdloop2\n");
    printf("Q case, eight 320x240 reads by DMA2 in 16-word blocks back to back, bandwidth's way: each transfer from CHCR2's store to its\n"
           "bit 24 clear, then the eight calls' total (cycles, Timer 2 at the system clock / 8). No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 38400; i++) big[i] = 0;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200);   /* system clock / 8 */
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int k = 0; k < 4; k++) {
        call(CS[k]); call(CS[k]);
        const uint32_t a = now();
        for (int c = 0; c < 8; c++) dt[k][c] = call(CS[k]);
        tot[k] = now() - a;
        write32(GP1, 0x04000000u);
    }
    write32(GP1, 0x04000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    { const uint32_t t = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t < LIM) {} }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int k = 0; k < 4; k++) {
        printf("Q %s", CS[k].name);
        for (int c = 0; c < 8; c++) printf(" %u", (unsigned)(dt[k][c] * 8));
        printf(" total %u\n", (unsigned)(tot[k] * 8));
    }
    printf("\ngpu/gpu-dma-rdloop2: Done.\n");
    for (;;) VSync(0);
}
