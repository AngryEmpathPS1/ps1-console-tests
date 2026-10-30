/*
 * gpu/gpu-tail - does a primitive's cost depend on how long the GPU sat idle before it: a draw's writes still draining
 * after GPUSTAT says ready (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 the same 256x32 flat rectangle costs 4,284 cycles in hwtests/gpu-scanout2 (each draw started
 * after VBlank and some scan lines - the GPU idle for a long time) and 4,537 in gpu-height (back to back, the next the
 * moment bits 28 and 26 read set); semi 6,272 against 6,870; a 128x32 copy 5,297 against 5,592. And gpu-height's tall
 * shapes cost less a row than its short ones would say. If the GPU reports ready while its last writes still drain,
 * a draw straight after another pays that tail, and a draw's own tail is never in its count.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. A PRIMER primitive (flat 256x64, semi 256x64 or a 128x64
 * copy), then bits 28+26 polled set, then an idle wait of N cycles (N = 0, 25, 50, 100, 200, 400, 800, 1600, 3200), then
 * a PROBE (flat 256x32, semi 256x32, copy 128x32) timed from its first word to bits 28+26: fastest..slowest of eight.
 * Every primer against every probe. A row: primer, probe, then each N's fastest..slowest.
 *
 * READ IT LIKE THIS. A probe that gets cheaper as N grows until it levels off: the level is its own cost from an idle
 * GPU, and the step is the primer's tail - how long, by what primitive. Flat for every N: no tail. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail`.
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
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

struct Prim { const char* name; uint32_t w[4]; int n; };
static Prim primers[3], probes[3];
static void mk(Prim& p, const char* name, int kind, int h, int y) {
    p.name = name;
    if (kind == 2) { p.w[0] = 0x80000000u; p.w[1] = xy(512, 0); p.w[2] = xy(0, y); p.w[3] = xy(128, h); p.n = 4; }
    else { p.w[0] = ((kind ? 0x62u : 0x60u) << 24) | 0x204080u; p.w[1] = xy(0, y); p.w[2] = xy(256, h); p.n = 3; }
}
static uint32_t once(const Prim& a, const Prim& b, uint32_t idle) {
    waitReady();
    { const uint32_t t0 = now(); while (now() - t0 < 4000u) {} }       /* everything before long drained */
    for (int i = 0; i < a.n; i++) write32(GP0, a.w[i]);
    waitReady();
    { const uint32_t t0 = now(); while (now() - t0 < idle) {} }
    const uint32_t t0 = now();
    for (int i = 0; i < b.n; i++) write32(GP0, b.w[i]);
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-tail\n");
    printf("A primer primitive, GPUSTAT bits 28+26 set, N idle cycles, then a probe timed to bits 28+26: fastest..slowest\n");
    printf("of eight. No assertions.\n\n");
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
    mk(primers[0], "flat 256x64", 0, 64, 300); mk(primers[1], "semi 256x64", 1, 64, 300); mk(primers[2], "copy 128x64", 2, 64, 300);
    mk(probes[0], "flat 256x32", 0, 32, 16); mk(probes[1], "semi 256x32", 1, 32, 16); mk(probes[2], "copy 128x32", 2, 32, 16);
    static const uint32_t N[9] = { 0, 25, 50, 100, 200, 400, 800, 1600, 3200 };
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
        printf("  %s -> %s:\n   ", primers[a].name, probes[b].name);
        for (int k = 0; k < 9; k++) {
            uint32_t lo = 0xFFFFFFFFu, hi = 0;
            for (int r = 0; r < 8; r++) { const uint32_t t = once(primers[a], probes[b], N[k]); lo = t < lo ? t : lo; hi = t > hi ? t : hi; }
            printf(" %u:%u..%u", (unsigned)N[k], (unsigned)lo, (unsigned)hi);
        }
        printf("\n");
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail: Done.\n");
    for (;;) VSync(0);
}
