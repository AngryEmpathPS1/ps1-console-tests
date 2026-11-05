/*
 * gpu/gpu-dma-stolast3 - which register a read takes decides whether it beats a block starting: gpu-dma-stolast2's kick and N nops, then
 * a read of Timer 2, or of CHCR2, or none, then the timing read of Timer 2 (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-stolast2 on the second SCPH-5501: a Timer 2 read issued the cycle before an upload's first block starts, or at its
 * start, waits for the whole block; hwtests/gpu-a0pollbs (v0.377.279's DMA_IO_LONE_TIE) read a lone CHCR2 read issued as a block came due
 * served first. One reads a timer, the other the DMA's own register. This puts each before the same timing read at every N.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. An upload of K (1, 2) blocks of B (16, 4) words into a one-row rectangle;
 * Timer 2 read, CHCR2 stored (the kick), a jump into a nop sled for N nops (0-39), then V: 0 the Timer 2 timing read alone; 1 a CHCR2 read
 * (lw) then the Timer 2 read; 2 a Timer 2 read (lhu, its value dropped) then the Timer 2 read. Four tries at four places of the refresh. A
 * line: L B K V N t0 t1 t2 t3 (t = the timing read minus the first).
 *
 * READ IT LIKE THIS. For each B, K, V: t against N - where the first read is held by the block (t jumps to the block's end) and where it is
 * served ahead of it. V 1 against V 2 is a DMA register's read against a timer's. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-stolast3 --frames=4000 --quiet=1500`.
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
#define WORDS 64

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the kick, N nops (a jump into the sled: its last N), the first read (FIRST), the timing read */
#define VK(V, FIRST) \
static __attribute__((noinline, noclone)) uint32_t v##V(uint32_t n, uint32_t chcr) { \
    uint32_t t0, t1, x; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "la    $t1, 2f\n" \
        "sll   $t2, %3, 2\n" \
        "subu  $t1, $t1, $t2\n" \
        "lhu   %0, 0x1120($t0)\n" \
        "sw    %4, 0x10a8($t0)\n" \
        "jr    $t1\n" \
        "nop\n" \
        ".rept 40\nnop\n.endr\n" \
        "2:\n" \
        FIRST \
        "lhu   %1, 0x1120($t0)\n" \
        "nop\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1), "=&r"(x) \
        : "r"(n), "r"(chcr) \
        : "t0", "t1", "t2", "memory"); \
    (void)x; return (uint16_t)(t1 - t0); \
}
VK(0, "")
VK(1, "lw    %2, 0x10a8($t0)\n")
VK(2, "lhu   %2, 0x1120($t0)\n")
typedef uint32_t (*Vf)(uint32_t, uint32_t);
static const Vf VF[3] = { v0, v1, v2 };

static uint32_t one(uint32_t bs, uint32_t k, uint32_t v, uint32_t n, uint32_t p) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (1u << 16) | (bs * k * 2));
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (k << 16) | bs);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint32_t t = VF[v](n, 0x01000201u);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    return t;
}

int main() {
    printf("\ngpu/gpu-dma-stolast3\n");
    printf("L B K V N t0 t1 t2 t3 (an upload of K blocks of B words: kick, N nops, V 0 nothing / 1 a CHCR2 read / 2 a Timer 2 read, then the timing read of Timer 2; t the cycles from the read before the kick, at four refresh places). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (uint32_t v = 0; v < 3; v++) one(16, 1, v, 10, 0);   /* cache the code (not printed) */
    static const uint32_t BS[2] = { 16, 4 };
    for (int b = 0; b < 2; b++) for (uint32_t k = 1; k <= 2; k++) for (uint32_t v = 0; v < 3; v++) for (uint32_t n = 0; n < 40; n++) {
        uint32_t t[4];
        for (int tr = 0; tr < 4; tr++) t[tr] = one(BS[b], k, v, n, (uint32_t)tr * 64u);
        printf("L %u %u %u %u %u %u %u %u\n", (unsigned)BS[b], (unsigned)k, (unsigned)v, (unsigned)n, (unsigned)t[0], (unsigned)t[1], (unsigned)t[2], (unsigned)t[3]);
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-stolast3: Done.\n");
    for (;;) VSync(0);
}
