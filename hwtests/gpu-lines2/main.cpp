/*
 * gpu/gpu-lines2 - does a line pay the scan-out's toll, by its angle (2026-10-13 night).
 *
 * WHY. hwtests/gpu-lines on the SCPH-7501 timed lines in the vertical blanking only; gpu-scan4 found a SHALLOW line,
 * (0,16)-(255,100), toll-free on active lines in every mode (0-3 tenths of a percent). But gpu-cost-sweep's lines, drawn
 * back to back through the frame, read a steep 256-pixel line 1,325-1,460 cycles on three consoles where gpu-lines'
 * blanking reads 1,259: a steep line - a pixel a row, every pixel its own frame-buffer access - may pay what a shallow
 * one does not.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. In 512x240 (the runner's mode, three refills a line) and
 * 640x240 24-bit (seven), range 16..256: lines from (0,16), 256 along the major axis, at rises 0, 32, 64, 128, 170, 200,
 * 228, 256 under a 255 run and a vertical one, flat (40h) and semi (42h), four times 20,000 cycles after the VBlank IRQ
 * (the blanking) and four 200,000 after (active lines), the fastest of each, timed from the first GP0 word to GPUSTAT
 * bits 28+26; the toll in tenths of a percent.
 *
 * READ IT LIKE THIS. The toll against the angle: 0 for level and shallow lines (gpu-scan4), rising as the line steepens
 * if a pixel a row pays. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-lines2 --frames=5000 --quiet=5000`.
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

static uint32_t W[4];
static uint32_t timeWords(uint32_t x) {
    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < 4; r++) {
        waitReady();
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        { const uint32_t t0 = now(); while (now() - t0 < x) {} }
        const uint32_t t0 = now();
        for (int i = 0; i < 3; i++) write32(GP0, W[i]);
        uint32_t t;
        for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
        best = t < best ? t : best;
    }
    return best;
}

int main() {
    printf("\ngpu/gpu-lines2\n");
    printf("Lines in the blanking (20,000 cycles after the VBlank IRQ) and on active lines (200,000), fastest of four, and the\n");
    printf("active lines' toll in tenths of a percent. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
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
    static const int DX[9] = { 255, 255, 255, 255, 255, 255, 255, 255, 0 }, DY[9] = { 0, 32, 64, 128, 170, 200, 228, 255, 255 };
    static const char* const MN[2] = { "512x240", "640x240 24-bit" };
    static const uint32_t MODE[2] = { 0x02, 0x13 };
    for (int m = 0; m < 2; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int i = 0; i < 9; i++) for (int semi = 0; semi < 2; semi++) {
            W[0] = semi ? 0x42204080u : 0x40204080u; W[1] = xy(0, 16); W[2] = xy(DX[i], 16 + DY[i]);
            const uint32_t b = timeWords(20000), a = timeWords(200000);
            ExitCriticalSection();
            printf("  %-15s %s dx %3d dy %3d  blank %5u  active %5u  toll %d\n", MN[m], semi ? "semi" : "flat", DX[i], DY[i], (unsigned)b, (unsigned)a, b ? ((int)a - (int)b) * 1000 / (int)b : 0);
            EnterCriticalSection();
        }
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-lines2: Done.\n");
    for (;;) VSync(0);
}
