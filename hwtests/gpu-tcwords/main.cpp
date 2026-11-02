/*
 * gpu/gpu-tcwords - GP0(03h) with every bit of its parameter, and with the nine 03xxxxxxh words a lost DMA header could have sent (2026-11-02)
 *
 * WHY. The first SCPH-5501's slow boot of 2026-11-01 came after hwtests/dma-len's first build hung it four times: that build's wait for the GPU
 * gave up without resetting the command buffer, so a transfer that lost words left the GPU inside it and the next transfer's buffer,
 * 0x9E3779B9 x (i + 1), was read as commands. Nine of its words are 03xxxxxxh. hwtests/gpu-timectl tried GP0(03h) = 0..0Fh only, and none of
 * those matched the slow state's price (console-logs/2026-11-01-SCPH-5501-slowvram against 2026-10-16: flat 8 x 8 +16, 16 x 16 +32,
 * 64 x 64 +128, 8 x 128 +256, 4 x 64 +65, 64 x 4 +8, 1 x 1 +2; sprites the same, semi-transparent nothing). A whole word sets bits 4-23 too.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock; VRAM 512-1023 x 256-383 saved and put back. For each value - 0, each single bit 4..23,
 * and the nine words - GP0(03h) = value, then the fastest of three of each shape, timed from the command's last word to GPUSTAT bits 26 and 28
 * both set (gpu-timectl's way): flat (60h) 1x1 4x4 8x8 16x16 32x32 64x64 8x128 4x64 64x4, a 15-bit sprite (65h) 64x4 and 16x16, a
 * semi-transparent (62h) 16x16. First the same shapes as found, before any GP0(03h) (the A row: run it in a slow state to name the value that
 * matches). Last, GP0(03h) = 0, which hwtests/gpu-timectl read this console's boot as.
 * Rows: A as-found t1 t2 ..., V value t1 t2 ... in the shape order of the header line.
 *
 * There are no assertions - compare the rows with value 0's.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-tcwords`.
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

enum { K_FLAT, K_SPRITE, K_SEMI };
struct Shape { uint8_t kind; uint8_t w, h; };
static const Shape SH[] = { {K_FLAT,1,1}, {K_FLAT,4,4}, {K_FLAT,8,8}, {K_FLAT,16,16}, {K_FLAT,32,32}, {K_FLAT,64,64}, {K_FLAT,8,128}, {K_FLAT,4,64},
                            {K_FLAT,64,4}, {K_SPRITE,64,4}, {K_SPRITE,16,16}, {K_SEMI,16,16} };
static const int NSH = sizeof SH / sizeof SH[0];
/* One draw, its last word at t0: cycles until the GPU is idle. Everything inside 512-1023 x 256-383; the sprite reads 15-bit texels at (768,256). */
static uint32_t once(const Shape& s) {
    waitIdle();
    const uint32_t x = 512, y = 256, w = s.w, h = s.h;
    uint32_t t0 = 0;
    if (s.kind == K_FLAT) { gp0(0x60404040u); gp0((y << 16) | x); t0 = now(); gp0((h << 16) | w); }
    else if (s.kind == K_SEMI) { gp0(0x62404040u); gp0((y << 16) | x); t0 = now(); gp0((h << 16) | w); }
    else { gp0(0x65808080u); gp0((y << 16) | x); gp0(0u); t0 = now(); gp0((h << 16) | w); }
    while ((gstat() & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {}
    return now() - t0;
}
static void shapes() {
    for (int i = 0; i < NSH; i++) {
        uint32_t lo = 0xFFFFFFFFu;
        for (int k = 0; k < 3; k++) { const uint32_t c = once(SH[i]); if (c < lo) lo = c; }
        printf(" %d", (int)lo);
    }
    printf("\n");
}
static void line(uint32_t v) {
    waitIdle();
    gp0(0x03000000u | (v & 0xFFFFFFu));
    waitIdle();
    printf("V %06x", (unsigned)(v & 0xFFFFFFu));
    shapes();
}
static const uint32_t WORDS[9] = { 0x035e2a92u, 0x03dbf3f3u, 0x03106c03u, 0x038e3564u, 0x034076d5u, 0x03be4036u, 0x037081a7u, 0x03ee4b08u, 0x0322c318u };

int main() {
    printf("\ngpu/gpu-tcwords\n");
    printf("GP0(03h) = V, then cycles from the last word to idle, fastest of 3: flat 1x1 4x4 8x8 16x16 32x32 64x64 8x128 4x64 64x4, sprite 64x4 16x16, semi 16x16.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);                  /* DMA2 on */
    save();
    /* a plain state: the whole of VRAM to draw in, no offset, no window; texture page (768,256) 15-bit for the sprite */
    gp0(0xE3000000u); gp0(0xE4000000u | (511u << 10) | 1023u); gp0(0xE5000000u);
    gp0(0xE1000000u | 12u | (1u << 4) | (2u << 7)); gp0(0xE2000000u); gp0(0xE6000000u);
    printf("A as-found");   /* before any GP0(03h): the state the console was in, to set beside the V rows */
    shapes();
    line(0);
    for (int b = 4; b < 24; b++) line(1u << b);
    for (int i = 0; i < 9; i++) line(WORDS[i]);
    line(0);
    waitIdle();
    restore();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("GP0(03h) left at 0\nDone.\n");
    for (;;) { }
}
