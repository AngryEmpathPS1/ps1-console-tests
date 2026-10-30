/*
 * gpu/gpu-tail9 - the display on or off, and one probe a frame or four: the GPU's cheap and dear draws (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a semi 256x32 probe costs ~6,200 cycles (cheap) or ~6,830 (dear). Cheap for ~46,000 cycles after a
 * VBlank and dear after (hwtests/gpu-tail6, gpu-tail7) - the length of the vertical blanking, as if the picture's scan-out
 * took VRAM time on every active line. But gpu-phase and gpu-phase2 (one probe a frame, Timer 1 0103h, waited to a line)
 * were cheap at every line, and gpu-tail8's same procedure with four probes a frame mostly dear. This runs both in one
 * program, with the display as the runner left it, forced on, and forced off, and prints GPUSTAT.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 0103h (hblanks, reset at VBlank). GPUSTAT printed.
 * For each display state - as left, GP1(03h) = 0 (on), GP1(03h) = 1 (off) - eight tries of: a VBlank wait, Timer 1
 * polled to line 100, then ONE semi 256x32 probe at (0,16); and eight tries of the same with FOUR probes back to back.
 * Each probe's fastest..slowest and how many were cheap (under 6,600). The display state is put back at the end.
 *
 * READ IT LIKE THIS. Cheap with the display off and dear with it on: the scan-out. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail9`.
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
static uint32_t probe() {
    waitReady();
    const uint32_t t0 = now();
    write32(GP0, 0x62204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-tail9\n");
    const uint32_t stat0 = read32(GP1);
    printf("GPUSTAT at the start %08x: display %s, %s, %s, video %s\n\n", (unsigned)stat0, (stat0 >> 23) & 1 ? "OFF" : "on",
           (stat0 >> 22) & 1 ? "interlaced" : "not interlaced", (stat0 >> 21) & 1 ? "24-bit" : "15-bit", (stat0 >> 20) & 1 ? "PAL" : "NTSC");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(T1_MODE, 0x0103);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    static const char* const DISP[3] = { "display as left", "display ON", "display OFF" };
    for (int d = 0; d < 3; d++) {
        if (d == 1) write32(GP1, 0x03000000u);
        if (d == 2) write32(GP1, 0x03000001u);
        for (int n = 1; n <= 4; n += 3) {
            uint32_t lo[4], hi[4]; int cheap[4];
            for (int i = 0; i < 4; i++) { lo[i] = 0xFFFFFFFFu; hi[i] = 0; cheap[i] = 0; }
            for (int r = 0; r < 8; r++) {
                waitReady();
                write32(I_STAT, ~1u);
                { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
                { const uint32_t t0 = now(); while (read16(T1_COUNT) < 100 && now() - t0 < 2000000u) {} }
                for (int i = 0; i < n; i++) { const uint32_t t = probe(); lo[i] = t < lo[i] ? t : lo[i]; hi[i] = t > hi[i] ? t : hi[i]; if (t < 6600u) cheap[i]++; }
            }
            printf("  %-16s %d a frame:", DISP[d], n);
            for (int i = 0; i < n; i++) printf(" #%d %u..%u cheap %d", i + 1, (unsigned)lo[i], (unsigned)hi[i], cheap[i]);
            printf("\n");
        }
    }
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail9: Done.\n");
    for (;;) VSync(0);
}
