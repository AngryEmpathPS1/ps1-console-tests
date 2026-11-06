/*
 * gpu/gpu-dma-rdpoll2 - a VRAM-to-RAM read's 16-word blocks against the period of the CPU's poll, and the parts of the fastest poll
 * (2026-11-06 small hours, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-dma-rdpoll on the second SCPH-5501: dma-len's 2,048-word read in 16-word blocks takes 4,447 cycles polled back to
 * back on CHCR2 (238 turns for the 128 blocks), 4,492 with 40 nops between the reads (93 turns) and 4,358 with a Timer 2 read and two
 * RAM stores in each turn (115) - the GPU's pace is not the whole of a block's time; the CPU's poll moves it up to a cycle a block, and
 * gpu-dma-rdloop2 found bandwidth's back-to-back poll +0.43 a block. If a read the CPU has on the bus when the GPU asks for the next
 * block holds the block back, the cost goes with the poll's period and its reads' length; if something else, not.
 *
 * WHAT IT DOES. Interrupts off, the display off, Timer 2 at the system clock / 8 (read only before and after). An 8,192-word read of
 * 128x128 at (0,256) by DMA2 to RAM in 16-word blocks (512), 6,000 cycles after GPUSTAT bit 27, polled until CHCR2's bit 24 clears:
 * N0..N48 - CHCR2 then N nops (0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48); and P4's parts - T: CHCR2 + a Timer 2 read; S1 / S2: +
 * one / two RAM stores; T1 / T2: + a Timer 2 read and one / two RAM stores; L2: + two RAM loads; X: + a scratchpad load. Four of
 * each, each timed from CHCR2's store to the poll's end, and the poll's turns. A line per poll: Q <poll> four times (cycles) and four
 * turn counts.
 *
 * READ IT LIKE THIS. Each poll's time against the others': what the poll costs the transfer, by its period (the N rows) and by what
 * it does (the rest). No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-rdpoll2 --frames=3000 --quiet=1500`.
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

static uint32_t big[8192 + 64] __attribute__((aligned(1024)));
static volatile uint32_t sink[4];

/* CHCR2 then N nops, until bit 24 clears; $t0 = 1F80h << 16 */
#define POLLN(N) __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\naddiu %0,%0,1\n.rept " #N "\nnop\n.endr\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) :: "t0", "t2", "memory")
#define POLLX(BODY) __asm__ volatile(".set push\n.set noreorder\nlui $t0,0x1f80\nmove %0,$zero\n1:\nlw $t2,0x10a8($t0)\n" BODY "addiu %0,%0,1\nsrl $t2,$t2,24\nandi $t2,$t2,1\nbnez $t2,1b\nnop\n.set pop\n" : "=&r"(n) : "r"(sink) : "t0", "t2", "t3", "t4", "memory")

#define NP 19
static const char* const PN[NP] = { "N0", "N2", "N4", "N6", "N8", "N10", "N12", "N16", "N20", "N24", "N32", "N40", "N48", "T", "S1", "S2", "T1", "T2", "L2" };

static __attribute__((noinline, noclone)) uint32_t poll(int k) {
    uint32_t n;
    switch (k) {
    case 0: POLLN(0); break;   case 1: POLLN(2); break;   case 2: POLLN(4); break;   case 3: POLLN(6); break;
    case 4: POLLN(8); break;   case 5: POLLN(10); break;  case 6: POLLN(12); break;  case 7: POLLN(16); break;
    case 8: POLLN(20); break;  case 9: POLLN(24); break;  case 10: POLLN(32); break; case 11: POLLN(40); break;
    case 12: POLLN(48); break;
    case 13: POLLX("lhu $t3,0x1120($t0)\n"); break;
    case 14: POLLX("sw %0,0(%1)\n"); break;
    case 15: POLLX("sw %0,0(%1)\nsh %0,4(%1)\n"); break;
    case 16: POLLX("lhu $t3,0x1120($t0)\nsw $t3,0(%1)\n"); break;
    case 17: POLLX("lhu $t3,0x1120($t0)\nsw $t3,0(%1)\nsh $t3,4(%1)\n"); break;
    default: POLLX("lw $t3,0(%1)\nlw $t4,4(%1)\n"); break;
    }
    return n;
}

static void gpuReady() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static void waitT2(uint16_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }   /* units of 8 cycles */

static uint32_t tm[NP][4], tn[NP][4];

static void one(int k, int r) {
    gpuReady();
    write32(GP1, 0x04000003u);
    write32(GP0, 0xC0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (128u << 16) | 128u);
    for (int i = 0; i < 40000 && !(read32(GP1) & 0x08000000u); i++) {}
    write32(D2_MADR, (uint32_t)big & 0xFFFFFCu);
    write32(D2_BCR, (512u << 16) | 16u);
    waitT2(750);   /* 6,000 cycles */
    const uint16_t t0 = read16(T2_COUNT);
    write32(D2_CHCR, 0x01000200u);
    const uint32_t n = poll(k);
    const uint16_t t1 = read16(T2_COUNT);
    tm[k][r] = (uint32_t)(uint16_t)(t1 - t0) * 8u; tn[k][r] = n;
    for (int i = 0; i < 40000 && (read32(GP1) & 0x08000000u); i++) (void)read32(GP0);
}

int main() {
    printf("\ngpu/gpu-dma-rdpoll2\n");
    printf("Q poll, four times (cycles, from CHCR2's store to the poll's end; Timer 2 at the system clock / 8) and the poll's turns, of an\n"
           "8,192-word read of 128x128 at (0,256) by DMA2 in 16-word blocks, the display off. Polls: N<n> CHCR2 then n nops; T + a Timer 2\n"
           "read; S1 / S2 + one / two RAM stores; T1 / T2 + a Timer 2 read and one / two RAM stores; L2 + two RAM loads. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    for (int i = 0; i < 8192 + 64; i++) big[i] = 0;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x03000001u);
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < NP; k++) one(k, r);
    write32(GP1, 0x04000000u);
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    gpuReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int k = 0; k < NP; k++) {
        printf("Q %s", PN[k]);
        for (int r = 0; r < 4; r++) printf(" %u", (unsigned)tm[k][r]);
        printf(" turns");
        for (int r = 0; r < 4; r++) printf(" %u", (unsigned)tn[k][r]);
        printf("\n");
    }
    printf("\ngpu/gpu-dma-rdpoll2: Done.\n");
    for (;;) VSync(0);
}
