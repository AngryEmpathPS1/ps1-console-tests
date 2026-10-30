/*
 * gpu/gpu-tail4 - Timer 1's mode, or a VBlank wait: which makes the cheap draw (2026-10-12 night).
 *
 * WHY. On the SCPH-7501 a 256x32 flat rectangle at (0,16) is cheap (~4,220-4,300) or dear (~4,480-4,650), semi ~6,240-
 * 6,340 or ~6,830-7,015. hwtests/gpu-tail3: dear behind every kind of previous draw, the probe after the probe included
 * (0-4 tries of 8 cheap). hwtests/gpu-phase and gpu-phase2 - the same probe after the same probe - cheap 4 of 4 at every
 * line and place in a line. Theirs differ in two things: Timer 1 set to 0103h (hblanks, reset at VBlank) for the whole
 * test, and a VBlank wait (I_STAT bit 0 acknowledged and polled) before every draw.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Eight tries of: (a 4,000-cycle idle) the probe, timed to
 * GPUSTAT bits 28+26 - under: Timer 1 as the runner left it; 0103h; 0100h (hblanks, no sync); 0000h (system clock);
 * and Timer 1 as left with a VBlank wait before each draw; and 0103h with the VBlank wait. Flat then semi 256x32 at
 * (0,16). A row: the case, fastest..slowest, how many were cheap (flat under 4,420, semi under 6,600).
 *
 * READ IT LIKE THIS. Cheap in the 0103h rows and dear in the others: Timer 1's mode (its hblank sync) is the switch;
 * cheap only with the VBlank wait: the wait. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tail4`.
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

int main() {
    printf("\ngpu/gpu-tail4\n");
    printf("A 256x32 probe at (0,16) under Timer 1 modes and with or without a VBlank wait before it: fastest..slowest of\n");
    printf("eight and how many were cheap (flat under 4,420, semi under 6,600). No assertions.\n\n");
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
    struct Case { const char* name; int mode; bool vbl; };
    static const Case C[6] = { { "Timer 1 as left", -1, false }, { "Timer 1 0103h", 0x0103, false }, { "Timer 1 0100h", 0x0100, false },
                               { "Timer 1 0000h", 0x0000, false }, { "as left + VBlank wait", -1, true }, { "0103h + VBlank wait", 0x0103, true } };
    for (int semi = 0; semi < 2; semi++) for (int c = 0; c < 6; c++) {
        write16(T1_MODE, C[c].mode < 0 ? m1 : (uint16_t)C[c].mode);
        uint32_t lo = 0xFFFFFFFFu, hi = 0; int cheap = 0;
        for (int r = 0; r < 8; r++) {
            waitReady(); idle(4000);
            if (C[c].vbl) { write32(I_STAT, ~1u); const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
            const uint32_t t0 = now();
            write32(GP0, ((semi ? 0x62u : 0x60u) << 24) | 0x204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(256, 32));
            uint32_t t;
            for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
            lo = t < lo ? t : lo; hi = t > hi ? t : hi;
            if (t < (semi ? 6600u : 4420u)) cheap++;
        }
        printf("  %-22s %s  %u..%u  cheap %d of 8\n", C[c].name, semi ? "semi" : "flat", (unsigned)lo, (unsigned)hi, cheap);
    }
    waitReady();
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tail4: Done.\n");
    for (;;) VSync(0);
}
