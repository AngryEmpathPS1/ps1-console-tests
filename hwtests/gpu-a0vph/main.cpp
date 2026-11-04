/*
 * gpu/gpu-a0vph - a 4-word-block GPU upload's time by where it starts against the VIDEO: 500 + j single cycles after an hblank edge in
 * vblank (2026-11-04, for the SCPH-5501).
 *
 * WHY. hwtests/gpu-a0phase on the second SCPH-5501 (each start on the DRAM refresh's grid, 32 phases): 4-word blocks AND chopped 4/4
 * take 7,312-7,331 cycles at every phase, this emulator 7,315-7,356 - where gpu-a0blk3 and gpu-a0mode, which start 500 cycles after an
 * hblank edge, read chopped 4/4 7,567-7,612 and gpu-a0mode its blocks 7,560-7,576 (gpu-a0blk3's 7,337-7,351). Not the refresh, then;
 * the video's phase, which an hblank-synced start fixes, may be (the GPU runs on its own crystal).
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock, Timer 1 counting hblanks. Inside vblank: its first hblank edge,
 * then 500 + j single cycles (cached nops, j = 0..31) before CHCR2's store - 2,048 words (i + 1 in both pixels) into GP0(A0h) at
 * (0,256), 1,024 wide, by SyncMode 1 blocks of 4 and by chopped 4-word windows at 4 cycles; once each. The time to 32 bits.
 *
 * READ IT LIKE THIS. V kind j t: the transfer's cycles by the start's cycle after the hblank edge. No assertions - compare against
 * your emulator.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT 0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define WORDS 2048

static uint32_t buf[WORDS] __attribute__((aligned(16)));
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

#define NMAX 192
/* n more cycles, one at a time: a jump into a run of 192 nops, n from its end (cached code, a cycle each) - gpu-dma-gaps' */
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 192\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n < NMAX ? n : NMAX - 1) : "t0", "t1");
}

/* vblank's first hblank edge, 500 + j cycles of cached nops, CHCR2's store; the poll; the cycles to done */
static __attribute__((noinline, noclone)) uint32_t go(uint32_t chcr, uint32_t j) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    sled(150); sled(150); sled(150); sled(50 + j);
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    return t;
}

static void one(char kind, uint32_t j) {
    const int w = 1024, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? ((512u << 16) | 4u) : (uint32_t)WORDS);
    const uint32_t t = go(kind == 'S' ? 0x01000201u : (0x11000101u | (2u << 16) | (2u << 20)), j);
    waitT2(3000);
    for (int i = 0; i < 8192 && !(read32(GP1) & (1u << 26)); i++) { write32(GP0, 0); for (volatile int k = 0; k < 8; k++) {} }
    write32(GP1, 0x04000000u);
    printf("V %c %u %u\n", kind, (unsigned)j, (unsigned)t);
}

int main() {
    printf("\ngpu/gpu-a0vph\n");
    printf("V kind j t (2,048 words, 1,024 wide, in vblank, started 500 + j cycles after an hblank edge; S 4-word blocks, P chopped 4/4). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one('S', 0);   /* cache the code */
    for (int k = 0; k < 2; k++) for (uint32_t j = 0; j < 32; j++) one(k ? 'P' : 'S', j);
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0vph: Done.\n");
    for (;;) VSync(0);
}
