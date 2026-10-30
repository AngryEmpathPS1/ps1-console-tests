/*
 * gpu/gpu-vbl - a probe's cost by the time since VBlank on Timer 2 (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a semi 256x32 probe costs ~6,200 (cheap) or ~6,830-6,930 (dear). gpu-phase2 (each probe after a
 * VBlank wait, Timer 1 0103h polled to line L, every eighth line) found every line cheap; gpu-tail7 / gpu-drift (a VBlank
 * wait, then 200,000 cycles counted on Timer 2) dear, every frame of a minute. This maps the cost against the time since
 * VBlank counted on Timer 2 alone, across the frame.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, Timer 1 untouched. For X = 0, 20,000 ... 540,000 cycles: four
 * tries of a VBlank wait, X idle cycles, one semi 256x32 probe at (0,16) timed to GPUSTAT bits 28+26; fastest..slowest and
 * how many cheap (under 6,600). A line every X.
 *
 * READ IT LIKE THIS. Where along the frame it turns dear. No assertions.
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
    printf("\ngpu/gpu-vbl\n");
    printf("A VBlank wait, X idle cycles on Timer 2, a semi 256x32 probe: fastest..slowest of four and how many cheap.\n");
    printf("No assertions.\n\n");
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
    for (uint32_t x = 0; x <= 540000; x += 20000) {
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
        printf("  X %6u: %u..%u cheap %d of 4\n", (unsigned)x, (unsigned)lo, (unsigned)hi, cheap);
        EnterCriticalSection();
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-vbl: Done.\n");
    for (;;) VSync(0);
}
