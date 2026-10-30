/*
 * gpu/gpu-scanline2 - the scan-out's fetches along a line, at ~9-cycle resolution (2026-10-13 night).
 *
 * WHY. hwtests/gpu-scanline on the SCPH-7501: a flat 64x4 rectangle (187 cycles in the blanking) costs +90 for ~180
 * cycles of each active line (one big fetch), +45 at scattered points elsewhere (refills; many in 640x240 24-bit, two
 * in 320x240), nothing between - but its poll loop read Timer 2 every pass (~45 cycles a step) and its X was the
 * idle loop's (~20 cycles of jitter). A model of short primitives needs each fetch's place and length.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. For 640x240 24-bit, 640x240 15-bit, 320x240 15-bit and
 * 256x240 15-bit (range 16..256, 260h..260h+2560): a VBlank wait polled on I_STAT alone, then X idle cycles, X =
 * 200,000 + 9k (k = 0..239: a line is ~2,152.6) three times each, then the flat 64x4 rectangle at (0,16) with GPUSTAT
 * polled alone (bits 28+26) and Timer 2 read after. Each try is binned by its ACTUAL start (Timer 2 from the VBlank
 * seen to the first GP0 write), 9 cycles a bin from 200,000; a bin's fastest-slowest printed, eight bins a line ("-":
 * nothing landed there). Sixteen tries in the blanking (X = 20,000) first, their fastest-slowest.
 *
 * READ IT LIKE THIS. Each bin's cost above the blanking's: where along the line a fetch holds the drawing, by how much.
 * No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scanline2 --frames=8000`.
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

/* one try: returns the cost, *at = the actual start after the VBlank was seen */
static uint32_t probe(uint32_t x, uint32_t* at) {
    waitReady();
    write32(I_STAT, ~1u);
    { int n = 0; while (!(read32(I_STAT) & 1u) && n < 4000000) n++; }
    const uint32_t tv = now();
    while (now() - tv < x) {}
    const uint32_t t0 = now();
    write32(GP0, 0x60204080u); write32(GP0, xy(0, 16)); write32(GP0, xy(64, 4));
    { int n = 0; while ((read32(GP1) & 0x14000000u) != 0x14000000u && n < 4000000) n++; }
    const uint32_t t1 = now();
    *at = t0 - tv;
    return t1 - t0;
}
static uint16_t lo[240], hi[240];

int main() {
    printf("\ngpu/gpu-scanline2\n");
    printf("A flat 64x4 rectangle by its actual start after the VBlank IRQ, 9 cycles a bin from 200,000: fastest-slowest,\n");
    printf("eight bins a line. No assertions.\n\n");
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
    static const char* const MN[4] = { "640x240 24-bit", "640x240 15-bit", "320x240 15-bit", "256x240 15-bit" };
    static const uint32_t MODE[4] = { 0x13, 0x03, 0x01, 0x00 };
    for (int m = 0; m < 4; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        uint32_t blo = 0xFFFFFFFFu, bhi = 0, at;
        for (int r = 0; r < 16; r++) { const uint32_t t = probe(20000, &at); blo = t < blo ? t : blo; bhi = t > bhi ? t : bhi; }
        for (int k = 0; k < 240; k++) { lo[k] = 0xFFFF; hi[k] = 0; }
        for (int k = 0; k < 240; k++) for (int r = 0; r < 3; r++) {
            const uint32_t t = probe(200000u + 9u * (uint32_t)k, &at);
            if (at < 200000u) continue;
            const uint32_t b = (at - 200000u) / 9u;
            if (b >= 240u) continue;
            const uint16_t c = (uint16_t)(t > 65535u ? 65535u : t);
            if (c < lo[b]) lo[b] = c;
            if (c > hi[b]) hi[b] = c;
        }
        ExitCriticalSection();
        printf("%s: the blanking %u-%u; active lines from 200,000:\n", MN[m], (unsigned)blo, (unsigned)bhi);
        for (int k = 0; k < 240; k += 8) {
            printf("  %6u:", (unsigned)(200000u + 9u * (uint32_t)k));
            for (int j = k; j < k + 8; j++) { if (hi[j]) printf(" %u-%u", (unsigned)lo[j], (unsigned)hi[j]); else printf(" -"); }
            printf("\n");
        }
        printf("\n");
        EnterCriticalSection();
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("gpu/gpu-scanline2: Done.\n");
    for (;;) VSync(0);
}
