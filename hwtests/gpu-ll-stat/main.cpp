/*
 * gpu/gpu-ll-stat - GPUSTAT read back to back through a linked list behind a drawing triangle (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. hwtests/gpu-ll-ahead: 200 cycles into a list whose first node is a drawing flat triangle, the console reads GPUSTAT bits 28 / 26 / 25
 * as 1 0 1 while a chain of header-only nodes is still walked, and 0 0 0 while a chain of GP0(00h) nodes streams through - though those
 * NOPs never queue (all 32 are fetched by 1,000 cycles). This emulator reads 0 0 0 for both (its rule: bits 26 and 28 clear while any GPU
 * DMA is active); with bit 28 from its FIFO it reads 1 0 1 for both (experiments/_mkll28.js). Is the console's 28 held through the whole
 * stream, or does it read 1 between nodes?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, GP1(04h)=2. gpu-ll-ahead's list (the triangle, then 32 nodes of kind K: 0
 * header only, 1 one GP0(00h) word, 4 four), CHCR2 01000401h, then 160 GPUSTAT reads back to back (lw, the value's bits 28 26 25 and
 * Timer 2's low byte stored to the scratchpad each time: ~8 cycles a read), then the list and the GPU waited out. 2 tries a kind.
 * A line: G K try, then for every read t:b - t Timer 2 from the read before the kick, b bits 28 26 25 as a number 0-7 (4 = 28).
 *
 * READ IT LIKE THIS. Where b changes and how often: K 0 should turn 5 (28 and 25) once the walk's words are none; K 1 / 4 read 0 while
 * the NOPs flow - or 5 between nodes. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-ll-stat --frames=3000 --quiet=1500`.
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
#define NODES 32
#define READS 160
#define SPAD ((volatile uint16_t *)0x1F800000u)   /* [i]: Timer 2 at read i; [READS + i]: its bits */

static uint32_t list[8 + NODES * 5 + 4] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static uint32_t addr24(const uint32_t* p) { return (uint32_t)p & 0xFFFFFFu; }
static void build(int k) {
    uint32_t* p = list;
    uint32_t* tri = p;
    p += 5;
    tri[1] = 0x20406080u; tri[2] = (0u << 16) | 0u; tri[3] = (0u << 16) | 319u; tri[4] = (239u << 16) | 160u;
    uint32_t first = addr24(p);
    for (int i = 0; i < NODES; i++) {
        for (int w = 0; w < k; w++) p[1 + w] = 0x00000000u;
        const uint32_t* next = p + 1 + k;
        p[0] = ((uint32_t)k << 24) | addr24(next);
        p = (uint32_t*)next;
    }
    p[0] = 0x00FFFFFFu;
    tri[0] = (4u << 24) | first;
}
/* the kick, then READS reads of GPUSTAT: Timer 2 low half and the ready bits into the scratchpad, back to back */
static __attribute__((noinline, noclone)) void sample(uint32_t chcr) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lui   $t0, 0x1f80\n"
        "li    $t8, 160\n"
        "move  $t6, $t0\n"
        "lhu   $t5, 0x1120($t0)\n"
        "nop\n"
        "sh    $t5, 0x300($t0)\n"      /* Timer 2 before the kick (SPAD index 384, past the bits' 160..319) */
        "sw    %0, 0x10a8($t0)\n"
        "1:\n"
        "lw    $t7, 0x1814($t0)\n"      /* GPUSTAT */
        "lhu   $t5, 0x1120($t0)\n"      /* Timer 2 */
        "srl   $t7, $t7, 25\n"
        "sh    $t5, 0($t6)\n"
        "sh    $t7, 320($t6)\n"         /* the bits 25.. at [160 + i] */
        "addiu $t8, $t8, -1\n"
        "bnez  $t8, 1b\n"
        "addiu $t6, $t6, 2\n"
        ".set pop\n"
        : : "r"(chcr) : "t0", "t5", "t6", "t7", "t8", "memory");
}
static const int KS[3] = { 0, 1, 4 };
int main() {
    printf("\ngpu/gpu-ll-stat\n");
    printf("G K try t:b ... (GPUSTAT read back to back behind a drawing flat triangle in a list of 32 nodes of K GP0(00h) words; t Timer 2 "
           "from the read before the kick, b bits 28 26 25 as 4/2/1). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    static uint16_t tt[3][2][READS], bb[3][2][READS];
    for (int pass = 0; pass < 2; pass++) for (int ki = 0; ki < 3; ki++) for (int tr = 0; tr < 2; tr++) {
        build(KS[ki]);
        gpuIdle();
        write32(GP1, 0x01000000u);
        write32(GP1, 0x04000002u);
        write32(MADR2, addr24(list));
        write32(BCR2, 0);
        for (int i = 0; i < 4000; i++) { const uint32_t d = ((uint32_t)tr * 128u - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
        sample(0x01000401u);
        for (int i = 0; i < 2000000 && (read32(CHCR2) & 0x01000000u); i++) {}
        gpuIdle();
        write32(GP1, 0x04000000u);
        if (pass == 1) for (int i = 0; i < READS; i++) { tt[ki][tr][i] = (uint16_t)(SPAD[i] - SPAD[384]); bb[ki][tr][i] = (uint16_t)(((SPAD[READS + i] >> 3) & 1) << 2 | ((SPAD[READS + i] >> 1) & 1) << 1 | (SPAD[READS + i] & 1)); }
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int ki = 0; ki < 3; ki++) for (int tr = 0; tr < 2; tr++) {
        printf("G %d %d", KS[ki], tr);
        for (int i = 0; i < READS; i++) printf(" %u:%u", (unsigned)tt[ki][tr][i], (unsigned)bb[ki][tr][i]);
        printf("\n");
    }
    printf("\ngpu/gpu-ll-stat: Done.\n");
    for (;;) VSync(0);
}
