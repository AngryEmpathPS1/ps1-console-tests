/*
 * gpu/gpu-scan3 - the scan-out's toll by primitive and display mode (2026-10-13 night).
 *
 * WHY. hwtests/gpu-vbl / gpu-vbl2 on the SCPH-7501: a draw on an active line is slower than the same draw in the vertical
 * blanking (the ~46,000 cycles after the VBlank IRQ) by what the picture's scan-out fetches a line - a semi 256x32
 * rectangle +7.3% at 256x240, +9.0% at 320x240, +10.9% at 512x240, +12.7% at 640x240 or 640x480i, +19.6% at 640x480i
 * 24-bit - and not by the same for every primitive: in the 512 mode a flat rectangle loses ~6.5%, a copy ~5%. A model
 * needs the toll by primitive kind.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. A texture page at (512,256) and a CLUT at (0,480) filled
 * opaque (7FFFh). For each display mode - 256x240, 320x240, 512x240, 640x240 15-bit, 640x480 interlaced 24-bit, from
 * (0,0), vertical range 16..256 - and each primitive, four tries each of: a VBlank wait, X idle cycles on Timer 2 (X =
 * 20,000: the blanking; 200,000: active lines), the primitive, timed to GPUSTAT bits 28+26. A row: mode, primitive,
 * blanking fastest, active fastest, the toll in tenths of a percent. Primitives (each ~2,000-12,000 cycles, inside
 * the blanking): flat rect 256x32, semi rect 256x32, fill 256x32, copy 128x32, flat triangle, gouraud triangle (both
 * ~128x64), 4-bit / 8-bit / 15-bit sprite 64x64 from the page, a 15-bit textured triangle.
 *
 * READ IT LIKE THIS. Each primitive's toll against the mode's bytes a line. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-scan3 --frames=3000`.
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

struct Prim { const char* name; uint32_t w[12]; int n; int depth; };
static Prim P[10];
static int nP = 0;
static Prim& add(const char* name, int depth) { Prim& p = P[nP++]; p.name = name; p.n = 0; p.depth = depth; return p; }

static uint32_t timeOne(const Prim& p, uint32_t x) {
    waitReady();
    write32(GP0, 0xE1000400u | tpage(p.depth < 0 ? 2 : p.depth));
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < x) {} }
    const uint32_t t0 = now();
    for (int i = 0; i < p.n; i++) write32(GP0, p.w[i]);
    for (;;) { const uint32_t t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) return t; if (t > 40000000u) return 0xFFFFFFFFu; }
}

int main() {
    printf("\ngpu/gpu-scan3\n");
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
    /* the texture page and the CLUT, opaque */
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    { Prim& p = add("flat rect 256x32", -1); p.w[p.n++] = 0x60204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("semi rect 256x32", -1); p.w[p.n++] = 0x62204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("fill 256x32", -1); p.w[p.n++] = 0x02204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(256, 32); }
    { Prim& p = add("copy 128x32", -1); p.w[p.n++] = 0x80000000u; p.w[p.n++] = xy(512, 0); p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(128, 32); }
    { Prim& p = add("flat triangle", -1); p.w[p.n++] = 0x20204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = xy(128, 16); p.w[p.n++] = xy(0, 80); }
    { Prim& p = add("gouraud triangle", -1); p.w[p.n++] = 0x30204080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = 0x00804020u; p.w[p.n++] = xy(128, 16); p.w[p.n++] = 0x00408020u; p.w[p.n++] = xy(0, 80); }
    static const int D[3] = { 0, 1, 2 };
    static const char* const SN[3] = { "sprite 4-bit 64x64", "sprite 8-bit 64x64", "sprite 15-bit 64x64" };
    for (int k = 0; k < 3; k++) { Prim& p = add(SN[k], D[k]); p.w[p.n++] = 0x65808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(64, 64); }
    { Prim& p = add("textured tri 15-bit", 2); p.w[p.n++] = 0x25808080u; p.w[p.n++] = xy(0, 16); p.w[p.n++] = uv(0, 0, CLUT); p.w[p.n++] = xy(128, 16); p.w[p.n++] = uv(127, 0, (uint32_t)tpage(2)); p.w[p.n++] = xy(0, 80); p.w[p.n++] = uv(0, 63, 0); }
    static const char* const MN[5] = { "256x240", "320x240", "512x240", "640x240", "640x480i 24-bit" };
    static const uint32_t MODE[5] = { 0x00, 0x01, 0x02, 0x03, 0x37 };
    for (int m = 0; m < 5; m++) {
        write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        for (int i = 0; i < nP; i++) {
            uint32_t b = 0xFFFFFFFFu, a = 0xFFFFFFFFu;
            for (int r = 0; r < 4; r++) { const uint32_t t1 = timeOne(P[i], 20000); b = t1 < b ? t1 : b; const uint32_t t2 = timeOne(P[i], 200000); a = t2 < a ? t2 : a; }
            ExitCriticalSection();
            printf("  %-15s %-20s blank %6u  active %6u  toll %d\n", MN[m], P[i].name, (unsigned)b, (unsigned)a, b ? ((int)a - (int)b) * 1000 / (int)b : 0);
            EnterCriticalSection();
        }
    }
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-scan3: Done.\n");
    for (;;) VSync(0);
}
