/*
 * gpu/gpu-scanline3 - the scan-out's fetches along a line as a semi rectangle, a fill and a copy meet them (2026-10-13
 * before dawn).
 *
 * WHY. v0.377.91 charges the scan-out's toll as an average over each line, a share of it by kind. Two kinds refuse that:
 * a FILL (amidog psxtest_gpu FIFO holds a fill's first few hundred cycles to a window both ways - any average toll on
 * them breaks it) and a COPY (gpu-copy3 / gpu-copy4: wide copies ~5% at 512x240 but under 1% at 320x240, more refills
 * notwithstanding). The console pays whole fetch bursts at their places in the line (gpu-scanline2, a flat 64x4
 * rectangle: one at the line's start, refills spaced by the picture's drain). A burst model needs each kind's own
 * response to a burst by where it lands.
 *
 * WHAT IT DOES. As gpu-scanline2 (interrupts off, Timer 2 at the system clock; a VBlank wait polled on I_STAT alone,
 * X idle cycles, the draw, GPUSTAT polled alone; each try binned by its ACTUAL start after the VBlank, 9 cycles a bin over
 * 2,160 from 200,000, three tries a step; sixteen in the blanking first) for a semi 64x4 rectangle at (0,16), a 64x4
 * fill at (0,16) and a 64x4 copy from (512,256) to (0,16), in 512x240 (three refills a line) and 320x240 (two).
 *
 * READ IT LIKE THIS. Each bin's cost above the blanking's, by kind: where a burst holds it and by how much. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scanline3 --frames=12000 --quiet=12000`.
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
static int nW;
/* one try: returns the cost, *at = the actual start after the VBlank was seen */
static uint32_t probe(uint32_t x, uint32_t* at) {
    waitReady();
    write32(I_STAT, ~1u);
    { int n = 0; while (!(read32(I_STAT) & 1u) && n < 4000000) n++; }
    const uint32_t tv = now();
    while (now() - tv < x) {}
    const uint32_t t0 = now();
    write32(GP0, W[0]); write32(GP0, W[1]); write32(GP0, W[2]); if (nW > 3) write32(GP0, W[3]);
    { int n = 0; while ((read32(GP1) & 0x14000000u) != 0x14000000u && n < 4000000) n++; }
    const uint32_t t1 = now();
    *at = t0 - tv;
    return t1 - t0;
}
static uint16_t lo[240], hi[240];

int main() {
    printf("\ngpu/gpu-scanline3\n");
    printf("A semi 64x4 rectangle, a 64x4 fill and a 64x4 copy by their actual start after the VBlank IRQ, 9 cycles a bin\n");
    printf("from 200,000: fastest-slowest, eight bins a line. No assertions.\n\n");
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
    static const char* const MN[2] = { "512x240", "320x240" };
    static const uint32_t MODE[2] = { 0x02, 0x01 };
    static const char* const KN[3] = { "semi 64x4", "fill 64x4", "copy 64x4" };
    for (int m = 0; m < 2; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int kind = 0; kind < 3; kind++) {
            if (kind == 0) { W[0] = 0x62204080u; W[1] = xy(0, 16); W[2] = xy(64, 4); nW = 3; }
            else if (kind == 1) { W[0] = 0x02204080u; W[1] = xy(0, 16); W[2] = xy(64, 4); nW = 3; }
            else { W[0] = 0x80000000u; W[1] = xy(512, 256); W[2] = xy(0, 16); W[3] = xy(64, 4); nW = 4; }
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
            printf("%s %s: the blanking %u-%u; active lines from 200,000:\n", MN[m], KN[kind], (unsigned)blo, (unsigned)bhi);
            for (int k = 0; k < 240; k += 8) {
                printf("  %6u:", (unsigned)(200000u + 9u * (uint32_t)k));
                for (int j = k; j < k + 8; j++) { if (hi[j]) printf(" %u-%u", (unsigned)lo[j], (unsigned)hi[j]); else printf(" -"); }
                printf("\n");
            }
            printf("\n");
            EnterCriticalSection();
        }
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("gpu/gpu-scanline3: Done.\n");
    for (;;) VSync(0);
}
