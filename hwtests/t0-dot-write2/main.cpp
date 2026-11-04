/*
 * timer/t0-dot-write2 - t0-dot-write with every probe started at a pseudo-random cycle (0-31 cached nops): its own probes met
 * the dot grid at a few phases only - the NTSC ratio is a fixed 15625:9856 on the console too, and a hold one cycle longer moved
 * this core's means by 0.41 dot where an even spread of phases gives 0.198 (2026-11-04, for the SCPH-5501).
 *
 * WHY (CURRENT "Timers, the first edge after a counter write"). ps1-tests' timer-dump (TCLK0 wired to RTS on its author's console)
 * reads 0 where this core reads 1 on four rows. hwtests/t1-count-edge found that a write to Timer 1's counter after the line's
 * hblank has begun loses that line's tick (v0.377.265); Timer 0's dot clock is the other clock the CPU does not make. If the
 * first dot after a write is lost, or counted late, the counts here sit a dot below this core's at every elapsed time.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 on the system clock as the stopwatch, Timer 0 on the dot clock (MODE 0100h, no sync) at
 * 320 and 256 wide. Each probe: Timer 2 read, Timer 0 written 0, a cached wait of k turns (3 cycles each, k 0..39), Timer 0 read,
 * Timer 2 read; the count summed in a bin by the elapsed cycles (the second Timer 2 read less the first). 64 probes a k, so each
 * bin holds many dot phases (on a two-crystal board the dot clock drifts against the CPU's). Printed per bin with probes: the
 * elapsed cycles, probes, mean count x100. No assertions. `node hwtests/run.js t0-dot-write --ntsc --frames=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP1 0x1F801814u

/* one probe: Timer 2, the write, k cached turns, Timer 0, Timer 2 - in one block of straight-line code so its timing is fixed */
/* n cached nops, 0..31, by a jump into a slide: a delay to the cycle, so the next probe meets the dot grid at any phase */
static __attribute__((noinline, noclone)) void nops(uint32_t n) {
    __asm__ volatile(
        ".set noreorder\n"
        "la $t0, 3f\n"
        "sll $t1, %[n], 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 32\nnop\n.endr\n"
        "3:\n"
        ".set reorder\n"
        : : [n] "r"(n & 31) : "t0", "t1");
}
static uint32_t lfsr = 0xACE1u;
static uint32_t rnd() { lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u); return lfsr; }
static __attribute__((noinline, noclone)) uint32_t probe(uint32_t k, uint32_t* elapsed) {
    uint32_t a, b, c;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "lhu %[a], 0x1120($t0)\n"
        "sh $zero, 0x1100($t0)\n"
        "move $t1, %[k]\n"
        "beqz $t1, 2f\n"
        "nop\n"
        "1:\n"
        "addiu $t1, $t1, -1\n"
        "bnez $t1, 1b\n"
        "nop\n"
        "2:\n"
        "lhu %[c], 0x1100($t0)\n"
        "lhu %[b], 0x1120($t0)\n"
        "nop\n"
        ".set reorder\n"
        : [a] "=&r"(a), [b] "=&r"(b), [c] "=&r"(c) : [k] "r"(k) : "t0", "t1", "memory");
    *elapsed = (b - a) & 0xFFFF;
    return c;
}

#define BINS 160
static uint32_t nBin[BINS], sumBin[BINS];

int main() {
    initVideo(320, 240);
    printf("\ntimer/t0-dot-write2\n");
    printf("Timer 0 on the dot clock, written 0, read k cached turns later; counts binned by Timer 2's elapsed cycles. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    static const struct { const char* name; uint32_t gp1_08; } W[2] = { { "320", 0x08000001u }, { "256", 0x08000000u } };
    for (int w = 0; w < 2; w++) {
        write32(GP1, W[w].gp1_08);
        write16(T0_MODE, 0x0100);                            /* the dot clock, free-running */
        for (int i = 0; i < BINS; i++) nBin[i] = sumBin[i] = 0;
        uint32_t e; probe(3, &e); probe(3, &e);              /* cache the probe */
        for (uint32_t k = 0; k < 40; k++)
            for (int r = 0; r < 64; r++) {
                const uint32_t c = probe(k, &e);
                if (e < BINS) { nBin[e]++; sumBin[e] += c; }
                nops(rnd());   /* the next probe at a pseudo-random cycle: every dot phase alike, on any machine */
            }
        printf("%s wide: elapsed probes mean x100\n", W[w].name);
        for (int i = 0; i < BINS; i++) if (nBin[i]) printf("  %3d %4u %5u\n", i, (unsigned)nBin[i], (unsigned)(sumBin[i] * 100 / nBin[i]));
    }
    write32(GP1, 0x08000001u);
    write16(T0_MODE, m0); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t0-dot-write2: Done.\n");
    for (;;) VSync(0);
}
