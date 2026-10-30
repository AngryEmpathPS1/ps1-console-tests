/*
 * gpu/gpu-shape-time10 - a texture-cache fill in the blanking and under the display's scan-out (2026-10-14 early morning).
 *
 * WHY. gpu-shape-time7/9 on the SCPH-9001, every draw in the vertical blanking: a 16x16 textured triangle whose texels
 * miss the cache on every pixel (U or V spanning 0..255) costs ~8.4 cycles a fill over the same triangle with no fills, at
 * 15 and 4 bits alike, where this core charges ~9.0 - and that fill cost was fitted to keep gpu/bandwidth's full-screen
 * textured quads, which draw while the display reads the frame buffer. Does the scan-out make a fill dearer?
 *
 * WHAT IT DOES. As gpu-shape-time3-9 (words, D cycles, ONE GPUSTAT read, D binary-searched for bit 26, eight start
 * points), each shape started at four places in the frame: the blanking (20,000 cycles after VBlank's IRQ, as before),
 * and three in the display (80,000 / 200,000 / 400,000 after it). Shapes at (192,128): a flat 16x16 and 64x64, a gouraud
 * 16x16, 15-bit textured 16x16 with texture coordinates 0..15 (no fills) and 0..255 (a fill a pixel), 4-bit the same,
 * and a 15-bit 256x16 with 0..15 (2,176 pixels, no fills).
 *
 * READ IT LIKE THIS. Per shape and place, bit 26's D, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time10 --ntsc --frames=60000 --quiet=60000`.
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
static void defs() {
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("flat 16", 0x20, V, U, 2); }
    { static const int V[6] = { 0, 0, 64, 0, 0, 64 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("flat 64", 0x20, V, U, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("gour 16", 0x30, V, U, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 15, 0, 0, 15 }; poly10("tex 0..15", 0x24, V, U, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 255, 0, 0, 255 }; poly10("tex 0..255", 0x24, V, U, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 15, 0, 0, 15 }; poly10("tex4 0..15", 0x24, V, U, 0); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 255, 0, 0, 255 }; poly10("tex4 0..255", 0x24, V, U, 0); }
    { static const int V[6] = { -64, 0, 192, 0, -64, 16 }, U[6] = { 0, 0, 15, 0, 0, 15 }; poly10("tex 256x16", 0x24, V, U, 2); }
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
    { const uint32_t t0 = now(); const uint32_t w = g_off + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 8000;
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
    printf("\ngpu/gpu-shape-time10\n");
    printf("A lone polygon's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set,\n");
    printf("fastest..slowest over eight start points at each of four places in the frame. No assertions.\n\n");
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
    static const uint32_t OFF[4] = { 20000u, 80000u, 200000u, 400000u };
    static uint32_t r26[16][4][2];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        probe(S[i], 6000); waitReady(); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
        for (int o = 0; o < 4; o++) {
            g_off = OFF[o];
            uint32_t a = 0xFFFFFFFFu, b = 0;
            for (int j = 0; j < 8; j++) { const uint32_t x = search(S[i], 0x04000000u, j); a = x < a ? x : a; b = x > b ? x : b; }
            r26[i][o][0] = a; r26[i][o][1] = b;
        }
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D started at 20,000 (blanking) / 80,000 / 200,000 / 400,000 cycles after VBlank's IRQ\n");
    printf("shape              blank          80000          200000         400000\n");
    for (int i = 0; i < NS; i++) {
        printf("%-16s", S[i].name);
        for (int o = 0; o < 4; o++) printf(" %5u..%-5u  ", (unsigned)r26[i][o][0], (unsigned)r26[i][o][1]);
        printf("\n");
    }
    printf("\ngpu/gpu-shape-time10: Done.\n");
    for (;;) VSync(0);
}
