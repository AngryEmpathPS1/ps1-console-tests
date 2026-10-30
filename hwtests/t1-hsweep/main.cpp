/*
 * timer/t1-hsweep - Timer 1's hblank tick against the display's horizontal range, GP1(06h) X1 and X2 swept
 * (2026-10-08, after t1-hres).
 *
 * WHY. t1-hres on the SCPH-5501: the tick sits at the same place in the line at every width with the BIOS's range
 * 260h..C60h (~2,015 cycles from the polled VBlank IRQ; this core ~2,025), and ~51-60 cycles earlier with 200h..C00h -
 * the tick FOLLOWS GP1(06h), as Timer 0's hblank gate does (T0_GATE); this core ticks Timer 1 200 GPU ticks before every
 * line's end whatever the range (T1_RETRACE). Which end of the range, and at what offset?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting hblanks, 320 wide NTSC 240 lines, display
 * on. X2 swept C60h, C40h, C00h, BC0h, B60h, A60h with X1 = 260h; then X1 swept 200h, 2C0h, 360h with X2 = C60h. For each
 * range, 4 frames to settle, then 24 frames: the VBlank edge polled, Timer 1 read with Timer 2 for 14 changes; changes
 * 2..13 taken mod a line (2,152.5 cycles), the median of the reads that saw them. The range is put back to 260h..C60h.
 *
 * READ IT LIKE THIS. Per row the tick's place in the line from the IRQ (cycles) - a slope of 0.63 cycles per GPU tick of
 * X2 (two crystals; 0.631 at 65/41) says the tick is X2 plus a constant, flat across X1 says the start does not matter.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-hsweep --ntsc --frames=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define GP1      0x1F801814u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static bool edge(uint32_t* at) {
    write32(I_STAT, ~1u);
    const uint32_t w0 = now();
    while (!(read32(I_STAT) & 1)) if (now() - w0 > 1200000u) return false;
    *at = now();
    write32(I_STAT, ~1u);
    return true;
}
static void isort(int32_t* v, int n) { for (int i = 1; i < n; i++) { const int32_t x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; } }

#define FRAMES 24
static int32_t place[FRAMES * 12];

static void measure(uint32_t x1, uint32_t x2) {
    write32(GP1, 0x06000000u | (x2 << 12) | x1);
    uint32_t irq;
    for (int i = 0; i < 4; i++) edge(&irq);
    int n = 0;
    for (int f = 0; f < FRAMES; f++) {
        if (!edge(&irq)) continue;
        uint16_t prev = read16(T1_COUNT);
        for (int k = 0; k < 14;) {
            const uint16_t v = read16(T1_COUNT);
            const uint32_t t = now();
            if (v != prev) {
                if (k >= 2) { const int32_t d = (int32_t)(t - irq); place[n++] = ((d * 2) % 4305 + 4305) % 4305 / 2; }
                k++; prev = v;
            }
            if (t - irq > 40000u) break;
        }
    }
    isort(place, n);
    printf("  X1 %03xh X2 %03xh   tick at %4d (median of %d; quartiles %d..%d)\n", (unsigned)x1, (unsigned)x2,
           n ? (int)place[n / 2] : -1, n, n ? (int)place[n / 4] : -1, n ? (int)place[3 * n / 4] : -1);
}

int main() {
    printf("\ntimer/t1-hsweep\n");
    printf("Timer 1's hblank tick, cycles into the line (mod 2,152.5) from the polled VBlank IRQ, by GP1(06h). No assertions.\n\n");
    const uint32_t st0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0100);
    write32(GP1, 0x03000000u);
    write32(GP1, 0x08000001u);                       /* 320 wide, NTSC, 240 lines */
    static const uint32_t X2S[6] = { 0xC60, 0xC40, 0xC00, 0xBC0, 0xB60, 0xA60 };
    for (int i = 0; i < 6; i++) measure(0x260, X2S[i]);
    static const uint32_t X1S[3] = { 0x200, 0x2C0, 0x360 };
    for (int i = 0; i < 3; i++) measure(X1S[i], 0xC60);
    const uint32_t gp08 = ((st0 >> 17) & 3) | (((st0 >> 19) & 1) << 2) | (((st0 >> 20) & 1) << 3) | (((st0 >> 21) & 1) << 4) |
                          (((st0 >> 22) & 1) << 5) | (((st0 >> 16) & 1) << 6) | (((st0 >> 14) & 1) << 7);
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    write32(GP1, 0x08000000u | gp08);
    write32(GP1, 0x03000000u | ((st0 >> 23) & 1));
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t1-hsweep: Done.\n");
    for (;;) VSync(0);
}
