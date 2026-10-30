/*
 * gpu/gpu-height - a wide rectangle's and a copy's cost by HEIGHT, at two places (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a 256-wide rectangle costs less a row when it is short: hwtests/gpu-scanout2's 256x32 flat 4,284
 * cycles and semi 6,272, gpu-cost-sweep's 256x256 35,654 and 54,493 - 133.9 / 196.0 a row against 139.3 / 212.9, where a
 * row's cost and a set-up give a POSITIVE set-up for every narrower shape gpu-thin times (64 wide: 128 rows 4,753, 256
 * rows 9,449 - 36.7 a row, set-up +57). This core fits the tall ones (it reads 6,825 for the semi 256x32 the console
 * does in 6,272) and gpu-scanout2's short ones are 2.5-11% over (copies 128x32 5,888 against 5,297); the per-scanline
 * VRAM pause (41 cycles in ~2,150) accounts for part. A curve in height says where the row cost changes, or whether
 * the short shapes end early (a write queue still draining when bit 26 reads ready).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; each primitive written to GP0 by the CPU and GPUSTAT
 * polled for bit 26 (gpu-thin's method), fastest..slowest of EIGHT tries: flat (60h) and semi (62h) rectangles 256
 * wide and 128 wide, heights 1 2 4 8 16 32 64 128 256, at Y 0 and Y 16 (X 0); copies (80h) 128 wide from (512,0) to
 * (0,Y) at the same heights. A row prints width, height, place, fastest..slowest.
 *
 * READ IT LIKE THIS. Each column's fastest against its height: a straight line is a set-up and a row; a bend says where
 * the row changes. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-height`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}
static uint32_t drawOnce(const uint32_t* w, int n) {
    waitIdle();
    const uint32_t t0 = now();
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if ((s & 0x14000000u) == 0x14000000u) return t;
        if (t > 40000000u) return 0xFFFFFFFFu;
    }
}
static void row(const uint32_t* w, int n) {
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (int k = 0; k < 8; k++) { const uint32_t t = drawOnce(w, n); lo = t < lo ? t : lo; hi = t > hi ? t : hi; }
    printf(" %7u..%u\n", (unsigned)lo, (unsigned)hi);
}
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t w[8];

int main() {
    printf("\ngpu/gpu-height\n");
    printf("Wide rectangles and copies by height at two places; cycles from the first word to GPUSTAT bits 28+26,\n");
    printf("fastest..slowest of eight. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);                                  /* draw to the displayed area allowed, 15-bit page */
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);

    static const int H[9] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
    static const int Y[2] = { 0, 16 };
    static const int W[2] = { 256, 128 };
    for (int c = 0; c < 2; c++) {
        const uint32_t cmd = c ? 0x62u : 0x60u;
        printf("%s RECTANGLE (%02xh):\n", c ? "SEMI" : "FLAT", (unsigned)cmd);
        for (int wi = 0; wi < 2; wi++) for (int yi = 0; yi < 2; yi++) for (int hi = 0; hi < 9; hi++) {
            w[0] = (cmd << 24) | 0x204080u; w[1] = xy(0, Y[yi]); w[2] = xy(W[wi], H[hi]);
            printf("  %3dx%-3d at Y %2d", W[wi], H[hi], Y[yi]); row(w, 3);
        }
    }
    printf("COPY (80h), 128 wide from (512,0):\n");
    for (int yi = 0; yi < 2; yi++) for (int hi = 0; hi < 9; hi++) {
        w[0] = 0x80000000u; w[1] = xy(512, 0); w[2] = xy(0, Y[yi]); w[3] = xy(128, H[hi]);
        printf("  128x%-3d to Y %2d", H[hi], Y[yi]); row(w, 4);
    }
    waitIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-height: Done.\n");
    for (;;) VSync(0);
}
