/*
 * gpu/gpu-lines - a line's cost by its angle and length, flat, gouraud and semi, and a poly-line's segments
 * (2026-10-13 night).
 *
 * WHY. This core charges a line 36.4 ticks to set up, 1.6 a step and 5.94 more a row change (v0.377.24, fitted to
 * gpu-cost-sweep's three: across, down, diagonal), and it fits those. gpu-scan4 on the SCPH-7501 drew a SHALLOW one,
 * (0,16)-(255,100): 822 cycles, where this model reads 716 - 15% short. psx-spx (nocash, "GPU Timings", lines, the new
 * GPU) charges 40 a segment, 1 a pixel when horizontal and 2 when not, and a scanline 2 (0..30 degrees) or 2.5 (45..90)
 * with "??" between, gouraud 60 more a segment: read in CPU cycles it fits all four of ours within ~5%. A refit needs
 * the angles between and the lengths, and nocash's "??" band is unmeasured anywhere.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; each line drawn four times, 20,000 cycles after a VBlank
 * IRQ (the vertical blanking: no scan-out toll, gpu-vbl2), timed from the first GP0 write to GPUSTAT bits 28+26; the
 * fastest printed. Lines from (0,16), 256 pixels along the major axis, at rises 0, 16, 32, 48, 64, 85, 100, 128, 148,
 * 170, 200, 228, 256 (0 to 45 degrees) and runs 228 ... 0 with a rise of 256 (45 to 90); flat (40h), gouraud (50h)
 * and semi (42h) at each. Then lengths 16, 64, 128, 512 at 0, 18 and 90 degrees (flat); then a poly-line (48h) of 2, 4
 * and 8 segments of 64 pixels across, and of 4 diagonal ones.
 *
 * READ IT LIKE THIS. Cycles by angle: nocash's 2-a-pixel jump off the horizontal and his per-scanline step; by length:
 * the per-pixel and per-segment terms. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-lines --frames=5000 --quiet=5000`.
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

static uint32_t W[24];
static int nW;
static uint32_t timeWords() {
    uint32_t best = 0xFFFFFFFFu;
    for (int r = 0; r < 4; r++) {
        waitReady();
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
        const uint32_t t0 = now();
        for (int i = 0; i < nW; i++) write32(GP0, W[i]);
        uint32_t t;
        for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 40000000u) break; }
        best = t < best ? t : best;
    }
    return best;
}
/* a single line from (0,16) to (dx, 16+dy), kind 0 flat, 1 gouraud, 2 semi */
static uint32_t line(int dx, int dy, int kind) {
    nW = 0;
    if (kind == 1) { W[nW++] = 0x50204080u; W[nW++] = xy(0, 16); W[nW++] = 0x00804020u; W[nW++] = xy(dx, 16 + dy); }
    else { W[nW++] = kind == 2 ? 0x42204080u : 0x40204080u; W[nW++] = xy(0, 16); W[nW++] = xy(dx, 16 + dy); }
    return timeWords();
}

int main() {
    printf("\ngpu/gpu-lines\n");
    printf("Lines in the vertical blanking, fastest of four, cycles from the first word. No assertions.\n\n");
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
    static const int RISE[13] = { 0, 16, 32, 48, 64, 85, 100, 128, 148, 170, 200, 228, 256 };
    static const int RUN[8] = { 228, 200, 170, 148, 128, 100, 64, 0 };
    ExitCriticalSection();
    printf("by angle (256 along the major axis):   dx   dy    flat  gouraud   semi\n");
    EnterCriticalSection();
    for (int i = 0; i < 13 + 8; i++) {
        const int dx = i < 13 ? 255 : RUN[i - 13], dy = i < 13 ? RISE[i] : 255;
        const uint32_t f = line(dx, dy, 0), g = line(dx, dy, 1), s = line(dx, dy, 2);
        ExitCriticalSection();
        printf("                                     %4d %4d  %6u  %6u  %6u\n", dx, dy, (unsigned)f, (unsigned)g, (unsigned)s);
        EnterCriticalSection();
    }
    static const int LEN[4] = { 16, 64, 128, 512 };
    ExitCriticalSection();
    printf("\nby length (flat):   len  across   ~18deg     down\n");
    EnterCriticalSection();
    for (int i = 0; i < 4; i++) {
        const int L = LEN[i];
        const uint32_t a = line(L - 1, 0, 0), b = line(L - 1, (L * 100 + 128) / 256 - 1 < 0 ? 0 : (L * 100 + 128) / 256 - 1, 0), c = line(0, L > 480 ? 479 : L - 1, 0);
        ExitCriticalSection();
        printf("                   %4d  %6u   %6u   %6u\n", L, (unsigned)a, (unsigned)b, (unsigned)c);
        EnterCriticalSection();
    }
    ExitCriticalSection();
    printf("\npoly-lines (48h), segments of 64:  n  across  diagonal\n");
    EnterCriticalSection();
    static const int NS[3] = { 2, 4, 8 };
    for (int j = 0; j < 3; j++) {
        uint32_t r[2];
        for (int d = 0; d < 2; d++) {
            nW = 0; W[nW++] = 0x48204080u;
            for (int k = 0; k <= NS[j]; k++) W[nW++] = d ? xy(k * 63, 16 + ((k & 1) ? 63 : 0)) : xy(k * 63, 16 + (k & 1));
            W[nW++] = 0x55555555u;
            r[d] = timeWords();
        }
        ExitCriticalSection();
        printf("                                   %d  %6u  %6u\n", NS[j], (unsigned)r[0], (unsigned)r[1]);
        EnterCriticalSection();
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-lines: Done.\n");
    for (;;) VSync(0);
}
