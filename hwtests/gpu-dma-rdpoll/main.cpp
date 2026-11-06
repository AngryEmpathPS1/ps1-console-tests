/*
 * gpu/gpu-dma-rdpoll - what the CPU's poll does to a VRAM-to-RAM read by DMA2: its reads, its RAM stores (2026-11-06 small hours,
 * for the second SCPH-5501)
 *
 * WHY. On the second SCPH-5501, dma-len's 2,048-word read in 8-word blocks takes ~4,215 cycles (16.5 a block, the bus's pace) and
 * hwtests/gpu-dma-rdbs's copy of that row 19.67 a block; in 4-word blocks 12.25 against 14.63. dma-len polls CHCR2 (or I_STAT)
 * with nothing else in the loop; gpu-dma-rdbs polls CHCR2 and reads Timer 2, storing its count to RAM (two stores a turn). And
 * gpu-dma-rdloop2: 16-word blocks read 0.43 cycles a block slower polled back to back than polled with the timer between.
 *
 * WHAT IT DOES. Interrupts off, the display off, Timer 2 at the system clock / 8 (read only before and after). A 2,048-word read
 * of 64x64 at (0,256) by DMA2 to RAM, in 4-, 8- and 16-word blocks, 6,000 cycles after GPUSTAT bit 27 (dma-len's), polled until
 * CHCR2's bit 24 clears five ways: P0 CHCR2 back to back; P1 CHCR2 and a Timer 2 read; P2 CHCR2 and a store to RAM; P3 CHCR2 and
 * a store to the scratchpad; P4 CHCR2, a Timer 2 read and two stores to RAM (gpu-dma-rdbs's poll); P5 CHCR2 then 40 nops. Four
 * of each, each timed from CHCR2's store to the poll's end; and the turns the poll took. A line per block size and poll:
 * P <bs> <poll> then four times (cycles) and four turn counts.
 *
 * READ IT LIKE THIS. Each poll against P0 at the same block size: what its reads or stores cost the transfer. No assertions -
 * compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdpoll --frames=3000 --quiet=1500`.
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

static uint32_t big[2048 + 64] __attribute__((aligned(1024)));
static volatile uint32_t sink[4];

/* the poll, one of six; returns its turns. $t0 = 1F80h << 16; CHCR2 at 10A8h; Timer 2 at 1120h; the stores to sink (RAM) or
 * the scratchpad (1F800000h). Bounded by a count. */
static __attribute__((noinline, noclone)) uint32_t poll(int kind) {
    uint32_t n;
    switch (kind) {
    case 0: __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "memory"); break;
    case 1: __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\nlhu $t3,0x1120($t0)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "t3", "memory"); break;
    case 2: __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\nsw %0,0(%1)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) : "r"(sink) : "t0", "t2", "memory"); break;
    case 3: __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\nsw %0,0($t0)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "memory"); break;
    case 4: __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\nlhu $t3,0x1120($t0)\nsw $t3,0(%1)\nsh $t3,4(%1)\naddiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) : "r"(sink) : "t0", "t2", "t3", "memory"); break;
    default: __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\naddiu %0,%0,1\n.rept 40\nnop\n.endr\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "memory"); break;
    }
    return n;
}

static void gpuReady() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void waitT2(uint16_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }   /* units of 8 cycles */

static uint32_t tm[3][6][4], tn[3][6][4];

static void one(int bi, int kind, int r) {
    static const uint32_t BS[3] = { 4, 8, 16 };
    const uint32_t bs = BS[bi];
    gpuReady();
    write32(GP1, 0x04000003u);
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (64u << 16) | 64u);
    for (int i = 0; i < 40000 && !(read32(GP1) & 0x08000000u); i++) {}
    write32(D2_MADR, (uint32_t)big & 0xFFFFFCu);
    write32(D2_BCR, ((2048u / bs) << 16) | bs);
    waitT2(750);   /* 6,000 cycles */
    const uint16_t t0 = read16(T2_COUNT);
    write32(D2_CHCR, 0x01000200u);
    const uint32_t n = poll(kind);
    const uint16_t t1 = read16(T2_COUNT);
    tm[bi][kind][r] = (uint32_t)(uint16_t)(t1 - t0) * 8u; tn[bi][kind][r] = n;
    for (int i = 0; i < 40000 && (read32(GP1) & 0x08000000u); i++) (void)read32(GP0);
}

int main() {
    printf("\ngpu/gpu-dma-rdpoll\n");
    printf("P bs poll, four times (cycles, from CHCR2's store to the poll's end; Timer 2 at the system clock / 8) and the poll's turns, of a\n"
           "2,048-word read of 64x64 at (0,256) by DMA2, the display off. Polls: 0 CHCR2 alone, 1 + a Timer 2 read, 2 + a RAM store, 3 + a\n"
           "scratchpad store, 4 + a Timer 2 read and two RAM stores, 5 + 40 nops. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 2048 + 64; i++) big[i] = 0;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x03000001u);
    for (int r = 0; r < 4; r++)
        for (int bi = 0; bi < 3; bi++)
            for (int k = 0; k < 6; k++) one(bi, k, r);
    write32(GP1, 0x04000000u);
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    gpuReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    static const uint32_t BS[3] = { 4, 8, 16 };
    for (int bi = 0; bi < 3; bi++)
        for (int k = 0; k < 6; k++) {
            printf("P %u %d", (unsigned)BS[bi], k);
            for (int r = 0; r < 4; r++) printf(" %u", (unsigned)tm[bi][k][r]);
            printf(" turns");
            for (int r = 0; r < 4; r++) printf(" %u", (unsigned)tn[bi][k][r]);
            printf("\n");
        }
    printf("\ngpu/gpu-dma-rdpoll: Done.\n");
    for (;;) VSync(0);
}
