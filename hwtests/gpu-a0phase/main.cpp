/*
 * gpu/gpu-a0phase - a 4-word-block GPU upload's time by where it starts on the DRAM refresh's 256-cycle grid (2026-11-04, for the
 * SCPH-5501).
 *
 * WHY. On the second SCPH-5501, 2,048 words of 4-word SyncMode 1 blocks into a 1,024-wide rectangle inside vblank take 7,337-7,351
 * cycles in hwtests/gpu-a0blk3 and 7,560-7,576 in hwtests/gpu-a0mode, run after run - the same upload, buffer address (800129A0h),
 * rectangle and poll loop instruction for instruction; only the code before the start differs, so each starts at its own place on the
 * refresh's grid. If a 4-word upload's blocks lock onto the refresh (17.9 blocks a refresh), that place would set every refresh's cost
 * through the transfer: this sweeps it. This emulator reads both programs ~7,340.
 *
 * WHAT IT DOES. Interrupts off, no disc, Timer 2 at the system clock (the refresh's grid: hwtests/runner), Timer 1 counting hblanks.
 * Inside vblank (500 cycles past its first hblank edge, then on to Timer 2 mod 256 = p to the cycle, gpu-dma-gaps' method), 2,048
 * words (i + 1 in both pixels) into GP0(A0h) at (0,256), 1,024 wide: SyncMode 1 blocks of 4, and chopped 4-word windows at 4 cycles;
 * p = 0, 8 ... 248; twice each. The time from CHCR2's store to the poll's done, to 32 bits.
 *
 * READ IT LIKE THIS. Q kind p rep t: the transfer's cycles by start phase. No assertions - compare against your emulator.
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

/* vblank's first hblank edge + 500, then the CHCR2 store at Timer 2 mod 256 = p, to the cycle; the poll; the cycles to done */
static __attribute__((noinline, noclone)) uint32_t go(uint32_t chcr, uint32_t p) {
    write32(I_STAT, ~1u);
    for (int i = 0; i < 2000000 && !(read32(I_STAT) & 1u); i++) {}
    const uint16_t e = read16(T1_COUNT);
    while (read16(T1_COUNT) == e) {}
    waitT2(500);
    for (unsigned k = 0; k < 4000; k++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d >= 6 && d < 56) break; }
    const uint32_t v = read16(T2_COUNT);
    sled((p - v - 6) & 0xFFu);
    uint16_t last = read16(T2_COUNT);
    uint32_t t = 0, polls = 0;
    write32(CHCR2, chcr);
    while ((read32(CHCR2) & 0x01000000u) && polls < 400000) { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); last = n; polls++; }
    { const uint16_t n = read16(T2_COUNT); t += (uint16_t)(n - last); }
    if (polls >= 400000) write32(CHCR2, 0);
    return t;
}

static void one(char kind, uint32_t p, int rep) {
    const int w = 1024, h = WORDS * 2 / w;
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)w);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, kind == 'S' ? ((512u << 16) | 4u) : (uint32_t)WORDS);
    const uint32_t t = go(kind == 'S' ? 0x01000201u : (0x11000101u | (2u << 16) | (2u << 20)), p);
    waitT2(3000);
    for (int i = 0; i < 8192 && !(read32(GP1) & (1u << 26)); i++) { write32(GP0, 0); for (volatile int k = 0; k < 8; k++) {} }
    write32(GP1, 0x04000000u);
    printf("Q %c %u %d %u\n", kind, (unsigned)p, rep, (unsigned)t);
}

int main() {
    printf("\ngpu/gpu-a0phase\n");
    printf("Q kind p rep t (2,048 words, 1,024 wide, in vblank, started at Timer 2 mod 256 = p; S 4-word blocks, P chopped 4/4). No assertions.\n\n");
    for (int i = 0; i < WORDS; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    one('S', 0, 0);   /* cache the code */
    for (int k = 0; k < 2; k++) {
        const char kind = k ? 'P' : 'S';
        for (uint32_t p = 0; p < 256; p += 8) for (int r = 0; r < 2; r++) one(kind, p, r);
    }
    gpuIdle();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    write32(I_STAT, ~1u);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0phase: Done.\n");
    for (;;) VSync(0);
}
