/*
 * cpu/ram-store-io8 - an on-die register read right behind a store to that register, the store queued behind RAM stores: does the
 * read return the stored value at every refresh phase? (2026-11-03, for the SCPH-5501.)
 *
 * WHY. mdec-reset21 on the SCPH-5501 (two runs byte-identical): in one 8-cycle window of the refresh's phase, one()'s poll sees DMA0
 * done ~40 cycles after the start where it otherwise takes 76 - as if its CHCR0 read went ahead of CHCR0's own start store, held
 * behind the clock's two RAM stores by the refresh, and read the old 0. This core's read priority (v0.377.113, ram-store-io4-7)
 * lets an on-die read go ahead of queued stores at a transaction's boundary but holds it behind a queued store to its own 64 bytes,
 * so it never can. Which is the console's?
 *
 * WHAT IT DOES. DMA channel 6's MADR (1F8010E0, never started here) as the register. Per try: MADR6 = 0 and 100 cycles' rest; then
 * two RAM stores to a word pair (a fresh 1 KB block each try, so each is a new DRAM transaction), MADR6 = the try's own value, and at
 * once a read of MADR6; then the read of a second register's store the same way (Timer 1's target, 1F801118, 16 bits) to see if the
 * register's kind matters. 4,096 tries, each started 0-31 single cycles later than the last (a jump into a run of nops), interrupts
 * off. Per 8-cycle bin of Timer 2's phase just before the two RAM stores: tries, reads of the new value, reads of the old.
 *
 * READ IT LIKE THIS. Every bin all new: an on-die read never passes a store to its own register. Old values in some bins: it does,
 * when the refresh holds the queue there. No assertions.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define T1_TARGET 0x1F801118u
#define MADR6    0x1F8010E0u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* n & 31 more cycles, one at a time: a jump into a run of 32 nops, n from its end (cached code, 1 cycle each). */
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 32\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n & 31u) : "t0", "t1");
}

static uint32_t buf[16 * 256] __attribute__((aligned(1024)));   /* 16 KB: a fresh 1 KB block for each try, 16 in turn */

/* the two RAM stores, the register's store and its read, back to back: phase is Timer 2 just before */
static __attribute__((noinline, noclone)) uint32_t tryDma(uint32_t *p, uint32_t v, uint32_t *phase) {
    uint32_t r;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lhu  %1, 0(%3)\n"          /* Timer 2 */
        "nop\n"
        "sw   %4, 0(%2)\n"          /* RAM */
        "sw   %4, 4(%2)\n"          /* RAM, the same block */
        "sw   %4, 0(%5)\n"          /* MADR6 = v */
        "lw   %0, 0(%5)\n"          /* and read it */
        "nop\n"
        ".set pop\n"
        : "=&r"(r), "=&r"(*phase) : "r"(p), "r"(T2_COUNT), "r"(v), "r"(MADR6) : "memory");
    return r;
}
static __attribute__((noinline, noclone)) uint32_t tryTimer(uint32_t *p, uint32_t v, uint32_t *phase) {
    uint32_t r;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lhu  %1, 0(%3)\n"
        "nop\n"
        "sw   %4, 0(%2)\n"
        "sw   %4, 4(%2)\n"
        "sh   %4, 0(%5)\n"          /* T1 target = v */
        "lhu  %0, 0(%5)\n"
        "nop\n"
        ".set pop\n"
        : "=&r"(r), "=&r"(*phase) : "r"(p), "r"(T2_COUNT), "r"(v), "r"(T1_TARGET) : "memory");
    return r;
}

static uint16_t tries[2][32], fresh[2][32], stale[2][32], other[2][32];

int main() {
    initVideo(320, 240);
    printf("\ncpu/ram-store-io8\n");
    printf("An on-die register read right behind its own store, queued behind two RAM stores, by Timer 2's phase /8. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint16_t t1t = read16(T1_TARGET);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    uint32_t ph;
    tryDma(buf, 0, &ph); tryTimer(buf, 0, &ph);   /* cache them */
    for (int i = 0; i < 4096; i++) {
        uint32_t *p = buf + (i & 15) * 256;
        const int k = i & 1;   /* alternate the two registers */
        if (k == 0) write32(MADR6, 0); else write16(T1_TARGET, 0);
        waitCycles(100 + (uint32_t)(i * 37 % 256));
        sled((uint32_t)i * 11u);
        const uint32_t v = k == 0 ? (0x00010000u + (uint32_t)i * 4u) & 0x00FFFFFCu : (uint32_t)(0x1000 + i) & 0xFFFFu;
        const uint32_t r = k == 0 ? tryDma(p, v, &ph) : tryTimer(p, v, &ph);
        const int b = (int)((ph & 0xFF) / 8);
        tries[k][b]++;
        if (r == v) fresh[k][b]++; else if (r == 0) stale[k][b]++; else other[k][b]++;
    }
    write16(T1_TARGET, t1t);
    write32(MADR6, 0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("phase    MADR6: tries  new  old other    T1 target: tries  new  old other\n");
    for (int b = 0; b < 32; b++)
        printf("%3d-%3d         %4d %4d %4d %4d               %4d %4d %4d %4d\n", b * 8, b * 8 + 7,
               (int)tries[0][b], (int)fresh[0][b], (int)stale[0][b], (int)other[0][b],
               (int)tries[1][b], (int)fresh[1][b], (int)stale[1][b], (int)other[1][b]);
    printf("\ncpu/ram-store-io8: Done.\n");
    for (;;) {}   /* park after Done. */
}
