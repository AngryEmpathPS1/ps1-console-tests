/*
 * gpu/gpu-scan4 - the scan-out's toll on more primitive kinds, inside and outside the picture, in four more modes
 * (2026-10-13 night).
 *
 * WHY. hwtests/gpu-scan3 on the SCPH-7501, the active lines' toll over the blanking by primitive and mode: a gouraud
 * triangle 1-2% in every mode, a semi rectangle 6.4% (256x240) to 18.6% (640x480i 24-bit), flat rectangles, sprites,
 * textured triangles and copies between. A model needs the rest of a game's primitives, and whether the toll is the
 * picture's BYTES a line (gpu-vbl3: a narrower GP1(06h) range costs less, its place nothing) - 320x240 24-bit fetches
 * 960 bytes, between 512x240's 1,024 and 320x240's 640 - and whether drawing outside the displayed VRAM pays the same.
 *
 * WHAT IT DOES. As gpu-scan3: interrupts off, Timer 2 at the system clock, a texture page at (512,256) and a CLUT at
 * (0,480) opaque; each primitive four times in the blanking (20,000 cycles after the VBlank IRQ) and four on active
 * lines (200,000), the fastest of each, timed from the first GP0 write to GPUSTAT bits 28+26. Modes 320x240 15-bit,
 * 368x240 15-bit, 320x240 24-bit, 640x240 24-bit (from (0,0), range 16..256, 260h..260h+2560). Primitives: semi rect
 * 256x32 at (0,16) and at (512,300) (outside the picture), flat rect 256x32 at (512,300), fill 512x128 at (0,256), a
 * flat and a gouraud line (0,16)-(255,100), a semi 15-bit sprite 64x64, a 4-bit textured triangle and a gouraud 15-bit
 * textured one (~128x64), a gouraud quad 128x64, sixteen flat 16x16 rectangles and sixteen 15-bit 16x16 sprites, each
 * written when the FIFO is empty (GPUSTAT bit 28, M223 - 48 words at once overflow it), back to
 * back.
 *
 * READ IT LIKE THIS. Each row's toll (tenths of a %) against gpu-scan3's for the same kind; by mode against the bytes a
 * line. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scan4 --frames=3000`.
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

struct Prim { const char* name; uint32_t w[48]; int n; int depth; int per; };
static Prim P[12];
static int nP = 0;
static Prim& add(const char* name, int depth) { Prim& p = P[nP++]; p.name = name; p.n = 0; p.depth = depth; p.per = 0; return p; }

static uint32_t timeOne(const Prim& p, uint32_t x) {
    waitReady();
    write32(GP0, 0xE1000400u | tpage(p.depth < 0 ? 2 : p.depth));
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
    printf("\ngpu/gpu-scan4\n");
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
    { Prim& p = add("semi rect in", -1); p.w[p.n++] = 0x62204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("semi rect out", -1); p.w[p.n++] = 0x62204080u; p.w[p.n++] = xy(512, 300); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("flat rect out", -1); p.w[p.n++] = 0x60204080u; p.w[p.n++] = xy(512, 300); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("fill 512x128 out", -1); p.w[p.n++] = 0x02204080u; p.w[p.n++] = xy(0, 256); p.w[p.n++] = xy(512, 128); }
    { Prim& p = add("flat line", -1); p.w[p.n++] = 0x40204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(255, 100); }
    { Prim& p = add("gouraud line", -1); p.w[p.n++] = 0x50204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = 0x00804020u; p.w[p.n++] = xy(255, 100); }
    { Prim& p = add("semi sprite 15 64", 2); p.w[p.n++] = 0x67808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(64, 64); }
    { Prim& p = add("textured tri 4", 0); p.w[p.n++] = 0x25808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(128, 16); p.w[p.n++] = uv(127, 0, tpage(0)); p.w[p.n++] = xy(0, 80); p.w[p.n++] = uv(0, 63, 0); }
    { Prim& p = add("gouraud tex tri 15", 2); p.w[p.n++] = 0x34808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = 0x00406080u; p.w[p.n++] = xy(128, 16); p.w[p.n++] = uv(127, 0, tpage(2)); p.w[p.n++] = 0x00806040u; p.w[p.n++] = xy(0, 80); p.w[p.n++] = uv(0, 63, 0); }
    { Prim& p = add("gouraud quad", -1); p.w[p.n++] = 0x38204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = 0x00804020u; p.w[p.n++] = xy(128, 16); p.w[p.n++] = 0x00408020u; p.w[p.n++] = xy(0, 80); p.w[p.n++] = 0x00202080u; p.w[p.n++] = xy(128, 80); }
    { Prim& p = add("16 flat 16x16", -1); p.per = 2; for (int i = 0; i < 16; i++) { p.w[p.n++] = 0x78204080u; p.w[p.n++] = xy((i & 7) * 32, 16 + (i >> 3) * 32); } }
    { Prim& p = add("16 sprites 16x16", 2); p.per = 3; for (int i = 0; i < 16; i++) { p.w[p.n++] = 0x7D808080u; p.w[p.n++] = xy((i & 7) * 32, 16 + (i >> 3) * 32); p.w[p.n++] = uv((i & 7) * 16, (i >> 3) * 16, CLUT); } }
    static const char* const MN[4] = { "320x240", "368x240", "320x240 24-bit", "640x240 24-bit" };
    static const uint32_t MODE[4] = { 0x01, 0x40, 0x11, 0x13 };
    for (int m = 0; m < 4; m++) {
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
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-scan4: Done.\n");
    for (;;) VSync(0);
}
