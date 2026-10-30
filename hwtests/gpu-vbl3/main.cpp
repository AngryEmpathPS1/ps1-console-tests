/*
 * gpu/gpu-vbl3 - the scan-out's toll by the display RANGE: vertical (which lines fetch) and horizontal (how much of a
 * line) (2026-10-13 night).
 *
 * WHY. hwtests/gpu-vbl / gpu-vbl2 on the SCPH-7501: a draw on an active line is slower than in the vertical blanking by
 * the picture's bytes a line (+7.3% at 256x240 ... +19.6% at 640x480i 24-bit), and GP1(03h) (display off) does not
 * remove it. psx-spx says the same (reference/docs/psx-spx.txt, "Vblank vs Display Area": "the exact overload depends
 * on the size of the Display Area and on the horizontal resolution"; Y2 = Y1+1 keeps the GPU "(almost) always in
 * Vblank") with "XXX Todo: add some examples". A model needs to know which lines fetch (the GP1(07h) range, as this
 * core's VBlank latch follows it - M194) and whether a narrower GP1(06h) range fetches less.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, 640x240 24-bit (GP1(08h) = 13h - a mode gpu-vbl2 did not
 * have: is it 640x480i 24-bit's +19.6%, the bytes a line, as 640x240 15-bit was 640x480i 15-bit's?). The probe: a semi
 * 256x32 rectangle at (0,16) timed to GPUSTAT bits 28+26, after a VBlank wait (I_STAT bit 0) and X idle cycles.
 *  A. vertical range Y1..Y2 = 16..256, 16..136, 136..256, 16..17 (horizontal 260h..260h+2560): X = 0, 20,000 ... 540,000,
 *     the fastest of two a cell.
 *  B. horizontal range, vertical 16..256, X = 20,000 (blanking) and 200,000 (active): X1..X2 = 260h + 2560 / 1920 /
 *     1280 / 640 / 320 / 16, and 500h + 1280 (the same width elsewhere); the fastest of four, the toll in tenths of a %.
 * The runner's display mode and ranges are put back.
 *
 * READ IT LIKE THIS. A: where each range's probes turn dear against the frame, and whether 16..17 is cheap everywhere.
 * B: the toll against the range's width. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-vbl3 --frames=3000`.
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
    write32(GP0, 0x62204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}
static uint32_t best(uint32_t x, int n) { uint32_t b = 0xFFFFFFFFu; for (int r = 0; r < n; r++) { const uint32_t t = probe(x); b = t < b ? t : b; } return b; }
static void hrange(uint32_t x1, uint32_t w) { write32(GP1, 0x06000000u | ((x1 + w) << 12) | x1); }
static void vrange(uint32_t y1, uint32_t y2) { write32(GP1, 0x07000000u | (y2 << 10) | y1); }

int main() {
    printf("\ngpu/gpu-vbl3\n");
    printf("640x240 24-bit. A: by vertical range, a VBlank wait, X idle cycles, a semi 256x32 probe (fastest of two).\n");
    printf("B: by horizontal range, the blanking (X 20,000) against active lines (200,000), fastest of four. No assertions.\n\n");
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
    write32(GP1, 0x08000013u); write32(GP1, 0x03000000u);
    write32(GP1, 0x05000000u);
    hrange(0x260u, 2560u);
    static const uint32_t Y1[4] = { 16, 16, 136, 16 }, Y2[4] = { 256, 136, 256, 17 };
    ExitCriticalSection();
    printf("A        X   16..256  16..136  136..256  16..17\n");
    EnterCriticalSection();
    for (uint32_t x = 0; x <= 540000; x += 20000) {
        uint32_t c[4];
        for (int v = 0; v < 4; v++) { vrange(Y1[v], Y2[v]); c[v] = best(x, 2); }
        ExitCriticalSection();
        printf("  %7u   %6u   %6u   %6u   %6u\n", (unsigned)x, (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3]);
        EnterCriticalSection();
    }
    vrange(16, 256);
    static const uint32_t HX[7] = { 0x260, 0x260, 0x260, 0x260, 0x260, 0x260, 0x500 }, HW[7] = { 2560, 1920, 1280, 640, 320, 16, 1280 };
    ExitCriticalSection();
    printf("\nB  X1..X2           blank   active  toll\n");
    EnterCriticalSection();
    for (int h = 0; h < 7; h++) {
        hrange(HX[h], HW[h]);
        const uint32_t b = best(20000, 4), a = best(200000, 4);
        ExitCriticalSection();
        printf("  %03x+%-4u        %6u   %6u  %d\n", (unsigned)HX[h], (unsigned)HW[h], (unsigned)b, (unsigned)a, b ? ((int)a - (int)b) * 1000 / (int)b : 0);
        EnterCriticalSection();
    }
    hrange(0x260u, 2560u);
    vrange(16, 256);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-vbl3: Done.\n");
    for (;;) VSync(0);
}
