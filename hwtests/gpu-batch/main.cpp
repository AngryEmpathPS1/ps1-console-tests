/*
 * gpu/gpu-batch - what a primitive costs back to back, against alone: does the next one's set-up run under the one
 * drawing (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-scan4 / gpu-scan5 fed sixteen small primitives one at a time, each when GPUSTAT bit 28 (the FIFO
 * empty, M223) came back: sixteen 16x16 flat rectangles 2,957 cycles on the SCPH-7501 (~185 each) where this core
 * draws them back to back with no gap at its own 202 each, a single one sitting on the consoles (gpu-cmd-ready); sixteen
 * 15-bit 16x16 sprites 12,522 (783 each) against 871 here; the SCPH-1001 the same way (250 / 274 a rectangle). M175 found
 * a POLYGON set up while the one before it draws; psx-spx's FIFO prefetch lets the GPU take a rectangle's first words
 * early. Whether a rectangle's set-up (or a sprite's texture fetches) overlaps the drawing before it needs the run's
 * length varied.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; in the vertical blanking (20,000 cycles after the VBlank
 * IRQ), fastest of four: N = 1, 2, 4, 8, 16 primitives back to back, each primitive's words written when bit 28 reads
 * set, timed from the first word to bits 28+26. Kinds: a flat 16x16 rectangle (78h) and a flat 64x64 (60h), a semi 16x16
 * (7Ah), a 15-bit 16x16 sprite (7Dh) on the SAME texels each time and on fresh ones each time (u,v by i), a 4-bit 16x16
 * sprite (7Dh through a CLUT), and a flat triangle ~16x16 (20h). Texture page at (512,256), CLUT at (0,480), opaque.
 *
 * READ IT LIKE THIS. Each kind's cycles by N: the slope a primitive's cost in a run, against N = 1 (alone). A slope
 * under the single cost is overlap; how much is the set-up's (or the texture's). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-batch --frames=4000 --quiet=4000`.
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
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;
static uint32_t tpage(int depth) { return 8u | (1u << 4) | ((uint32_t)depth << 7); }   /* X 512, Y 256 */

/* the words of primitive i of kind k into w; returns how many */
static int prim(int k, int i, uint32_t* w) {
    const int x = (i & 7) * 32, y = 16 + (i >> 3) * 80;
    switch (k) {
    case 0: w[0] = 0x78204080u; w[1] = xy(x, y); return 2;
    case 1: w[0] = 0x60204080u; w[1] = xy(x, y); w[2] = xy(64, 64); return 3;
    case 2: w[0] = 0x7A204080u; w[1] = xy(x, y); return 2;
    case 3: w[0] = 0x7D808080u; w[1] = xy(x, y); w[2] = uv(0, 0, CLUT); return 3;
    case 4: w[0] = 0x7D808080u; w[1] = xy(x, y); w[2] = uv((i & 7) * 16, (i >> 3) * 16, CLUT); return 3;
    case 5: w[0] = 0x7D808080u; w[1] = xy(x, y); w[2] = uv((i & 7) * 16, (i >> 3) * 16, CLUT); return 3;
    default: w[0] = 0x20204080u; w[1] = xy(x, y); w[2] = xy(x + 16, y); w[3] = xy(x, y + 16); return 4;
    }
}
static uint32_t run(int k, int n) {
    uint32_t best = 0xFFFFFFFFu, w[4];
    for (int r = 0; r < 4; r++) {
        waitReady();
        write32(GP0, 0xE1000400u | tpage(k == 5 ? 0 : 2));
        write32(I_STAT, ~1u);
        { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
        { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
        const uint32_t t0 = now();
        for (int i = 0; i < n; i++) {
            const int nw = prim(k, i, w);
            while (!(read32(GP1) & 0x10000000u) && now() - t0 < 4000000u) {}
            for (int j = 0; j < nw; j++) write32(GP0, w[j]);
        }
        uint32_t t;
        for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 4000000u) break; }
        best = t < best ? t : best;
    }
    return best;
}

int main() {
    printf("\ngpu/gpu-batch\n");
    printf("N primitives back to back, each fed when GPUSTAT bit 28 reads set, in the blanking, fastest of four: cycles\n");
    printf("from the first word to bits 28+26. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    static const char* const KN[7] = { "flat 16x16", "flat 64x64", "semi 16x16", "sprite15 same", "sprite15 fresh", "sprite4 fresh", "flat tri 16" };
    static const int NN[5] = { 1, 2, 4, 8, 16 };
    ExitCriticalSection();
    printf("kind                 N=1      2      4      8     16\n");
    EnterCriticalSection();
    for (int k = 0; k < 7; k++) {
        uint32_t c[5];
        for (int j = 0; j < 5; j++) c[j] = run(k, NN[j]);
        ExitCriticalSection();
        printf("%-16s %6u %6u %6u %6u %6u\n", KN[k], (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3], (unsigned)c[4]);
        EnterCriticalSection();
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-batch: Done.\n");
    for (;;) VSync(0);
}
