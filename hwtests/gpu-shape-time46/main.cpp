/*
 * gpu/gpu-shape-time46 - where in a display line a RECTANGLE, a SPRITE or a FILL starts, against what the scan-out costs
 * it (2026-10-16).
 *
 * WHY. v0.377.107 put the scan-out's fetches at their places for the new GPU's POLYGONS (GPU_SCAN_POS: a draw pays a
 * refill or the line-start fetch whole or not at all, by where its busy span meets them; mapped by gpu-shape-time12-17).
 * Rectangles, sprites and fills still pay scanTicks' AVERAGE a line, each class's share (GPU_SCAN_K) - what 2D games
 * draw most. gpu-shape-time13 mapped one flat 16x16 rectangle and one 16x16 fill (rect ~20 a refill, fill ~31-35); a
 * positional model wants each kind at a few widths and lengths: a write-only rectangle's buffer runs on through a burst
 * by its row width (as a flat polygon's), a semi one reads and cannot, a sprite's texel fetches end the run-ahead, a fill
 * is its own engine.
 *
 * WHAT IT DOES. As gpu-shape-time13 (words, D cycles, ONE GPUSTAT read, D binary-searched for bit 26, up to 16,000): once
 * in the blanking (20,000 cycles after VBlank's IRQ), then at 32 starts 67 cycles apart across one display line of
 * Unirom's 512x240 (200,000 + 67 j). At (192,128):
 *   flat rectangles (60h) 16x16, 64x16, 128x4, 256x8, 256x64
 *   semi rectangles (62h) 16x16, 64x16, 256x8, 256x32
 *   raw sprites (65h; 67h semi) 15-bit 16x16 (texels cached), 15-bit 64x16 (a fill every 4), 4-bit 64x16 (cached), semi
 *   15-bit 64x16
 *   fills (02h) 16x16, 64x16, 256x32
 *
 * READ IT LIKE THIS. Per shape the blanking's D, then the 32 starts' D, each minus the blanking's. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time46 --ntsc --frames=60000 --quiet=60000`.
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

#define PROBE_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n"
#define PROBE_TAIL \
        "beqz %[n], 3f\n" \
        "nop\n" \
        "4:\n" \
        "addiu %[n], %[n], -1\n" \
        "bnez %[n], 4b\n" \
        "nop\n" \
        "3:\n" \
        "jr $t1\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "2:\n" \
        "lw %[st], 0x1814($t0)\n" \
        "nop\n" \
        ".set reorder\n"
static uint32_t probe2(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t probe3(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}
static uint32_t probe4(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}

struct Shape { const char* name; int depth; int n; uint32_t w[12]; };
static Shape S[64];
static int NS = 0;
static Shape& add(const char* name, int depth) { Shape& s = S[NS++]; s.name = name; s.depth = depth; s.n = 0; return s; }
static void W(Shape& s, uint32_t v) { s.w[s.n++] = v; }
static uint32_t P(int x, int y) { return xy(192 + x, 128 + y); }
static const uint32_t F = 0x408020u, T = 0x808080u, G2 = 0x00204080u, G3 = 0x00802040u;
static uint32_t Q(int x0, int x, int y) { return xy(x0 + x, 128 + y); }
/* a triangle from (192,128): c its command (20h flat, 30h gouraud with colours 404040h 404040h 404040h, 24h textured) */
static void poly10(const char* nm, int c, const int* v, const int* t, int depth) {
    Shape& s = add(nm, depth);
    const bool g = (c & 0x10) != 0, tx = (c & 4) != 0;
    W(s, ((uint32_t)c << 24) | (g ? 0x404040u : (tx ? T : F)));
    for (int i = 0; i < 3; i++) {
        if (g && i > 0) W(s, 0x404040u);
        W(s, Q(192, v[2 * i], v[2 * i + 1]));
        if (tx) W(s, uv(t[2 * i] & 255, t[2 * i + 1] & 255, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0));
    }
}
/* the flat 16x16 behind n GP0 NOPs */
static void nopflat(const char* nm, int n) {
    Shape& s = add(nm, 2);
    for (int i = 0; i < n; i++) W(s, 0u);
    W(s, 0x20000000u | F); W(s, Q(192, 0, 0)); W(s, Q(192, 16, 0)); W(s, Q(192, 0, 16));
}
static void rect(const char* nm, uint32_t c, int w, int h) {
    Shape& s = add(nm, 2);
    if (c == 0x02u) { W(s, 0x02000000u | 0x404040u); W(s, xy(192, 128)); W(s, xy(w, h)); }
    else { W(s, (c << 24) | F); W(s, xy(192, 128)); W(s, xy(w, h)); }
}
static void sprite(const char* nm, uint32_t c, int depth, int w, int h) {
    Shape& s = add(nm, depth);
    W(s, (c << 24) | T); W(s, xy(192, 128)); W(s, uv(0, 0, CLUT)); W(s, xy(w, h));
}
static void defs() {
    rect("rect 16x16", 0x60u, 16, 16); rect("rect 64x16", 0x60u, 64, 16); rect("rect 128x4", 0x60u, 128, 4);
    rect("rect 256x8", 0x60u, 256, 8); rect("rect 256x64", 0x60u, 256, 64);
    rect("semi 16x16", 0x62u, 16, 16); rect("semi 64x16", 0x62u, 64, 16); rect("semi 256x8", 0x62u, 256, 8); rect("semi 256x32", 0x62u, 256, 32);
    sprite("spr15 16x16", 0x65u, 2, 16, 16); sprite("spr15 64x16", 0x65u, 2, 64, 16); sprite("spr4 64x16", 0x65u, 0, 64, 16);
    sprite("semi spr15 64", 0x67u, 2, 64, 16);
    rect("fill 16x16", 0x02u, 16, 16); rect("fill 64x16", 0x02u, 64, 16); rect("fill 256x32", 0x02u, 256, 32);
}

/* the words before the last four (if any) written just before, then the last 2-4 stored back to back and timed */
static uint32_t probe(const Shape& s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    if (s.n <= 4) return s.n == 2 ? probe2(s.w, n, k) : s.n == 3 ? probe3(s.w, n, k) : probe4(s.w, n, k);
    for (int i = 0; i < s.n - 4; i++) write32(GP0, s.w[i]);
    return probe4(s.w + s.n - 4, n, k);
}
static uint32_t g_off = 20000u;
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = g_off + 67u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 16000;
static uint32_t search(const Shape& s, uint32_t bit, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(j);
        const uint32_t st = probe(s, mid);
        if (st & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-shape-time46\n");
    printf("A lone polygon's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set,\n");
    printf("at one start place each. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | tpage(2));
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    static uint32_t base[20], r26[20][32];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        probe(S[i], 6000); waitReady(); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
        g_off = 20000u; base[i] = search(S[i], 0x04000000u, 0);
        g_off = 200000u;
        for (int j = 0; j < 32; j++) r26[i][j] = search(S[i], 0x04000000u, j);
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then at 200,000 + 67 j cycles after VBlank's IRQ less the blanking's, j = 0..31\n");
    for (int i = 0; i < NS; i++) {
        printf("%-12s %5u :", S[i].name, (unsigned)base[i]);
        for (int j = 0; j < 32; j++) printf(" %d", (int)r26[i][j] - (int)base[i]);
        printf("\n");
    }
    printf("\ngpu/gpu-shape-time46: Done.\n");
    for (;;) VSync(0);
}
