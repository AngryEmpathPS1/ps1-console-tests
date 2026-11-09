/*
 * gpu/gpu-tex-noscan - textured rectangles and quads timed with NO scan-out at all, and with the display on: the texel and
 * fill costs apart from the scan-out's tolls.
 *
 * WHY (2026-11-07). A big textured draw outlasts the vertical blanking, so every console row of one (gpu-cost-sweep,
 * gpu-tex-opaque, gpu/bandwidth) carries the scan-out's fetches as well as its own texels, fills and rows, and this core's
 * texture costs (GPU_TEX2, GPU_TEXR2) were fitted on rows like that beside its scan-out model (GPU_SCAN, GPU_SCAN_POS). Its
 * KNOWN-OPENs: gpu-cost-sweep's 8-bit 256x256 rectangle reads 2-4% cheap here; GPU_TEX2's batches and CPU-fed rows disagree
 * ~3% "the display or the feeding". hwtests/gpu-clut-ready used an EMPTY vertical display range (GP1(07h) with Y1 = Y2)
 * to keep the scan-out out - tried here against the display on. (RESULT, the second SCPH-5501: the empty range reads as
 * 320x240 on the console and here - psx-spx wants Y2 > Y1 - so E is not "no scan-out"; D512 against D320 is the toll.)
 *
 * WHAT IT DOES. gpu-tex-opaque's OPAQUE texture (15-bit texels 1234h; paletted texels 1111h, every CLUT entry 1234h; all
 * filled with GP0(02h) first, at (512,0), the CLUT at (768,500)) and its timing (interrupts off, the words from the CPU, then
 * GPUSTAT polled for bits 28 and 26 with Timer 2 unwrapped: cycles from the first word, fastest..slowest of four). Each try
 * starts 53 lines after the VBlank's start (a displayed line ~30 when the display is on). Drawn at (0,256): a flat 256x256
 * rectangle (60h, the control), then at 4, 8 and 15 bits a textured rectangle (64h) and a 1:1 textured quad (2Ch) at 64x64,
 * 128x128 and 256x256. Three times: E the vertical range empty (Y1 = Y2); D320 the display on, 320x240; D512 the
 * display on, 512x240.
 *
 * READ IT LIKE THIS. E is the draw alone; D320 - E and D512 - E are the scan-out's tolls, by kind, depth and size.
 *
 * ON THIS CORE (v0.377.302 + the CLUT proposal, `node hwtests/run.js gpu-tex-noscan --frames=20000 --quiet=15000`):
 * PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define ISTAT 0x1F801070u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}
static void waitVblank() { write32(ISTAT, ~1u); for (int i = 0; i < 2000000 && !(read32(ISTAT) & 1u); i++) { now(); } }
static void waitLines(int n) { for (int l = 0; l < n; l++) { const uint16_t t = read16(T1_COUNT); for (int i = 0; i < 100000 && read16(T1_COUNT) == t; i++) { now(); } } }

struct Tm { uint32_t t28, t26; };
static Tm drawOnce(const uint32_t* w, int n) {
    waitIdle();
    waitVblank(); waitLines(53);
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
static const uint32_t CLUT = (500u << 6) | (768u >> 4);         /* (768, 500) */
static uint32_t tpage(int depth) { return 8u | ((uint32_t)depth << 7); }   /* X 512, Y 0 */
static void drawMode(int depth) { write32(GP0, 0xE1000000u | (1u << 10) | tpage(depth)); }

static void fill15(int x, int y, int w, int h, uint32_t v) {
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    waitIdle();
    write32(GP0, 0x02000000u | (b << 16) | (g << 8) | r);
    write32(GP0, xy(x, y));
    write32(GP0, xy(w, h));
    waitIdle();
}

static uint32_t w[16];
static const int Y0 = 256;

static void rect(uint32_t cmd, int s) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    w[n++] = xy(0, Y0);
    if (cmd & 4) w[n++] = uv(0, 0, CLUT);
    w[n++] = xy(s, s);
    row(w, n);
}
static void quad(uint32_t cmd, int s, int depth) {
    int n = 0;
    w[n++] = (cmd << 24) | 0x808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? s : 0, y = (i & 2) ? s : 0;
        w[n++] = xy(x, Y0 + y);
        const int u = x > 255 ? 255 : x, v = y > 255 ? 255 : y;
        w[n++] = uv(u, v, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0);
    }
    row(w, n);
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-tex-noscan\n");
    printf("Textured rectangles and quads with the vertical display range empty (E) and the display on (D320, D512): cycles\n");
    printf("from the first word to bits 28 and 26, fastest..slowest of four, each try 53 lines after the VBlank. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write16(T1_MODE, 0x0100);                                   /* Timer 1 counts hblanks */
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x04000000u);                                  /* no DMA */
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    fill15(768, 500, 256, 1, 0x1234u);                          /* the CLUT */

    static const char* const dn[3] = { " 4-bit", " 8-bit", "15-bit" };
    static const char* const mn[3] = { "E (range empty)", "D320 (display on, 320x240)", "D512 (display on, 512x240)" };
    for (int m = 0; m < 3; m++) {
        if (m == 0) write32(GP1, 0x07000000u | (16u << 10) | 16u);
        else {
            write32(GP1, 0x07000000u | (256u << 10) | 16u);
            write32(GP1, 0x08000000u | (m == 1 ? 1u : 2u));     /* 320 or 512 wide, 240 lines, NTSC, 15-bit */
            write32(GP1, 0x06000000u | ((0x260u + (m == 1 ? 320u : 512u) * (m == 1 ? 8u : 5u)) << 12) | 0x260u);
        }
        printf("%s:\n", mn[m]);
        drawMode(2);
        printf("  flat rectangle 256x256 "); rect(0x60, 256);
        for (int d = 0; d < 3; d++) {
            fill15(512, 0, 256, 256, d == 2 ? 0x1234u : 0x1111u);
            drawMode(d);
            for (int s = 64; s <= 256; s *= 2) { printf("  %s rectangle %3dx%-3d ", dn[d], s, s); rect(0x64, s); }
            for (int s = 64; s <= 256; s *= 2) { printf("  %s quad      %3dx%-3d ", dn[d], s, s); quad(0x2C, s, d); }
        }
    }
    drawMode(2);
    initVideo(320, 240);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-tex-noscan: Done.\n");
    for (;;) {}
}
