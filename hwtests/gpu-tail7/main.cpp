/*
 * gpu/gpu-tail7 - the GPU's cheap stretch after a VBlank: what it counts (2026-10-12 night).
 *
 * WHY. hwtests/gpu-tail6 on the SCPH-7501: after a VBlank wait, semi 256x32 probes back to back cost ~6,200 cycles each
 * for the first seven, ~6,450 the eighth, ~6,830 from the ninth on; without a VBlank wait a probe is dear whatever the
 * idle before it (128,000-768,000 cycles: 0-1 of 8 cheap). gpu-phase2: ONE probe a frame, at any line, always cheap.
 * So after each VBlank there are ~45,000 cycles of cheap drawing, spent by drawing, not by time.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; eight tries each, each probe timed to GPUSTAT bits 28+26,
 * fastest..slowest and how many cheap (flat 256x32 under 4,420, semi 256x32 under 6,600):
 *   A  a VBlank wait, then 24 FLAT probes back to back (cycles or pixels: semi probes are ~1.5x a flat one's cycles)
 *   B  a VBlank wait, 200,000 idle cycles, then 16 semi probes (does the stretch start at the VBlank or the first draw)
 *   C  a VBlank wait, 4 semi probes, 100,000 idle cycles, 12 more (does an idle refill it)
 *
 * READ IT LIKE THIS. The probe number where each turns dear. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail7`.
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
static void idle(uint32_t n) { const uint32_t t0 = now(); while (now() - t0 < n) {} }
static void vblank() { waitReady(); write32(I_STAT, ~1u); const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
static uint32_t probe(int semi) {
    waitReady();
    const uint32_t t0 = now();
    write32(GP0, ((semi ? 0x62u : 0x60u) << 24) | 0x204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}
static uint32_t lo[24], hi[24]; static int cheap[24];
static void reset() { for (int i = 0; i < 24; i++) { lo[i] = 0xFFFFFFFFu; hi[i] = 0; cheap[i] = 0; } }
static void take(int i, uint32_t t, int semi) { lo[i] = t < lo[i] ? t : lo[i]; hi[i] = t > hi[i] ? t : hi[i]; if (t < (semi ? 6600u : 4420u)) cheap[i]++; }
static void show(const char* name, int n) { printf("%s\n", name); for (int i = 0; i < n; i++) printf("  #%-2d %u..%u cheap %d\n", i + 1, (unsigned)lo[i], (unsigned)hi[i], cheap[i]); }

int main() {
    printf("\ngpu/gpu-tail7\n");
    printf("The cheap stretch after a VBlank: A 24 flat probes, B 200,000 idle cycles first, C an idle in the middle. 256x32 at\n");
    printf("(0,16); fastest..slowest of eight and how many cheap (flat under 4,420, semi under 6,600). No assertions.\n\n");
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
    reset();
    for (int r = 0; r < 8; r++) { vblank(); for (int i = 0; i < 24; i++) take(i, probe(0), 0); }
    show("A  a VBlank wait, then 24 flat probes:", 24);
    reset();
    for (int r = 0; r < 8; r++) { vblank(); idle(200000); for (int i = 0; i < 16; i++) take(i, probe(1), 1); }
    show("B  a VBlank wait, 200,000 idle cycles, then 16 semi probes:", 16);
    reset();
    for (int r = 0; r < 8; r++) { vblank(); for (int i = 0; i < 16; i++) { if (i == 4) { waitReady(); idle(100000); } take(i, probe(1), 1); } }
    show("C  a VBlank wait, 4 semi probes, 100,000 idle cycles, 12 more:", 16);
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail7: Done.\n");
    for (;;) VSync(0);
}
