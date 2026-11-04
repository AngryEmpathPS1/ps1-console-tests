/*
 * timer/t1-count-edge - is the first counting edge after a COUNTER write counted? Timer 1 on hblank, its counter written at
 * T0 = s, by one cycle across the line's tick (2026-11-04, for the SCPH-5501).
 *
 * WHY (CURRENT "Timers, the first edge after a counter write"). ps1-tests' timer-dump, on its author's console with TCLK0 wired to
 * RTS, reads 0 where this core reads 1 on four rows: a count written just before an external clock's edge either misses that
 * edge on the console, or the edge comes later than here. A stock console has no external clock on Timer 0, but Timer 1's hblank
 * is one the CPU does not make. hwtests/t1-first-edge2 found where a MODE write stops taking the line's tick (the display's end
 * less the write's ~13 cycles to land); this asks the same of a write to the COUNTER: if the edge after it always counts, the cut
 * sits where t1-first-edge2's does; if the edge right after a counter write is lost, the count reads 0 for some s before it.
 *
 * WHAT IT DOES. Interrupts off. Timer 0 on the system clock in sync mode 1 (reset at the display's start: T0 is cycles into the
 * line), Timer 1 on hblank, free-running. Each probe: ONE store of 0 to Timer 1's counter at T0 = s (gpu-shape-time69's exact
 * start, as t1-first-edge2), then the next line's start, then T0 = 600 (before that line's tick), and Timer 1 read. For two
 * display widths (320: the tick ~1615, 256: ~1610 cycles from T0's reset), s = c - 64 .. c + 48 by 1, three passes. Printed per
 * case and pass: the last s that read 1, and the counts (1: the edge after the write counted; 0: it did not, or came before).
 * No assertions. THE RULE: run it here first: `node hwtests/run.js t1-count-edge --ntsc --frames=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* From a fresh Timer 0 reset (T0 >= 200, then < 64), delay to T0 = s exactly (3-cycle loop, its remainder in nops) and store
   Timer 1's COUNTER (t1-first-edge2's writeAt, its store moved from 1114h to 1110h). */
static void countAt(uint32_t s, uint32_t value) {
    const uint32_t k = 0;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "la $t1, 2f\n"
        "sll $t2, %[k], 2\n"
        "subu $t1, $t1, $t2\n"
        "5:\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "sltiu $t5, $t3, 200\n"
        "bnez $t5, 5b\n"
        "nop\n"
        "6:\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "sltiu $t5, $t3, 64\n"
        "beqz $t5, 6b\n"
        "nop\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "subu $t4, %[s], $t3\n"
        "7:\n"
        "addiu $t4, $t4, -3\n"
        "bgtz $t4, 7b\n"
        "nop\n"
        "la $t6, 8f\n"
        "sll $t5, $t4, 2\n"
        "addiu $t6, $t6, -8\n"
        "subu $t6, $t6, $t5\n"
        "jr $t6\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "8:\n"
        "sh %[m], 0x1110($t0)\n"
        "2:\n"
        ".set reorder\n"
        : : [s] "r"(s), [k] "r"(k), [m] "r"(value)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
}

/* Timer 0's next reset (it drops below 64 after being 200 or more), every wait bounded. */
static void nextReset() {
    const uint32_t t0 = now();
    while (read16(T0_COUNT) < 200 && now() - t0 < 20000u) {}
    while (read16(T0_COUNT) >= 64 && now() - t0 < 20000u) {}
}

#define PASSES 3
#define SPAN 113
static uint8_t cnt[PASSES][SPAN];
struct Case { const char* name; uint32_t gp1_08; uint32_t x1, x2; uint32_t c; };
static const Case CASES[] = {
    { "320 260h..C60h", 0x08000001u, 0x260u, 0xC60u, 1615 },
    { "256 260h..C60h", 0x08000000u, 0x260u, 0xC60u, 1610 },
};

int main() {
    initVideo(320, 240);
    printf("\ntimer/t1-count-edge\n");
    printf("Timer 1 on hblank, its counter written 0 at T0 = s, by one cycle across the line's tick, at two dot clocks. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000); lastT = read16(T2_COUNT); clk = 0;
    for (unsigned k = 0; k < sizeof(CASES) / sizeof(CASES[0]); k++) {
        const Case& c = CASES[k];
        write32(GP1, c.gp1_08);
        write32(GP1, 0x06000000u | (c.x2 << 12) | c.x1);
        write16(T0_MODE, 0x0003);                            /* system clock, sync mode 1: reset at the hblank gate's end */
        write16(T1_MODE, 0x0100);                            /* hblank, free-running */
        for (int l = 0; l < 2000; l++) nextReset();          /* settle: ~a frame and more */
        const uint32_t s0 = c.c - 64;
        for (int p = 0; p < PASSES; p++)
            for (int i = 0; i < SPAN; i++) {
                countAt(s0 + i, 0);
                nextReset();
                { const uint32_t t0 = now(); while (read16(T0_COUNT) < 600 && now() - t0 < 20000u) {} }
                cnt[p][i] = (uint8_t)read16(T1_COUNT);
            }
        for (int p = 0; p < PASSES; p++) {
            int last = -1;
            for (int i = 0; i < SPAN; i++) if (cnt[p][i] == 1) last = (int)(s0 + i);
            printf("%s pass %d: s %d..%d, last s that read 1: %d\n  ", c.name, p + 1, (int)s0, (int)(s0 + SPAN - 1), last);
            for (int i = 0; i < SPAN; i++) printf("%d", cnt[p][i] > 9 ? 9 : cnt[p][i]);
            printf("\n");
        }
    }
    write32(GP1, 0x08000001u); write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    write16(T0_MODE, m0); write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t1-count-edge: Done.\n");
    for (;;) VSync(0);
}
