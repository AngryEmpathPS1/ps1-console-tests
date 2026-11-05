/*
 * gpu/gpu-dma-lasttie - an on-die read that lands as an upload's LAST block comes due: who takes the bus, and what CHCR2's Busy bit says
 * (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. v0.377.279's DMA_IO_LONE_TIE gives a lone on-die read the bus from an upload's block due at its own cycle (gpu-a0pollbs, gpu-
 * burstline), and hwtests/gpu-dma-gapsd2 on both consoles reads it so at the second block of four. hwtests/gpu-dma-stolast3 on the second
 * SCPH-5501 reads the opposite at the LAST block (K 2's read due with the second block: held, 57 against the tie's 36), and a knob that
 * takes the tie from the last block alone fits it (1.15 -> 0.33 cycles RMS) - but moves gpu-a0pollbs's poll loops by a turn at their end,
 * where what the poll reads also depends on when Busy clears behind a read the last block held (v0.377.280). This reads both at once:
 * a read's time and the Busy bit it saw, at every N around the last block's due cycle.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. An upload of K (1, 2, 3) blocks of B (16, 4) words into a one-row
 * rectangle; Timer 2 read, CHCR2 stored (the kick), a jump into a nop sled for N nops (0-63), then V: 1 a CHCR2 read (lw, its Busy bit
 * kept) then the Timer 2 timing read; 3 the Timer 2 timing read, then a CHCR2 read (its Busy bit kept). Four tries at four places of
 * the refresh. A line: L B K V N t0 t1 t2 t3 | b0 b1 b2 b3 (t = the timing read minus the first; b = Busy, CHCR2 bit 24).
 *
 * READ IT LIKE THIS. For each B, K, V: t against N - where the read is held (t jumps to the block's end) and where it goes ahead - and
 * Busy beside it: a CHCR2 read that goes ahead of the last block reads 1; one held by it reads 1 or 0 as Busy clears before or after its
 * sample. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-lasttie --frames=4000 --quiet=1500`.
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
/* the kick, N nops (a jump into the sled: its last N), then FIRST and SECOND (one is the timing read of Timer 2 into %1, the other the
 * CHCR2 read into %2); returns the time in the low 16 bits and Busy in bit 16 */
#define VK(V, FIRST, SECOND) \
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
        ".rept 64\nnop\n.endr\n" \
        "2:\n" \
        FIRST \
        SECOND \
        "nop\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1), "=&r"(x) \
        : "r"(n), "r"(chcr) \
        : "t0", "t1", "t2", "memory"); \
    return (uint32_t)(uint16_t)(t1 - t0) | (((x >> 24) & 1u) << 16); \
}
VK(1, "lw    %2, 0x10a8($t0)\n", "lhu   %1, 0x1120($t0)\n")
VK(3, "lhu   %1, 0x1120($t0)\n", "lw    %2, 0x10a8($t0)\n")
static uint32_t one(uint32_t bs, uint32_t k, uint32_t v, uint32_t n, uint32_t p) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (1u << 16) | (bs * k * 2));
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (k << 16) | bs);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint32_t t = v == 1 ? v1(n, 0x01000201u) : v3(n, 0x01000201u);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    return t;
}
int main() {
    printf("\ngpu/gpu-dma-lasttie\n");
    printf("L B K V N t0 t1 t2 t3 | b0 b1 b2 b3 (an upload of K blocks of B words: kick, N nops, V 1 a CHCR2 read then the timing read of "
           "Timer 2 / 3 the timing read then a CHCR2 read; t from the read before the kick, b CHCR2's Busy, at four refresh places). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one(16, 1, 1, 10, 0); one(16, 1, 3, 10, 0);   /* cache the code (not printed) */
    static const uint32_t BS[2] = { 16, 4 };
    for (int b = 0; b < 2; b++) for (uint32_t k = 1; k <= 3; k++) for (uint32_t v = 1; v <= 3; v += 2) for (uint32_t n = 0; n < 64; n++) {
        uint32_t t[4];
        for (int tr = 0; tr < 4; tr++) t[tr] = one(BS[b], k, v, n, (uint32_t)tr * 64u);
        printf("L %u %u %u %u %u %u %u %u | %u %u %u %u\n", (unsigned)BS[b], (unsigned)k, (unsigned)v, (unsigned)n,
               (unsigned)(t[0] & 0xFFFF), (unsigned)(t[1] & 0xFFFF), (unsigned)(t[2] & 0xFFFF), (unsigned)(t[3] & 0xFFFF),
               (unsigned)(t[0] >> 16), (unsigned)(t[1] >> 16), (unsigned)(t[2] >> 16), (unsigned)(t[3] >> 16));
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-lasttie: Done.\n");
    for (;;) VSync(0);
}
