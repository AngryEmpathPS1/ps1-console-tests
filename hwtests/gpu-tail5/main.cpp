/*
 * gpu/gpu-tail5 - is it the FIRST draw after a VBlank that is cheap (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a 256x32 flat rectangle at (0,16) is cheap (~4,210-4,300) or dear (~4,480-4,650); semi ~6,200-
 * 6,340 or ~6,830-7,015. hwtests/gpu-tail4: a VBlank wait (I_STAT bit 0 acknowledged and polled) before the draw makes it
 * cheap 8 of 8 whatever Timer 1's mode, and without the wait it is dear (0-2 of 8 cheap); gpu-tail2's 1 cheap in 8 at
 * up to 128,000 idle cycles, and gpu-phase2's cheap draws at every line of the frame after a VBlank, read the same way.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Eight tries of: a VBlank wait, then FOUR probes back to back
 * (each the moment GPUSTAT bits 28+26 read set), each timed; and eight tries of: a VBlank wait, a 1x1 flat rectangle,
 * then four probes. Flat then semi 256x32 at (0,16). A row: the case, then each probe's fastest..slowest and how many
 * of the eight were cheap (flat under 4,420, semi under 6,600).
 *
 * READ IT LIKE THIS. Probe 1 cheap and 2-4 dear: the first draw after a VBlank. All cheap: the VBlank itself sets the
 * state, for longer than four draws. The 1x1 rows: whether any draw spends it. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail5`.
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
static uint32_t probe(int semi) {
    waitReady();
    const uint32_t t0 = now();
    write32(GP0, ((semi ? 0x62u : 0x60u) << 24) | 0x204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-tail5\n");
    printf("A VBlank wait, then four 256x32 probes at (0,16) back to back (and with a 1x1 draw first): each probe's\n");
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
    for (int semi = 0; semi < 2; semi++) for (int dot = 0; dot < 2; dot++) {
        uint32_t lo[4], hi[4]; int cheap[4];
        for (int i = 0; i < 4; i++) { lo[i] = 0xFFFFFFFFu; hi[i] = 0; cheap[i] = 0; }
        for (int r = 0; r < 8; r++) {
            waitReady();
            write32(I_STAT, ~1u);
            { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
            if (dot) { write32(GP0, 0x60204080u); write32(GP0, xy(600, 400)); write32(GP0, xy(1, 1)); }
            for (int i = 0; i < 4; i++) {
                const uint32_t t = probe(semi);
                lo[i] = t < lo[i] ? t : lo[i]; hi[i] = t > hi[i] ? t : hi[i];
                if (t < (semi ? 6600u : 4420u)) cheap[i]++;
            }
        }
        printf("  %s%s:\n   ", semi ? "semi" : "flat", dot ? ", a 1x1 draw first" : "");
        for (int i = 0; i < 4; i++) printf(" #%d %u..%u cheap %d", i + 1, (unsigned)lo[i], (unsigned)hi[i], cheap[i]);
        printf("\n");
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail5: Done.\n");
    for (;;) VSync(0);
}
