/*
 * gpu/gpu-drift - one probe a frame for a minute: does the GPU's cheap/dear state drift against the frame (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a semi 256x32 probe costs ~6,200 or ~6,830 cycles. Within a run the state holds to the frame - in
 * hwtests/gpu-tail6/7 a stretch of ~46,000 cycles after each VBlank cheap and the rest dear - but between runs minutes
 * apart it differs: gpu-tail8 found probes ~200,000 cycles after a VBlank dear, gpu-tail9 (the same procedure) cheap. A
 * GPU-internal period drifting slowly against the video frame would do that. This samples one point of the frame for a
 * minute.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For 3,600 frames: a VBlank wait, 200,000 idle cycles, one
 * semi 256x32 probe at (0,16) timed to GPUSTAT bits 28+26; printed as one letter a frame - c (under 6,600) or D - 72 a
 * line (printed as each line completes, interrupts back on for the print - the runner ends a quiet test), with the
 * fastest and slowest of each line's cheap and dear probes after it.
 *
 * READ IT LIKE THIS. Runs of c and D: their lengths are the drift's period against the frame. All one letter: no drift
 * within a minute. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-drift --frames=6000`.
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

static char line[73];
static uint16_t cost[3600];
/* one line: frames f0..f0+71 as c / D, then the cheap and dear ranges */
static void show(int f0) {
    uint32_t clo = 0xFFFFFFFFu, chi = 0, dlo = 0xFFFFFFFFu, dhi = 0;
    for (int i = 0; i < 72; i++) {
        const uint32_t t = cost[f0 + i];
        const bool cheap = t < 6600u;
        if (cheap) { clo = t < clo ? t : clo; chi = t > chi ? t : chi; } else { dlo = t < dlo ? t : dlo; dhi = t > dhi ? t : dhi; }
        line[i] = cheap ? 'c' : 'D';
    }
    line[72] = 0;
    printf("  %s", line);
    if (chi) printf("  c %u..%u", (unsigned)clo, (unsigned)chi);
    if (dhi) printf("  D %u..%u", (unsigned)dlo, (unsigned)dhi);
    printf("\n");
}
int main() {
    printf("\ngpu/gpu-drift\n");
    printf("3,600 frames: a VBlank wait, 200,000 idle cycles, a semi 256x32 probe - c (under 6,600 cycles) or D, 72 a line.\n");
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
    for (int f = 0; f < 3600; f++) {
        waitReady();
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        { const uint32_t t0 = now(); while (now() - t0 < 200000u) {} }
        const uint32_t t0 = now();
        write32(GP0, 0x62204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
        uint32_t t;
        for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
        cost[f] = (uint16_t)(t > 65535u ? 65535u : t);
        if (f % 72 == 71) { ExitCriticalSection(); show(f - 71); EnterCriticalSection(); }   /* a line a 72 frames: the runner ends a quiet test */
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-drift: Done.\n");
    for (;;) VSync(0);
}
