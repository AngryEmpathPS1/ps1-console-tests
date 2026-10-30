/*
 * gpu/gpu-tail6 - how long an idle GPU needs to be to draw cheap, and how long the cheap stretch lasts (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a 256x32 flat rectangle at (0,16) is cheap (~4,210-4,300) or dear (~4,480-4,650); semi ~6,200-
 * 6,340 or ~6,830-7,015. Cheap after a VBlank wait every time - four back to back included (hwtests/gpu-tail5) - and
 * dear without one even 128,000 idle cycles after the last draw (gpu-tail2: 7 of 8 dear). A test that waits for each
 * VBlank leaves the GPU idle for most of a frame (~540,000 cycles) before its burst; so perhaps a long idle buys a cheap
 * stretch (a VRAM refresh banked ahead while idle?).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. (1) A VBlank wait, then SIXTEEN semi 256x32 probes back to
 * back (each the moment GPUSTAT bits 28+26 read set), each timed: eight tries, each probe's fastest..slowest and how
 * many were cheap (under 6,600). (2) No VBlank wait: a semi 256x32 probe after N idle cycles since the last draw, N =
 * 128,000, 192,000, 256,000, 384,000, 512,000, 768,000; eight tries each, fastest..slowest and how many cheap.
 *
 * READ IT LIKE THIS. (1): the probe number where they turn dear, times ~6,200, is the cheap stretch. (2): the idle at
 * which they turn cheap. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail6`.
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
static uint32_t probe() {
    waitReady();
    const uint32_t t0 = now();
    write32(GP0, 0x62204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-tail6\n");
    printf("(1) A VBlank wait, then sixteen semi 256x32 probes back to back; (2) a probe after N idle cycles, no VBlank wait.\n");
    printf("Fastest..slowest of eight and how many were cheap (under 6,600). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
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
    static uint32_t lo[16], hi[16]; static int cheap[16];
    for (int i = 0; i < 16; i++) { lo[i] = 0xFFFFFFFFu; hi[i] = 0; cheap[i] = 0; }
    for (int r = 0; r < 8; r++) {
        waitReady();
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        for (int i = 0; i < 16; i++) { const uint32_t t = probe(); lo[i] = t < lo[i] ? t : lo[i]; hi[i] = t > hi[i] ? t : hi[i]; if (t < 6600u) cheap[i]++; }
    }
    printf("(1) after a VBlank wait, probe by probe:\n");
    for (int i = 0; i < 16; i++) printf("  #%-2d %u..%u cheap %d\n", i + 1, (unsigned)lo[i], (unsigned)hi[i], cheap[i]);
    printf("(2) after N idle cycles, no VBlank wait:\n");
    static const uint32_t N[6] = { 128000, 192000, 256000, 384000, 512000, 768000 };
    for (int k = 0; k < 6; k++) {
        uint32_t l = 0xFFFFFFFFu, h = 0; int c = 0;
        for (int r = 0; r < 8; r++) {
            waitReady();
            { const uint32_t t0 = now(); while (now() - t0 < N[k]) {} }
            const uint32_t t = probe();
            l = t < l ? t : l; h = t > h ? t : h; if (t < 6600u) c++;
        }
        printf("  N %6u: %u..%u cheap %d of 8\n", (unsigned)N[k], (unsigned)l, (unsigned)h, c);
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail6: Done.\n");
    for (;;) VSync(0);
}
