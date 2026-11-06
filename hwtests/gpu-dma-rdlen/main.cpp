/*
 * gpu/gpu-dma-rdlen - a VRAM-to-RAM read's 16-word pace by its length, the CPU's poll, the refresh hold-off and the display
 * (2026-11-06 small hours, for the second SCPH-5501)
 *
 * WHY. On the second SCPH-5501 a read by DMA2 in 16-word blocks polled with CHCR2 back to back runs 34.6-34.74 cycles a block over 128
 * blocks (dma-len, gpu-dma-rdpoll), 35.0 over 512 (gpu-dma-rdpoll2) and 35.15 over 2,400 (gpu-dma-rdloop2) - and polled with a Timer 2
 * read and RAM stores between, 34.72 at every length (gpu-dma-rdlong, -rdloop2). Something grows with the length under the tight poll.
 * A DRAM refresh postponed while the bus never idles would; the live psx-spx's DRAM_CTRL (1F801060h) bits 0-2 set how long a refresh
 * may wait while the controller serves DMA (0, the BIOS's: it yields at once; 6: 128 cycles), and gpu-dma-refctl found them working.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock / 8 (read only before and after a transfer). Reads of 256 x h at (0,256) by
 * DMA2 to RAM in 16-word blocks - 128, 512 and 2,048 blocks (h 16, 64, 256) - 6,000 cycles after GPUSTAT bit 27, polled until CHCR2's
 * bit 24 clears three ways: T CHCR2 back to back; S CHCR2, a Timer 2 read and two RAM stores (gpu-dma-rdpoll2's fastest); N CHCR2 then 40
 * nops; with DRAM_CTRL bits 0-2 = 0 and = 6 (the register written back after each), the display on (320x240) and off. Four of each.
 * A line per case: R <display> <hold-off> <poll> <blocks> then four times (cycles) and four turn counts.
 *
 * READ IT LIKE THIS. Each case's cycles a block against its length: whether the poll's cost grows with the transfer, and whether the
 * refresh hold-off or the display moves it. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdlen --frames=3000 --quiet=1500`.
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
#define DRAM_CTRL 0x1F801060u

static uint32_t big[32768 + 64] __attribute__((aligned(1024)));
static volatile uint32_t sink[4];

static __attribute__((noinline, noclone)) uint32_t poll(int k) {
    uint32_t n;
    if (k == 0) __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "memory");
    else if (k == 1) __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\nlhu $t3,0x1120($t0)\nsw $t3,0(%1)\nsh $t3,4(%1)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) : "r"(sink) : "t0", "t2", "t3", "memory");
    else __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\naddiu %0,%0,1\n.rept 40\nnop\n.endr\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "memory");
    return n;
}

static void gpuReady() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void waitT2(uint16_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }   /* units of 8 cycles */
static void display(int on) {
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u | (on ? 0u : 1u));
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
}

static const uint32_t NB[3] = { 128, 512, 2048 };
static const char PC[3] = { 'T', 'S', 'N' };
static uint32_t tm[2][2][3][3][4], tn[2][2][3][3][4];   /* [display][hold][poll][len][rep] */

static void one(int d, int h, int k, int li, int r, uint32_t saved) {
    const uint32_t blocks = NB[li], rows = blocks * 16 * 2 / 256;
    gpuReady();
    write32(GP1, 0x04000003u);
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (rows << 16) | 256u);
    for (int i = 0; i < 40000 && !(read32(GP1) & 0x08000000u); i++) {}
    write32(D2_MADR, (uint32_t)big & 0xFFFFFCu);
    write32(D2_BCR, (blocks << 16) | 16u);
    write32(DRAM_CTRL, (saved & ~7u) | (h ? 6u : 0u));
    waitT2(750);   /* 6,000 cycles */
    const uint16_t t0 = read16(T2_COUNT);
    write32(D2_CHCR, 0x01000200u);
    const uint32_t n = poll(k);
    const uint16_t t1 = read16(T2_COUNT);
    write32(DRAM_CTRL, saved);
    tm[d][h][k][li][r] = (uint32_t)(uint16_t)(t1 - t0) * 8u; tn[d][h][k][li][r] = n;
    for (int i = 0; i < 40000 && (read32(GP1) & 0x08000000u); i++) (void)read32(GP0);
}

int main() {
    printf("\ngpu/gpu-dma-rdlen\n");
    printf("R display hold-off poll blocks, four times (cycles, from CHCR2's store to the poll's end; Timer 2 at the system clock / 8) and\n"
           "the poll's turns, of reads of 256 x h at (0,256) by DMA2 in 16-word blocks. Polls: T CHCR2 back to back, S + a Timer 2 read and\n"
           "two RAM stores, N + 40 nops; hold-off: DRAM_CTRL bits 0-2 = 0 or 6. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 32768 + 64; i++) big[i] = 0;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    const uint32_t saved = read32(DRAM_CTRL);
    for (int d = 0; d < 2; d++) {
        display(d);
        for (int r = 0; r < 4; r++)
            for (int h = 0; h < 2; h++)
                for (int k = 0; k < 3; k++)
                    for (int li = 0; li < 3; li++) one(d, h, k, li, r, saved);
    }
    write32(DRAM_CTRL, saved);
    write32(GP1, 0x04000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    gpuReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("DRAM_CTRL as found: %08x\n", (unsigned)saved);
    for (int d = 0; d < 2; d++)
        for (int h = 0; h < 2; h++)
            for (int k = 0; k < 3; k++)
                for (int li = 0; li < 3; li++) {
                    printf("R %s %d %c %u", d ? "on " : "off", h ? 6 : 0, PC[k], (unsigned)NB[li]);
                    for (int r = 0; r < 4; r++) printf(" %u", (unsigned)tm[d][h][k][li][r]);
                    printf(" turns");
                    for (int r = 0; r < 4; r++) printf(" %u", (unsigned)tn[d][h][k][li][r]);
                    printf("\n");
                }
    printf("\ngpu/gpu-dma-rdlen: Done.\n");
    for (;;) VSync(0);
}
