/*
 * timer/t1-hres - Timer 1's hblank tick against the VBlank IRQ at every horizontal resolution (2026-10-08, after
 * t1-ticks).
 *
 * WHY. Under the test runner the SCPH-5501's Timer 1 hblank tick came ~80 cycles earlier against the IRQ than this
 * core's (t1-edges, t1-ticks; runner v5's vernier 80.7, the 7501 and 9001 94), yet gpustat31 - sideloaded through
 * Unirom on console day - found it where this core puts it against GPUSTAT bit 31 (5.8% on all three consoles; a core
 * with the tick 128 GPU ticks earlier reads 8.8%). The runner keeps the display mode it finds: GPUSTAT 14042000h on
 * the console (512 wide, display on) against 94802000h on the virtual console (256 wide, off). This core places the
 * tick 200 GPU ticks before every line's end whatever the mode (T1_RETRACE); DuckStation ties hblank to the display's
 * horizontal end, mednafen to a 200-clock retrace. Does the tick follow the dot clock or the display's range?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting hblanks (MODE 0100h). For each display
 * mode - GP1(08h) NTSC 240 lines at 256, 320, 368, 512 and 640 wide, display ON, then 320 wide with the display OFF
 * (GP1(03h)), then 320 with the horizontal range GP1(06h) moved 260h..C60h -> 200h..C00h - wait 4 frames, then 16
 * frames: the VBlank edge polled, Timer 1 read with Timer 2 for 12 changes, each change's read-after dated from the
 * IRQ. Printed per mode: the median over the frames of changes 2..11 taken mod a line (2,152.5 cycles at NTSC) - the
 * tick's place in the line against the IRQ - and GPUSTAT. The display mode is put back as found.
 *
 * READ IT LIKE THIS. The same place at every mode: the tick is the line's, and what moved under the runner is
 * elsewhere. Places that move with the width (the dot clock) or with GP1(06h) name the mechanism; this core reads one
 * place for all.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-hres --ntsc --frames=1500`.
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

#define FRAMES 16
static int32_t place[FRAMES * 10];

static void measure(const char* name) {
    uint32_t irq;
    for (int i = 0; i < 4; i++) edge(&irq);
    int n = 0;
    for (int f = 0; f < FRAMES; f++) {
        if (!edge(&irq)) continue;
        uint16_t prev = read16(T1_COUNT);
        for (int k = 0; k < 12;) {
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
    printf("  %-34s GPUSTAT %08x  tick at %4d (median, %d ticks; quartiles %d..%d) cycles into the line from the IRQ\n",
           name, (unsigned)read32(GP1), n ? (int)place[n / 2] : -1, n, n ? (int)place[n / 4] : -1, n ? (int)place[3 * n / 4] : -1);
}

int main() {
    printf("\ntimer/t1-hres\n");
    printf("Timer 1's hblank tick, cycles into the line (mod 2,152.5) from the polled VBlank IRQ, by display mode. No assertions.\n\n");
    const uint32_t st0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0100);
    printf("  found: GPUSTAT %08x\n", (unsigned)st0);
    write32(GP1, 0x03000000u);                                  /* display on */
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);       /* the BIOS's horizontal range */
    static const struct { uint32_t gp08; const char* name; } M[5] = {
        { 0x00, "256 wide, display on" }, { 0x01, "320 wide, display on" }, { 0x40, "368 wide, display on" },
        { 0x02, "512 wide, display on" }, { 0x03, "640 wide, display on" } };
    for (int i = 0; i < 5; i++) { write32(GP1, 0x08000000u | M[i].gp08); measure(M[i].name); }
    write32(GP1, 0x08000001u); write32(GP1, 0x03000001u); measure("320 wide, display OFF");
    write32(GP1, 0x03000000u); write32(GP1, 0x06000000u | (0xC00u << 12) | 0x200u); measure("320 wide, range 200h..C00h");
    /* put back the mode found: GP1(08h) from GPUSTAT's bits, display enable, the BIOS's range */
    const uint32_t gp08 = ((st0 >> 17) & 3) | (((st0 >> 19) & 1) << 2) | (((st0 >> 20) & 1) << 3) | (((st0 >> 21) & 1) << 4) |
                          (((st0 >> 22) & 1) << 5) | (((st0 >> 16) & 1) << 6) | (((st0 >> 14) & 1) << 7);
    write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u);
    write32(GP1, 0x08000000u | gp08);
    write32(GP1, 0x03000000u | ((st0 >> 23) & 1));
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ntimer/t1-hres: Done.\n");
    for (;;) VSync(0);
}
