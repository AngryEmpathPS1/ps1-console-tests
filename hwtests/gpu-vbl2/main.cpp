/*
 * gpu/gpu-vbl2 - the active-line slowdown by display mode, and with the display off (2026-10-12 night).
 *
 * WHY. hwtests/gpu-vbl on the SCPH-7501: a semi 256x32 probe costs ~6,240 cycles for the first ~40,000 cycles after the
 * VBlank IRQ (on Timer 2) and ~6,830-6,930 from 60,000 on - the vertical blanking against the active lines, as if the
 * picture's scan-out took VRAM time. (gpu-scanout2 / gpu-phase / gpu-phase2 / gpu-tail9 waited on Timer 1 for a line
 * after the IRQ, and that wait did not wait: every probe of theirs was drawn in the blanking.)
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For each display state - off (GP1(03h) = 1), 256x240 15-bit,
 * 320x240 15-bit, 640x240 15-bit, 640x480 interlaced 15-bit, 640x480 interlaced 24-bit, all from (0,0), vertical range
 * 16..256 - four tries each of a VBlank wait, X idle cycles (X = 20,000: the blanking; 200,000: active lines), one semi
 * 256x32 probe at (0,16) timed to GPUSTAT bits 28+26: fastest..slowest and how many cheap (under 6,600). The display
 * mode the runner left is put back.
 *
 * READ IT LIKE THIS. Off: cheap at both X if the scan-out is the cost; by mode: the active-line cost against the picture's
 * bytes a line. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-vbl --frames=3000`.
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

int main() {
    printf("\ngpu/gpu-vbl2\n");
    printf("By display state: a VBlank wait, X idle cycles on Timer 2, a semi 256x32 probe; fastest..slowest of four, how many cheap.\n");
    printf("No assertions.\n\n");
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
    static const char* const NAME[6] = { "display off", "256x240 15-bit", "320x240 15-bit", "640x240 15-bit", "640x480i 15-bit", "640x480i 24-bit" };
    static const uint32_t MODE[6] = { 0, 0x00, 0x01, 0x03, 0x27, 0x37 };
    for (int d = 0; d < 6; d++) {
        if (d == 0) write32(GP1, 0x03000001u);
        else { write32(GP1, 0x08000000u | MODE[d]); write32(GP1, 0x03000000u); }
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int k = 0; k < 2; k++) {
            const uint32_t x = k ? 200000u : 20000u;
            uint32_t lo = 0xFFFFFFFFu, hi = 0; int cheap = 0;
            for (int r = 0; r < 4; r++) {
                waitReady();
                write32(I_STAT, ~1u);
                { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
                { const uint32_t t0 = now(); while (now() - t0 < x) {} }
                const uint32_t t0 = now();
                write32(GP0, 0x62204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
                uint32_t t;
                for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
                lo = t < lo ? t : lo; hi = t > hi ? t : hi; if (t < 6600u) cheap++;
            }
            ExitCriticalSection();
            printf("  %-16s X %6u: %u..%u cheap %d of 4\n", NAME[d], (unsigned)x, (unsigned)lo, (unsigned)hi, cheap);
            EnterCriticalSection();
        }
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-vbl2: Done.\n");
    for (;;) VSync(0);
}
