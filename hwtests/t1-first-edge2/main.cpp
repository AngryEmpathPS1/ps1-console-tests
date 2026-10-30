/*
 * timer/t1-first-edge2 - WHERE a switch to hblank counting stops taking the line's tick, to the cycle, and whether it
 * follows the display's end (2026-10-27).
 *
 * WHY. hwtests/t1-first-edge on the SCPH-5000: Timer 1 switched from the system clock to hblank at T0 = s takes line 0's
 * tick for s < 1596 and not from 1596 (four passes identical, a 4-cycle grid) - the display's end at T0 ~1615 less the
 * write's own landing. v0.377.202 cuts at the display's end and reads 1600; 8 ticks earlier reads 1592. This sweeps s by
 * ONE cycle across the step, at three dot clocks and two display ends: if the cut is the display's end, the step moves
 * with X2 (A60h: ~323 cycles earlier) and by the dot grid's floor (256 wide: X1 floors to 600, the end to 3160, ~5 cycles
 * earlier), not with the 5 dots to the tick (T1_DOTS: 25 / 40 / 50 ticks at 640 / 320 / 256).
 *
 * WHAT IT DOES. As t1-first-edge (interrupts off, Timer 0 at the system clock in sync mode 1 - reset at the display's start
 * - each probe Timer 1 on the system clock, then ONE store at T0 = s of MODE 0100h, three line starts, Timer 1 read at T0 =
 * 600), for each case s = c - 48 .. c + 32 by 1 (c the display's end in cycles from T0's reset), three passes. Printed per
 * case and pass: the first s with a lower count, and the counts. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-first-edge2 --ntsc --frames=1500`.
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

/* From a fresh Timer 0 reset (T0 >= 200, then < 64), delay to T0 = s exactly (3-cycle loop, its remainder in nops) and
   store Timer 1's mode (gpu-shape-time69's SYNC_HEAD, its tail one store). */
static void writeAt(uint32_t s, uint32_t mode) {
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
        "sh %[m], 0x1114($t0)\n"
        "2:\n"
        ".set reorder\n"
        : : [s] "r"(s), [k] "r"(k), [m] "r"(mode)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
}

/* Timer 0's next reset (it drops below 64 after being 200 or more), every wait bounded. */
static void nextReset() {
    const uint32_t t0 = now();
    while (read16(T0_COUNT) < 200 && now() - t0 < 20000u) {}
    while (read16(T0_COUNT) >= 64 && now() - t0 < 20000u) {}
}

#define PASSES 3
#define SPAN 81
static uint8_t cnt[PASSES][SPAN];
struct Case { const char* name; uint32_t gp1_08; uint32_t x1, x2; uint32_t c; };
static const Case CASES[] = {
    { "320 260h..C60h", 0x08000001u, 0x260u, 0xC60u, 1615 },
    { "320 260h..A60h", 0x08000001u, 0x260u, 0xA60u, 1292 },
    { "256 260h..C60h", 0x08000000u, 0x260u, 0xC60u, 1610 },
    { "640 260h..C60h", 0x08000003u, 0x260u, 0xC60u, 1615 },
};

int main() {
    initVideo(320, 240);
    printf("\ntimer/t1-first-edge2\n");
    printf("Timer 1 switched to hblank at T0 = s, by one cycle across the step, at three dot clocks and two display ends. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000); lastT = read16(T2_COUNT); clk = 0;
    for (unsigned k = 0; k < sizeof(CASES) / sizeof(CASES[0]); k++) {
        const Case& c = CASES[k];
        write32(GP1, c.gp1_08);
        write32(GP1, 0x06000000u | (c.x2 << 12) | c.x1);
        write16(T0_MODE, 0x0003);                            /* system clock, sync mode 1: reset at the hblank gate's end */
        for (int l = 0; l < 2000; l++) nextReset();          /* settle: ~a frame and more */
        const uint32_t s0 = c.c - 48;
        for (int p = 0; p < PASSES; p++)
            for (int i = 0; i < SPAN; i++) {
                write16(T1_MODE, 0x0000);                    /* the system clock between probes */
                writeAt(s0 + i, 0x0100);
                for (int l = 0; l < 3; l++) nextReset();
                { const uint32_t t0 = now(); while (read16(T0_COUNT) < 600 && now() - t0 < 20000u) {} }
                cnt[p][i] = (uint8_t)read16(T1_COUNT);
            }
        write16(T0_MODE, m0);
        for (int p = 0; p < PASSES; p++) {
            int drop = -1;
            for (int i = 1; i < SPAN; i++) if (cnt[p][i] < cnt[p][i - 1]) { drop = (int)(s0 + i); break; }
            printf("%s pass %d: s %d..%d, first s with a lower count %d\n  ", c.name, p + 1, (int)s0, (int)(s0 + SPAN - 1), drop);
            for (int i = 0; i < SPAN; i++) printf("%d", cnt[p][i]);
            printf("\n");
        }
    }
    write32(GP1, 0x08000001u); write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    write16(T0_MODE, m0); write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t1-first-edge2: Done.\n");
    for (;;) VSync(0);
}
