/*
 * timer/t1-first-edge - does the first hblank after a write to Timer 1's mode count? Swept to the cycle across a line
 * (2026-10-27).
 *
 * WHY. ps1-tests' timer-dump (its author's console, CPU pin 160 - TCLK0 - cut from the GPU and wired to pin 70, RTS, so
 * software clocks Timer 0 by hand): after a mode write the FIRST edge does not count - the counter reads 0 after it and
 * steps one transition late - where every later edge counts at once; this core counts it (hwtests/tclk-edge, its four
 * differing rows). Either an external clock's first edge after a counter write is swallowed (an edge detector that needs
 * a level seen after the write), or it reaches the counter late. TCLK0 needs the mod; Timer 1's hblank is the same kind of
 * input (a GPU signal into the timer block) on every stock console, so ask it: if its first edge after a write is eaten,
 * the count comes out ONE LOW at every write position; if it is late, the counts are right and only the place where the
 * count steps (the write crossing the tick) moves by the lateness.
 *
 * WHAT IT DOES. Interrupts off, 320x240 (GP1(08h) 01h, range 260h..C60h). Timer 0 at the system clock in sync mode 1
 * (reset at the hblank gate's end - the display's start: src/psx.js T0_GATE, gpu-shape-time69's anchor). Each probe: a
 * fresh Timer 0 reset, an exact delay to T0 = s (a 3-cycle loop, its remainder in nops - gpu-shape-time69's), then ONE
 * store, Timer 1's mode = 0100h (hblank, free run: the write zeroes the counter); then three more Timer 0 resets (three
 * line starts) and Timer 1 read at T0 = R (600: well clear of the tick, which comes after the display's end). s = 64 + 4 i,
 * i = 0..528 (a whole line and a little); four passes. Expected without any eaten edge: 3 ticks (lines 1-3's) plus line
 * 0's when the write is before it.
 *
 * READ IT LIKE THIS. Per pass, the counts at each s (3 or 4 if every edge counts; 2 or 3 if the first is eaten), and the
 * first s from which the count drops (where the write passes line 0's tick). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-first-edge --ntsc --frames=900`.
 * ON THIS CORE AT v0.377.200 (2026-10-27): every pass 3 from s = 64, 2 from s = 1756 (line 0's tick ~1,752 cycles after
 * the display's start) to the line's end, 3 again past it - its first edge after the write counts.
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

#define NS 529
#define PASSES 4
static uint8_t cnt[PASSES][NS];

int main() {
    initVideo(320, 240);
    printf("\ntimer/t1-first-edge\n");
    printf("Timer 1 (hblank) mode written at s cycles after a line's start, read at T0 = 600 three line starts later. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m0 = read16(T0_MODE), m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write32(GP1, 0x08000001u);                               /* 320x240 NTSC */
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    write16(T2_MODE, 0x0000); lastT = read16(T2_COUNT); clk = 0;
    write16(T0_MODE, 0x0003);                                /* system clock, sync mode 1: reset at the hblank gate's end */
    for (int p = 0; p < PASSES; p++)
        for (int i = 0; i < NS; i++) {
            write16(T1_MODE, 0x0000);                        /* the system clock between probes */
            writeAt(64 + 4 * i, 0x0100);
            for (int l = 0; l < 3; l++) nextReset();
            { const uint32_t t0 = now(); while (read16(T0_COUNT) < 600 && now() - t0 < 20000u) {} }
            cnt[p][i] = (uint8_t)read16(T1_COUNT);
        }
    write16(T0_MODE, m0); write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int p = 0; p < PASSES; p++) {
        int drop = -1;
        for (int i = 1; i < NS; i++) if (cnt[p][i] < cnt[p][i - 1]) { drop = 64 + 4 * i; break; }
        printf("pass %d: count at s=64 %d, at s=%d %d; first s with a lower count %d\n", p + 1, cnt[p][0], 64 + 4 * (NS - 1), cnt[p][NS - 1], drop);
        printf("  ");
        for (int i = 0; i < NS; i++) printf("%d", cnt[p][i]);
        printf("\n");
    }
    printf("\ntimer/t1-first-edge: Done.\n");
    for (;;) VSync(0);
}
