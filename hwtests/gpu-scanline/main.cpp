/*
 * gpu/gpu-scanline - a SHORT draw's cost at every point of an active line: is the scan-out's fetch one burst a line or
 * spread along it (2026-10-13 night).
 *
 * WHY. hwtests/gpu-vbl2 on the SCPH-7501: a draw on an active line is slower than in the vertical blanking by the
 * picture's bytes a line (+12.7% at 640x240 15-bit, +19.6% at 640x480i 24-bit) - measured on a semi 256x32 rectangle,
 * ~6,200-7,500 cycles, three lines or more, so an average. A short draw sees what the fetch does WITHIN a line: one burst
 * (a short draw either untouched or held for the whole burst, by where it starts) or a stream along the line (every
 * short draw a few % slower). A model of the toll for short primitives - most of a game's - needs which.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For 640x240 24-bit (GP1(08h) = 13h) and 320x240 15-bit
 * (01h), range 16..256 and 260h..260h+2560: a flat opaque 64x4 rectangle at (0,16), timed from before its first GP0
 * write to GPUSTAT bits 28+26, after a VBlank wait (I_STAT bit 0) and X idle cycles on Timer 2. In the blanking X =
 * 20,000 + 27k, k = 0..15; on active lines X = 200,000 + 27k, k = 0..79 (2,160 cycles: a line is ~2,152.6). Four tries
 * a point, fastest and slowest, eight points a printed line. The runner's display mode is put back.
 *
 * READ IT LIKE THIS. Flat along the line a few % over the blanking's: a stream. Most points at the blanking's cost and a
 * stretch of them dearer by up to a burst's length: a burst, where in the line and how long. The polling has a jitter
 * of some ten cycles, so a burst's edges are smeared by about that. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scanline --frames=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

static uint32_t probe(uint32_t x) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < x) {} }
    const uint32_t t0 = now();
    write32(GP0, 0x60204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(64, 4));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}
static uint16_t lo[80], hi[80];
static void sweep(uint32_t x0, int n) {
    for (int k = 0; k < n; k++) {
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int r = 0; r < 4; r++) { const uint32_t t = probe(x0 + 27u * (uint32_t)k); a = t < a ? t : a; b = t > b ? t : b; }
        lo[k] = (uint16_t)(a > 65535u ? 65535u : a); hi[k] = (uint16_t)(b > 65535u ? 65535u : b);
    }
}
static void show(uint32_t x0, int n) {
    for (int k = 0; k < n; k += 8) {
        printf("  %6u:", (unsigned)(x0 + 27u * (uint32_t)k));
        for (int j = k; j < k + 8 && j < n; j++) printf(" %u-%u", (unsigned)lo[j], (unsigned)hi[j]);
        printf("\n");
    }
}

int main() {
    printf("\ngpu/gpu-scanline\n");
    printf("A flat 64x4 rectangle after a VBlank wait and X idle cycles: the blanking (X = 20,000 + 27k) and an active line\n");
    printf("(200,000 + 27k), fastest-slowest of four a point, eight a line. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    static const char* const MN[2] = { "640x240 24-bit", "320x240 15-bit" };
    static const uint32_t MODE[2] = { 0x13, 0x01 };
    for (int m = 0; m < 2; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        sweep(20000, 16);
        ExitCriticalSection();
        printf("%s, the blanking\n", MN[m]);
        show(20000, 16);
        EnterCriticalSection();
        sweep(200000, 80);
        ExitCriticalSection();
        printf("%s, active lines\n", MN[m]);
        show(200000, 80);
        printf("\n");
        EnterCriticalSection();
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("gpu/gpu-scanline: Done.\n");
    for (;;) VSync(0);
}
