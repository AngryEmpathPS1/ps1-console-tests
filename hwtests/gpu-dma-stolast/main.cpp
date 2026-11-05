/*
 * gpu/gpu-dma-stolast - S back-to-back RAM stores N cycles after a short SyncMode 1 upload's kick, then a Timer 2 read: how many of the
 * stores the gap after the upload's LAST block drains, and when the bus is the CPU's again (2026-11-05, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-storun and gpu-dma-storow on the second SCPH-5501 fitted one rule for a gap's stores (STATUS "gpu-dma-storow"):
 * the CPU's next access goes before the DMA's next block iff the bus interface committed to it before the block's ask - so two stores
 * a gap at the usual ask, three after a row-start block. After the LAST block there is no ask, and the rule says the whole write buffer
 * drains as one burst, where this emulator before v0.377.281 drained two and stalled the CPU for the third: 3 and 4 stores under the
 * last block end ~4 cycles sooner by the rule. Nothing measured that yet.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. An upload of K blocks (K 1, 2) of B words (16, 4) by GP0(A0h) into a
 * 256-wide rectangle; Timer 2 read, CHCR2 stored (the kick), a jump into a nop sled to run N nops (N 0..38), S stores to one RAM word
 * (S 0..4, back to back), then one Timer 2 read - an on-die read: it waits for a block in progress and behind the buffered stores that
 * drain first. Four tries at four places of the DRAM refresh. A line: L B K S N t0 t1 t2 t3 (t = the second read minus the first).
 *
 * READ IT LIKE THIS. For each B, K, S: t against N. Where the stores come under the last block, t(S) - t(0) is what the stores cost
 * the bus after it: two words of one burst 2 cycles past the gap's 6, each more 2 if the whole buffer goes, more if it waits. No
 * assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-stolast --frames=3000 --quiet=1500`.
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
static volatile uint32_t cell[64] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the kick, N nops (a jump into the sled: its last N), S stores, a Timer 2 read */
#define STS(S) \
static __attribute__((noinline, noclone)) uint32_t sts##S(uint32_t n, uint32_t chcr) { \
    uint32_t t0, t1; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "la    $t1, 2f\n" \
        "sll   $t2, %2, 2\n" \
        "subu  $t1, $t1, $t2\n" \
        "move  $t4, %2\n" \
        "lhu   %0, 0x1120($t0)\n" \
        "sw    %3, 0x10a8($t0)\n" \
        "jr    $t1\n" \
        "nop\n" \
        ".rept 40\nnop\n.endr\n" \
        "2:\n" \
        ".rept " #S "\nsw $t4, 0(%4)\n.endr\n" \
        "lhu   %1, 0x1120($t0)\n" \
        "nop\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1) \
        : "r"(n), "r"(chcr), "r"(cell) \
        : "t0", "t1", "t2", "t4", "memory"); \
    return (uint16_t)(t1 - t0); \
}
STS(0) STS(1) STS(2) STS(3) STS(4)
typedef uint32_t (*Sts)(uint32_t, uint32_t);
static const Sts STF[5] = { sts0, sts1, sts2, sts3, sts4 };

static uint32_t one(uint32_t bs, uint32_t k, uint32_t s, uint32_t n, uint32_t p) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, (1u << 16) | (bs * k * 2));
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (k << 16) | bs);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    const uint32_t t = STF[s](n, 0x01000201u);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    return t;
}

int main() {
    printf("\ngpu/gpu-dma-stolast\n");
    printf("L B K S N t0 t1 t2 t3 (an upload of K blocks of B words: kick, N nops, S stores to one RAM word, a Timer 2 read; t the cycles from the read before the kick, at four refresh places). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (uint32_t s = 0; s < 5; s++) one(16, 1, s, 10, 0);   /* cache the code (not printed) */
    static const uint32_t BS[2] = { 16, 4 };
    for (int b = 0; b < 2; b++) for (uint32_t k = 1; k <= 2; k++) for (uint32_t s = 0; s < 5; s++) for (uint32_t n = 0; n < 40; n += 2) {
        uint32_t t[4];
        for (int tr = 0; tr < 4; tr++) t[tr] = one(BS[b], k, s, n, (uint32_t)tr * 64u);
        printf("L %u %u %u %u %u %u %u %u\n", (unsigned)BS[b], (unsigned)k, (unsigned)s, (unsigned)n, (unsigned)t[0], (unsigned)t[1], (unsigned)t[2], (unsigned)t[3]);
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-stolast: Done.\n");
    for (;;) VSync(0);
}
