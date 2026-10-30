/*
 * gpu/gpu-phase - a primitive's cost by WHERE IN THE SCAN LINE it starts (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 one 256x32 flat rectangle costs either ~4,285 or ~4,550 cycles (semi 256x32 ~6,290 or ~6,900, a
 * 128x32 copy ~5,300 or ~5,560) - hwtests/gpu-tail, drawn at arbitrary times: two values, no dependence on how long the
 * GPU idled before. hwtests/gpu-scanout2, which starts every draw the moment Timer 1 (counting hblanks) reaches a line,
 * only ever saw the cheap one, at every line and display mode. So the start's place in the line decides it.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 counting hblanks and reset at VBlank (0103h, as
 * gpu-scanout2). For each delay D = 0, 50, 100 ... 2150 cycles after Timer 1 reaches line 100: wait D, write the probe,
 * poll GPUSTAT bits 28+26; four tries (one a frame), fastest..slowest. Probes: flat 256x32 (60h) and semi 256x32 (62h)
 * at (0,16). A row: D, then each probe's fastest..slowest.
 *
 * READ IT LIKE THIS. A window of D where the cost steps up: its position in the line (a line is ~2,150 cycles on an NTSC
 * console) and its width. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-phase`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

static uint32_t drawAt(const uint32_t* w, int n, uint32_t d) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (read16(T1_COUNT) < 100 && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < d) {} }
    const uint32_t t0 = now();
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-phase\n");
    printf("A flat and a semi 256x32 rectangle started D cycles after Timer 1 (hblanks since VBlank) reaches line 100;\n");
    printf("cycles to GPUSTAT bits 28+26, fastest..slowest of four (one a frame). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(T1_MODE, 0x0103);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    static uint32_t flat[3], semi[3];
    flat[0] = 0x60204080u; flat[1] = xy(0, 16); flat[2] = xy(256, 32);
    semi[0] = 0x62204080u; semi[1] = xy(0, 16); semi[2] = xy(256, 32);
    for (uint32_t d = 0; d <= 2150; d += 50) {
        uint32_t lo[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu }, hi[2] = { 0, 0 };
        for (int r = 0; r < 4; r++) for (int p = 0; p < 2; p++) {
            const uint32_t t = drawAt(p ? semi : flat, 3, d);
            lo[p] = t < lo[p] ? t : lo[p]; hi[p] = t > hi[p] ? t : hi[p];
        }
        printf("  D %4u   flat %5u..%-5u   semi %5u..%u\n", (unsigned)d, (unsigned)lo[0], (unsigned)hi[0], (unsigned)lo[1], (unsigned)hi[1]);
    }
    waitReady();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-phase: Done.\n");
    for (;;) VSync(0);
}
