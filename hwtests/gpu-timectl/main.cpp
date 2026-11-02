/*
 * gpu/gpu-timectl - GP0(03h), "Timing Control", against every kind of drawing and every size it could hide in (2026-11-02)
 *
 * WHY. psx-spx: the new GPU's GP0(03h) low 4 bits "affect the GPU rendering speed (00h/01h for 100%, downto 0Fh=25%) (probably
 * related to GPU clock or VRAM access or VRAM refresh timings)"; a 3 x 128 rectangle about 4x slower at 0Fh, a 512 x 128 one
 * not slower; "verify if default is 00h/01h". hwtests/gpu-version timed two sizes on seven consoles, alike to a few cycles:
 * 3 x 128 724 at 0, 654 at 1, then about a cycle more a row for each step to 2,382 at 0Fh; 64 x 64 2,296 .. 3,421. This core
 * does not model the command. Two sizes cannot say where the cost goes as a rectangle widens, or what else it slows.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock. Saves VRAM 512-1023 x 256-383 (DMA) and puts it back after. First,
 * with GP0(03h) as the BIOS left it: a flat 3 x 128 and 64 x 64 (to set beside the 00h and 01h rows: the default). Then for each
 * value 0..0Fh: flat rectangles (GP0(60h)) of width 1 2 3 4 8 16 24 32 48 64 128 256 512 at height 32; width 3 at heights 1, 8,
 * 128; a textured 64 x 64 rectangle (GP0(64h), a 15-bit page); a 64 x 64 fill (GP0(02h)); a 64 x 64 VRAM copy (GP0(80h)); a
 * flat and a gouraud triangle (GP0(20h), GP0(30h)) of half 64 x 64. Each the fastest and slowest of three, timed from the
 * command's last word to GPUSTAT bits 26 and 28 both set (gpu-version's way). At the end GP0(03h) is set to the value whose 3 x 128 and
 * 64 x 64 are nearest the default's, and named: the console is left as it was found, whatever its boot gave it.
 * Rows: D kind w h min max (the default); R v kind w h min max. Kinds: rect tex fill copy tri gtri.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-timectl`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void gp0(uint32_t w) { write32(GP0, w); }
static void gp1(uint32_t w) { write32(GP1, w); }
static uint32_t gstat() { return read32(GP1); }
static void waitIdle() { for (int i = 0; i < 4000000 && (gstat() & 0x14000000u) != 0x14000000u; i++) {} }   /* 26 and 28 */

static uint32_t saved[512 * 128 / 2];
static void dma(uint32_t* p, int words, bool toGpu) {
    gp1(toGpu ? 0x04000002u : 0x04000003u);
    write32(D2_MADR, (uint32_t)p & 0x1FFFFF);
    write32(D2_BCR, ((uint32_t)(words / 16) << 16) | 16u);
    write32(D2_CHCR, toGpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 8000000 && (read32(D2_CHCR) & 0x01000000u); i++) {}
    gp1(0x04000000u);
}
static void save() {
    waitIdle();
    gp0(0xC0000000u); gp0((256u << 16) | 512u); gp0((128u << 16) | 512u);
    for (int i = 0; i < 4000000 && !(gstat() & (1u << 27)); i++) {}
    dma(saved, 512 * 128 / 2, false);
    waitIdle();
}
static void restore() {
    waitIdle();
    gp0(0xA0000000u); gp0((256u << 16) | 512u); gp0((128u << 16) | 512u);
    dma(saved, 512 * 128 / 2, true);
    waitIdle();
}

enum { K_RECT, K_TEX, K_FILL, K_COPY, K_TRI, K_GTRI };
static const char *const KIND[] = { "rect", "tex", "fill", "copy", "tri", "gtri" };
/* One draw, its last word at t0: cycles until the GPU is idle. Everything inside 512-1023 x 256-383. */
static uint32_t once(int kind, uint32_t w, uint32_t h) {
    waitIdle();
    const uint32_t x = 512, y = 256;
    uint32_t t0 = 0;
    switch (kind) {
        case K_RECT: gp0(0x60404040u); gp0((y << 16) | x); t0 = now(); gp0((h << 16) | w); break;
        case K_TEX:  gp0(0x64808080u); gp0((y << 16) | x); gp0((0u << 8) | 0u); t0 = now(); gp0((h << 16) | w); break;
        case K_FILL: gp0(0x02102030u); gp0((y << 16) | x); t0 = now(); gp0((h << 16) | w); break;
        case K_COPY: gp0(0x80000000u); gp0((y << 16) | (x + 256)); gp0((y << 16) | x); t0 = now(); gp0((h << 16) | w); break;
        case K_TRI:  gp0(0x20404040u); gp0((y << 16) | x); gp0((y << 16) | (x + w)); t0 = now(); gp0(((y + h) << 16) | x); break;
        case K_GTRI: gp0(0x30400000u); gp0((y << 16) | x); gp0(0x00004000u); gp0((y << 16) | (x + w)); gp0(0x00400000u); t0 = now(); gp0(((y + h) << 16) | x); break;
    }
    while ((gstat() & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {}
    return now() - t0;
}
static uint32_t last;
static void row(char tag, int v, int kind, uint32_t w, uint32_t h) {
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (int k = 0; k < 3; k++) { const uint32_t c = once(kind, w, h); if (c < lo) lo = c; if (c > hi) hi = c; }
    last = lo;
    if (tag == 'D') printf("D %s %d %d %d %d\n", KIND[kind], (int)w, (int)h, (int)lo, (int)hi);
    else printf("R %d %s %d %d %d %d\n", v, KIND[kind], (int)w, (int)h, (int)lo, (int)hi);
}

int main() {
    printf("\ngpu/gpu-timectl\n");
    printf("GP0(03h) Timing Control against drawing kinds and sizes. D: as the BIOS left it; R v: after GP0(03h) = v.\n");
    printf("Each row: kind w h fastest slowest of 3, cycles from the last word to idle. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);                  /* DMA2 on */
    save();
    /* a plain state: the whole of VRAM to draw in, no offset, no window, page 512,256 15-bit for the textured one */
    gp0(0xE3000000u); gp0(0xE4000000u | (511u << 10) | 1023u); gp0(0xE5000000u);
    gp0(0xE1000000u | 8u | (1u << 4) | (2u << 7)); gp0(0xE2000000u); gp0(0xE6000000u);
    row('D', 0, K_RECT, 3, 128); const uint32_t d3 = last;
    row('D', 0, K_RECT, 64, 64); const uint32_t d64 = last;
    uint32_t best = 0xFFFFFFFFu; int bestV = 0;
    static const uint16_t W[] = { 1, 2, 3, 4, 8, 16, 24, 32, 48, 64, 128, 256, 512 };
    for (int v = 0; v < 16; v++) {
        waitIdle();
        gp0(0x03000000u | (uint32_t)v);
        waitIdle();
        for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) row('R', v, K_RECT, W[i], 32);
        row('R', v, K_RECT, 3, 1);
        row('R', v, K_RECT, 3, 8);
        row('R', v, K_RECT, 3, 128); const uint32_t r3 = last;
        row('R', v, K_TEX, 64, 64);
        row('R', v, K_FILL, 64, 64);
        row('R', v, K_COPY, 64, 64);
        row('R', v, K_TRI, 64, 64);
        row('R', v, K_GTRI, 64, 64);
        row('R', v, K_RECT, 64, 64); const uint32_t r64 = last;
        const uint32_t e = (r3 > d3 ? r3 - d3 : d3 - r3) + (r64 > d64 ? r64 - d64 : d64 - r64);
        if (e < best) { best = e; bestV = v; }
    }
    waitIdle();
    gp0(0x03000000u | (uint32_t)bestV);   /* as found: the value whose times the default's are nearest */
    printf("default nearest value %d (off by %d cycles over 3 x 128 and 64 x 64); left at it\n", bestV, (int)best);
    restore();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) { }
}
