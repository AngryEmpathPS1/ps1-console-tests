/*
 * gpu/gpu-tail3 - does WHERE THE LAST DRAW WAS decide whether the next is cheap or dear (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a 256x32 flat rectangle at (0,16) costs ~4,230-4,300 or ~4,550-4,650 cycles (semi ~6,215-6,340
 * or ~6,860-7,015). hwtests/gpu-tail2: after a 256x256 primer at (0,256) it is dear in 7 tries of 8 at every idle up to
 * 128,000 cycles - so not a time; gpu-phase / gpu-phase2, where every probe follows the same probe at the same place, are
 * cheap every time. So perhaps the previous draw's VRAM rows (a bank's open row, a cache) decide it.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For each PRIMER - none (the previous probe), the probe
 * itself, flat 256x32 at (0,300), flat 256x256 at (0,256), flat 16x16 at (0,300), flat 256x32 at (0,48) (right below
 * the probe), flat 256x32 at (512,16) (the probe's rows, X 512) - and N = 0 and 20,000 idle cycles: eight tries of
 * primer, bits 28+26 set, N idle, probe timed to bits 28+26 (a 4,000-cycle idle before each primer). The probe: flat
 * 256x32 at (0,16), then semi 256x32 at (0,16). A row: primer, N, probe, fastest..slowest and how many of the eight were
 * CHEAP (flat under 4,420, semi under 6,600).
 *
 * READ IT LIKE THIS. Cheap 8 of 8 behind some primers and 0-1 behind others: the previous draw's place is the switch.
 * No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail3`.
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
static void rect(uint32_t cmd, int x, int y, int w, int h) { write32(GP0, (cmd << 24) | 0x204080u); write32(GP0, xy(x, y)); write32(GP0, xy(w, h)); }
static void idle(uint32_t n) { const uint32_t t0 = now(); while (now() - t0 < n) {} }

struct Primer { const char* name; int x, y, w, h; };
static const Primer P[7] = {
    { "none (the last probe)", -1, 0, 0, 0 }, { "the probe itself", 0, 16, 256, 32 }, { "256x32 at (0,300)", 0, 300, 256, 32 },
    { "256x256 at (0,256)", 0, 256, 256, 256 }, { "16x16 at (0,300)", 0, 300, 16, 16 }, { "256x32 at (0,48)", 0, 48, 256, 32 },
    { "256x32 at (512,16)", 512, 16, 256, 32 },
};

int main() {
    printf("\ngpu/gpu-tail3\n");
    printf("A primer at a place, bits 28+26 set, N idle cycles, then a 256x32 probe at (0,16) timed to bits 28+26:\n");
    printf("fastest..slowest of eight and how many were cheap (flat under 4,420, semi under 6,600). No assertions.\n\n");
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
    static const uint32_t N[2] = { 0, 20000 };
    for (int semi = 0; semi < 2; semi++) for (int p = 0; p < 7; p++) for (int k = 0; k < 2; k++) {
        uint32_t lo = 0xFFFFFFFFu, hi = 0; int cheap = 0;
        for (int r = 0; r < 8; r++) {
            waitReady(); idle(4000);
            if (P[p].x >= 0) { rect(0x60, P[p].x, P[p].y, P[p].w, P[p].h); waitReady(); }
            idle(N[k]);
            const uint32_t t0 = now();
            rect(semi ? 0x62 : 0x60, 0, 16, 256, 32);
            uint32_t t;
            for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
            lo = t < lo ? t : lo; hi = t > hi ? t : hi;
            if (t < (semi ? 6600u : 4420u)) cheap++;
        }
        printf("  %-22s N %5u  %s  %u..%u  cheap %d of 8\n", P[p].name, (unsigned)N[k], semi ? "semi" : "flat", (unsigned)lo, (unsigned)hi, cheap);
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail3: Done.\n");
    for (;;) VSync(0);
}
