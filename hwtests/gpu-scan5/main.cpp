/*
 * gpu/gpu-scan5 - the scan-out's toll on the primitive kinds gpu-scan3 and gpu-scan4 left out (2026-10-13 night).
 *
 * WHY. gpu-scan3 / gpu-scan4 on the SCPH-7501 gave each primitive's active-line toll against a semi rectangle's (how
 * much of its drawing waits on VRAM): fill ~0.95, 15-bit sprites ~0.84, 15-bit textured triangles ~0.75, flat rect
 * ~0.68, flat triangle ~0.63, copy 128x32 ~0.52, 4-bit textured triangle ~0.36, gouraud ~0.1, lines 0. A model charges
 * every kind; these are the kinds a game draws that those two did not: semi-transparent polygons (flat, textured,
 * gouraud), 8-bit textured triangles, dithered gouraud, mask-checked rectangles, semi 4-bit sprites, small 8x8 sprites,
 * textured quads and a wider copy.
 *
 * WHAT IT DOES. As gpu-scan4 (interrupts off, Timer 2 at the system clock, a texture page at (512,256) and a CLUT at
 * (0,480) opaque, each primitive four times 20,000 cycles after the VBlank IRQ and four 200,000 after, the fastest of
 * each, timed from the first GP0 write to GPUSTAT bits 28+26; batches each into an empty FIFO, bit 28), in 320x240
 * (two refills a line), 512x240 (three, the runner's mode) and 640x240 24-bit (seven). Primitives: semi flat triangle,
 * semi 15-bit textured triangle, 8-bit textured triangle, semi gouraud triangle (all ~128x64 at (0,16)), dithered
 * gouraud triangle (E1 bit 9), mask-checked flat rect 256x32 (E6 bit 1), semi 4-bit sprite 64x64, sixteen 15-bit 8x8
 * sprites, a 15-bit textured quad 128x64, a copy 256x32.
 *
 * READ IT LIKE THIS. Each row's toll (tenths of a %) against the same mode's semi rectangle in gpu-scan3 / gpu-scan4
 * (320x240 7.9%, 512x240 10.0%, 640x240 24-bit 17.8%). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scan5 --frames=5000 --quiet=5000`.
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
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;
static uint32_t tpage(int depth) { return 8u | (1u << 4) | ((uint32_t)depth << 7); }   /* X 512, Y 256 */

struct Prim { const char* name; uint32_t w[48]; int n; int depth; int per; uint32_t e1; uint32_t e6; };
static Prim P[12];
static int nP = 0;
static Prim& add(const char* name, int depth) { Prim& p = P[nP++]; p.name = name; p.n = 0; p.depth = depth; p.per = 0; p.e1 = 0; p.e6 = 0; return p; }

static uint32_t timeOne(const Prim& p, uint32_t x) {
    waitReady();
    write32(GP0, 0xE1000400u | p.e1 | tpage(p.depth < 0 ? 2 : p.depth));
    write32(GP0, 0xE6000000u | p.e6);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < x) {} }
    const uint32_t t0 = now();
    for (int i = 0; i < p.n; i++) {
        if (p.per && i % p.per == 0) while (!(read32(GP1) & 0x10000000u) && now() - t0 < 40000000u) {}   /* a batch: each primitive into an empty FIFO (bit 28, M223) */
        write32(GP0, p.w[i]);
    }
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-scan5\n");
    printf("Each primitive in the vertical blanking (20,000 cycles after the VBlank IRQ) and on active lines (200,000), by\n");
    printf("display mode: fastest of four each, and the active lines' toll in tenths of a percent. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
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
    { Prim& p = add("semi flat tri", -1); p.w[p.n++] = 0x22204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(128, 16); p.w[p.n++] = xy(0, 80); }
    { Prim& p = add("semi tex tri 15", 2); p.w[p.n++] = 0x27808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(128, 16); p.w[p.n++] = uv(127, 0, tpage(2)); p.w[p.n++] = xy(0, 80); p.w[p.n++] = uv(0, 63, 0); }
    { Prim& p = add("tex tri 8", 1); p.w[p.n++] = 0x25808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(128, 16); p.w[p.n++] = uv(127, 0, tpage(1)); p.w[p.n++] = xy(0, 80); p.w[p.n++] = uv(0, 63, 0); }
    { Prim& p = add("semi gouraud tri", -1); p.w[p.n++] = 0x32204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = 0x00804020u; p.w[p.n++] = xy(128, 16); p.w[p.n++] = 0x00408020u; p.w[p.n++] = xy(0, 80); }
    { Prim& p = add("dither gouraud tri", -1); p.e1 = 0x200u; p.w[p.n++] = 0x30204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = 0x00804020u; p.w[p.n++] = xy(128, 16); p.w[p.n++] = 0x00408020u; p.w[p.n++] = xy(0, 80); }
    { Prim& p = add("mask flat rect", -1); p.e6 = 2u; p.w[p.n++] = 0x60204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("semi sprite 4 64", 0); p.w[p.n++] = 0x67808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(64, 64); }
    { Prim& p = add("16 sprites 8x8", 2); p.per = 3; for (int i = 0; i < 16; i++) { p.w[p.n++] = 0x75808080u; p.w[p.n++] = xy((i & 7) * 32, 16 + (i >> 3) * 32); p.w[p.n++] = uv((i & 7) * 8, (i >> 3) * 8, CLUT); } }
    { Prim& p = add("tex quad 15", 2); p.w[p.n++] = 0x2D808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(128, 16); p.w[p.n++] = uv(127, 0, tpage(2)); p.w[p.n++] = xy(0, 80); p.w[p.n++] = uv(0, 63, 0); p.w[p.n++] = xy(128, 80); p.w[p.n++] = uv(127, 63, 0); }
    { Prim& p = add("copy 256x32", -1); p.w[p.n++] = 0x80000000u; p.w[p.n++] = xy(512, 0); p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(256, 32); }
    static const char* const MN[3] = { "320x240", "512x240", "640x240 24-bit" };
    static const uint32_t MODE[3] = { 0x01, 0x02, 0x13 };
    for (int m = 0; m < 3; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int i = 0; i < nP; i++) {
            uint32_t b = 0xFFFFFFFFu, a = 0xFFFFFFFFu;
            for (int r = 0; r < 4; r++) { const uint32_t t1 = timeOne(P[i], 20000); b = t1 < b ? t1 : b; const uint32_t t2 = timeOne(P[i], 200000); a = t2 < a ? t2 : a; }
            ExitCriticalSection();
            printf("  %-15s %-19s blank %6u  active %6u  toll %d\n", MN[m], P[i].name, (unsigned)b, (unsigned)a, b ? ((int)a - (int)b) * 1000 / (int)b : 0);
            EnterCriticalSection();
        }
    }
    write32(GP0, 0xE6000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-scan5: Done.\n");
    for (;;) VSync(0);
}
