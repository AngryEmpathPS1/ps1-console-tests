/*
 * gpu/gpu-tail2 - how long after a LONG draw the next one stays dear (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 the same 256x32 flat rectangle costs 4,284 cycles in hwtests/gpu-scanout2 (each draw started
 * after VBlank and some scan lines - the GPU idle for a long time) and 4,537 in gpu-height (back to back, the next the
 * moment bits 28 and 26 read set); semi 6,272 against 6,870; a 128x32 copy 5,297 against 5,592. And gpu-height's tall
 * shapes cost less a row than its short ones would say. If the GPU reports ready while its last writes still drain,
 * a draw straight after another pays that tail, and a draw's own tail is never in its count.
 *
 * WHAT IT DOES. gpu-tail found every probe bimodal (flat 256x32 ~4,230 or ~4,550 on the SCPH-7501) at every idle up to
 * 3,200 cycles after a 256x64 primer, and gpu-phase / gpu-phase2 (a VBlank's idle before each draw) only the cheap value.
 * Here the primer is LONG (flat 256x256, ~35,000 cycles) and N runs to 128,000: N = 0, 1,000, 2,000, 4,000, 8,000,
 * 16,000, 32,000, 64,000, 128,000; probes flat and semi 256x32; fastest..slowest of eight, and how many of the eight
 * read the dear value (over the cheap one + 150).
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
    printf("\ngpu/gpu-tail2\n");
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
    mk(primers[0], "flat 256x256", 0, 256, 256);
    mk(probes[0], "flat 256x32", 0, 32, 16); mk(probes[1], "semi 256x32", 1, 32, 16);
    static const uint32_t N[9] = { 0, 1000, 2000, 4000, 8000, 16000, 32000, 64000, 128000 };
    for (int b = 0; b < 2; b++) {
        printf("  %s -> %s:\n", primers[0].name, probes[b].name);
        for (int k = 0; k < 9; k++) {
            uint32_t t[8], lo = 0xFFFFFFFFu, hi = 0;
            for (int r = 0; r < 8; r++) { t[r] = once(primers[0], probes[b], N[k]); lo = t[r] < lo ? t[r] : lo; hi = t[r] > hi ? t[r] : hi; }
            int dear = 0; for (int r = 0; r < 8; r++) if (t[r] > lo + 150) dear++;
            printf("    N %6u: %u..%u  dear %d of 8\n", (unsigned)N[k], (unsigned)lo, (unsigned)hi, dear);
        }
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail2: Done.\n");
    for (;;) VSync(0);
}
