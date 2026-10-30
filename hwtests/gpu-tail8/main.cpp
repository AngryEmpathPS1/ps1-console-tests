/*
 * gpu/gpu-tail8 - why gpu-phase's draws deep in the frame are cheap when gpu-tail7's are not (2026-10-12 night).
 *
 * WHY. hwtests/gpu-tail7 on the SCPH-7501: a 256x32 probe is cheap for ~46,000 cycles after a VBlank (11 flat or 7 semi
 * back to back) and dear after, and a VBlank wait then 200,000 idle cycles (polled on Timer 2) gives dear probes. But
 * gpu-phase and gpu-phase2 drew the same probe at line 100 and at every eighth line to 256 after a VBlank - ~200,000-
 * 550,000 cycles after it - cheap every time. Theirs set Timer 1 to 0103h (hblanks, reset at VBlank) and waited on Timer 1.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; eight tries of four semi 256x32 probes at (0,16) back to
 * back, each timed, how many cheap (under 6,600):
 *   B1  Timer 1 as the runner left it; a VBlank wait; 200,000 cycles idle on Timer 2
 *   B2  Timer 1 0103h; a VBlank wait; Timer 1 polled to 93 hblanks (~200,000 cycles)
 *   B3  Timer 1 0103h; a VBlank wait; 200,000 cycles idle on Timer 2
 *   B4  Timer 1 0103h; no VBlank wait; Timer 1 polled until it reads 93 (the line since its own reset)
 *   B5  Timer 1 as left; a VBlank wait; Timer 1 polled for 93 counts whatever it counts
 *
 * READ IT LIKE THIS. Cheap only where Timer 1 is polled, or only where it is 0103h. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail8`.
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
static void idle(uint32_t n) { const uint32_t t0 = now(); while (now() - t0 < n) {} }
static void vblank() { write32(I_STAT, ~1u); const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
static void t1To(uint16_t n) { const uint32_t t0 = now(); while (read16(T1_COUNT) != n && now() - t0 < 2000000u) {} }
static uint32_t probe() {
    waitReady();
    const uint32_t t0 = now();
    write32(GP0, 0x62204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-tail8\n");
    printf("Four semi 256x32 probes at (0,16) back to back, ~200,000 cycles after a VBlank five ways: fastest..slowest of eight\n");
    printf("and how many cheap (under 6,600). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    static const char* const NAME[5] = { "B1 T1 as left, VBlank, 200,000 on T2", "B2 T1 0103h, VBlank, T1 to 93", "B3 T1 0103h, VBlank, 200,000 on T2",
                                         "B4 T1 0103h, T1 to 93 (no VBlank wait)", "B5 T1 as left, VBlank, T1 93 counts" };
    for (int c = 0; c < 5; c++) {
        write16(T1_MODE, (c == 0 || c == 4) ? m1 : 0x0103);
        uint32_t lo[4], hi[4]; int cheap[4];
        for (int i = 0; i < 4; i++) { lo[i] = 0xFFFFFFFFu; hi[i] = 0; cheap[i] = 0; }
        for (int r = 0; r < 8; r++) {
            waitReady();
            if (c != 3) vblank();
            if (c == 0 || c == 2) idle(200000);
            if (c == 1 || c == 3) t1To(93);
            if (c == 4) { const uint16_t s = read16(T1_COUNT); t1To((uint16_t)(s + 93)); }
            for (int i = 0; i < 4; i++) { const uint32_t t = probe(); lo[i] = t < lo[i] ? t : lo[i]; hi[i] = t > hi[i] ? t : hi[i]; if (t < 6600u) cheap[i]++; }
        }
        printf("  %-40s", NAME[c]);
        for (int i = 0; i < 4; i++) printf(" #%d %u..%u cheap %d", i + 1, (unsigned)lo[i], (unsigned)hi[i], cheap[i]);
        printf("\n");
    }
    waitReady();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail8: Done.\n");
    for (;;) VSync(0);
}
