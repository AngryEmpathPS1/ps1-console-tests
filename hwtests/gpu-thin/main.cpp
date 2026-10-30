/*
 * gpu/gpu-thin - how long the GPU takes over THIN shapes: copies, rectangles, quads and fills a few pixels wide and
 * a few hundred tall, at Sony's widths.
 *
 * WHY (v0.377.70). REVC.PDF (Sony, 95/07/25) gives the one official GPU timing table there is: Revision C's new
 * frame-buffer memory - psx-spx's new GPU - runs MoveImage at "100%" / "85%" / "77%" / "70%" of the old speed at 32 /
 * 16 / 8 / 4 dots wide, and its Japanese original adds tall narrow DRAWING slower too (docs/SDK-TECHREF-SURVEY.md, item
 * 15). This core's copy is a set-up, a row and a pixel (v0.377.24, fitted to gpu-cost-sweep's 16x16 to 1024x16 on the
 * new GPU) and the old GPU's are those terms times its own (GPU_OLD, fitted to the SCPH-1001): together they give
 * 100.0% / 84.1% / 66.7% / 51.5% - right at 32 and 16, too steep below, where no copy was ever timed. And the
 * rectangle's row is not one cost: gpu-version's 3 x 128 reads 956 cycles on the 1001 and 724 on the new GPU, and no
 * fit of set-up + rows + pixels holds it beside the sweep's squares. This times every width Sony names, at two heights
 * so a row's cost separates from the set-up, and at two places in a 16-pixel block.
 *
 * WHAT IT DOES. gpu-cost-sweep's method: interrupts off, Timer 2 at the system clock unwrapped in software; for each
 * primitive wait for GPUSTAT bits 26 and 28, write its words to GP0 from the CPU, poll GPUSTAT for bit 28 and then bit
 * 26; four runs each, the fastest and slowest printed. Drawing area all of VRAM, no offset, no mask; textures at X 512,
 * Y 0 with a CLUT at (0, 480) as in the sweep.
 *   COPY      80h from (0,0) to (512,256), widths 1, 2, 4, 8, 16, 32, 64 at heights 128 and 256; widths 4 and 16 also
 *             from X 5 to X 517 (inside a 16-pixel block, crossing one)
 *   RECTANGLE 60h at (0,0), widths 1, 2, 3, 4, 8, 16, 32, 64 at heights 128 and 256; widths 3 and 8 at X 13 (crossing
 *             a 16-pixel block) and widths 3 at X 1021 (gpu-version's); semi (62h) widths 4, 16, 64 at 256; textured
 *             (64h) 4-bit and 15-bit widths 4, 16, 64 at 256
 *   QUAD      28h flat, widths 4, 8, 16, 32 at 256 tall
 *   FILL      02h widths 16, 32, 48, 64 at 256 tall (a fill's width is in 16s)
 *
 * READ IT LIKE THIS. Cycles at 33,868,800 a second, from the first word. A row's cost is the 256 row less the 128
 * row, over 128; the set-up is what is left. The poll loop reads in steps of about 45 cycles, so every row here is
 * made long (most over 1,500 cycles). No assertions.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.70 (bit 26, fastest; the new GPU, then the old as the SCPH-1001), each
 * row what the source's constants give (the new 16 x 256 copy: 141 + 256 x (24 + 1.98 x 16) ticks, 9,161 cycles, and
 * the poll's ~120):
 *   COPY 256 tall, 1 / 2 / 4 / 8 / 16 / 32 / 64 wide: 4,437 / 4,759 / 5,387 / 6,711 / 9,283 / 14,423 / 24,751 new;
 *     1,617 / 2,013 / 2,853 / 4,495 / 7,802 / 14,419 / 27,623 old (the old at 32 wide the new's: REVC.PDF's 100%). X 5
 *     as X 0: the model has no 16-pixel block.
 *   RECTANGLE flat 256 tall, 1 / 3 / 8 / 64 wide: 982 / 1,247 / 1,911 / 9,227 new; 1,700 / 1,955 / 2,619 / 9,758 old; X 13
 *     and X 1021 as X 0. semi 4 / 64 wide 1,691 / 14,179 new, 3,155 / 26,551 old.
 *   QUAD flat 4 / 32 wide 802 / 4,689 new, 3,087 / 6,924 old (the old GPU's row on polygons). FILL 16 / 64 wide 1,778 /
 *     2,616 new, 2,166 / 2,926 old.
 * THE CONSOLE (the SCPH-7501, 2026-10-10, three runs within a poll; console-logs/2026-10-10-SCPH-7501-night):
 *   COPY 256 tall: 6,213 / 6,215 / 6,713 / 8,068 / 10,686 / 15,879 / 25,140 - 1 and 2 wide the same, a thin row far
 *     dearer than this model's; X 5: 4 wide 7,386, 16 wide 11,586 (a block more).
 *   RECTANGLE flat 256 tall: 1 / 2 / 3 wide 1,363-1,368 (a floor), 4 wide 1,547, 8 1,815, 64 9,449; X 13 and 1021 as X 0.
 *     semi 4 / 16 / 64 wide 2,629 / 5,338 / 15,142; textured 4-bit 3,143 / 4,498 / 18,043 (thin ones CHEAPER than this
 *     model), 15-bit 3,322 / 12,039 / 44,397.
 *   QUAD flat 4 / 8 / 16 / 32 wide: 2,454 / 2,832 / 3,651 / 5,375 - three times this model's 802 at 4 wide.
 *   FILL 16 / 32 / 48 / 64 wide: 1,765 / 2,080 / 2,353 / 2,666 - this model's.
 * AT v0.377.71 (GPU_FLAT_ROW, GPU_RECTF_ROW, GPU_COPY2_ROW): copies within 2% but 64 wide (+3.7%), flat rectangles within
 *   3% but 4 wide (-11%), quads 2,445 / 2,860 / 3,548 / 5,498; semi and textured rectangles as before (KNOWN-OPEN).
 * `node hwtests/run.js gpu-thin`
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}

struct Tm { uint32_t t28, t26; };
static Tm drawOnce(const uint32_t* w, int n) {
    waitIdle();
    const uint32_t t0 = now();
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    Tm r = { 0, 0 };
    bool s28 = false;
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if (!s28 && (s & 0x10000000u)) { s28 = true; r.t28 = t; }
        if ((s & 0x04000000u) && s28) { r.t26 = t; break; }
        if (t > 40000000u) { r.t26 = 0xFFFFFFFFu; break; }
    }
    return r;
}

static void row(const uint32_t* w, int n) {
    uint32_t lo28 = 0xFFFFFFFFu, hi28 = 0, lo26 = 0xFFFFFFFFu, hi26 = 0;
    for (int k = 0; k < 4; k++) {
        const Tm t = drawOnce(w, n);
        lo28 = t.t28 < lo28 ? t.t28 : lo28; hi28 = t.t28 > hi28 ? t.t28 : hi28;
        lo26 = t.t26 < lo26 ? t.t26 : lo26; hi26 = t.t26 > hi26 ? t.t26 : hi26;
    }
    printf("   %2d words   bit 28 %7u..%-7u  bit 26 %8u..%u\n", n, (unsigned)lo28, (unsigned)hi28, (unsigned)lo26, (unsigned)hi26);
}

static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;                  /* (0, 480) */
static uint32_t tpage(int depth, int semi) { return 8u | ((uint32_t)semi << 5) | ((uint32_t)depth << 7); }   /* X 512 */
static void drawMode(int depth) { write32(GP0, 0xE1000000u | (1u << 10) | tpage(depth, 0)); }

static uint32_t w[16];

static void copy(int x, int wd, int h) {
    w[0] = 0x80000000u; w[1] = xy(x, 0); w[2] = xy(512 + x, 256); w[3] = xy(wd, h);
    printf("  %2dx%-3d at X %d", wd, h, x); row(w, 4);
}
static void rect(uint32_t cmd, int x, int wd, int h, const char* what) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x204080u; w[n++] = xy(x, 0);
    if (cmd & 4) w[n++] = uv(0, 0, CLUT);
    w[n++] = xy(wd, h);
    printf("  %s %2dx%-3d at X %d", what, wd, h, x); row(w, n);
}

int main() {
    printf("\ngpu/gpu-thin\n");
    printf("Thin shapes written to GP0 by the CPU, then GPUSTAT polled: when bit 28 and bit 26 read set,\n");
    printf("cycles from the first word, fastest..slowest of four. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x01000000u);                                  /* reset the command buffer */
    write32(GP1, 0x04000000u);                                  /* no DMA */
    write32(GP0, 0xE2000000u);                                  /* no texture window */
    write32(GP0, 0xE3000000u);                                  /* drawing area (0, 0) .. */
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);           /* .. (1023, 511) */
    write32(GP0, 0xE5000000u);                                  /* no offset */
    write32(GP0, 0xE6000000u);                                  /* no mask */
    drawMode(2);

    static const int cw[] = { 1, 2, 4, 8, 16, 32, 64 };
    printf("COPY (80h), from (X,0) to (512+X,256):\n");
    for (int h = 128; h <= 256; h += 128)
        for (int i = 0; i < 7; i++) copy(0, cw[i], h);
    copy(5, 4, 256); copy(5, 16, 256);

    static const int rw[] = { 1, 2, 3, 4, 8, 16, 32, 64 };
    printf("RECTANGLE (60h flat, 62h semi-transparent, 64h textured):\n");
    for (int h = 128; h <= 256; h += 128)
        for (int i = 0; i < 8; i++) rect(0x60, 0, rw[i], h, "flat");
    rect(0x60, 13, 3, 256, "flat"); rect(0x60, 13, 8, 256, "flat"); rect(0x60, 1021, 3, 256, "flat"); rect(0x60, 1021, 3, 128, "flat");
    static const int sw[] = { 4, 16, 64 };
    for (int i = 0; i < 3; i++) rect(0x62, 0, sw[i], 256, "semi");
    for (int depth = 0; depth < 3; depth += 2) {
        drawMode(depth);
        for (int i = 0; i < 3; i++) rect(0x64, 0, sw[i], 256, depth == 0 ? "textured  4-bit" : "textured 15-bit");
    }
    drawMode(2);

    printf("QUAD (28h flat):\n");
    static const int qw[] = { 4, 8, 16, 32 };
    for (int i = 0; i < 4; i++) {
        const int W = qw[i];
        w[0] = 0x28204080u; w[1] = xy(0, 0); w[2] = xy(W, 0); w[3] = xy(0, 256); w[4] = xy(W, 256);
        printf("  flat %2dx256", W); row(w, 5);
    }

    printf("FILL (02h):\n");
    for (int W = 16; W <= 64; W += 16) {
        w[0] = 0x02203040u; w[1] = xy(0, 0); w[2] = xy(W, 256);
        printf("  %2dx256", W); row(w, 3);
    }

    waitIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-thin: Done.\n");
    for (;;) VSync(0);
}
