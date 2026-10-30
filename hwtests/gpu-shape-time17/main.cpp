/*
 * gpu/gpu-shape-time17 - what the scan-out costs a LONG draw, by kind (2026-10-14 morning).
 *
 * WHY. gpu-shape-time12-16 on the SCPH-9001 mapped what the display's fetches cost a short draw (all or nothing by where
 * it starts; by kind: flat held from its first word, a write buffer for wide rows, shaded caught only near its end, semi
 * always). A model placing the fetches so took those maps from 26 to 13 cycles RMS a start - and charged gpu-cost-sweep's
 * big polygons (legs 128-256) 200-1,000 cycles more than the consoles take, where an average a line had them right. What
 * does a draw spanning many display lines pay, kind by kind?
 *
 * WHAT IT DOES. As gpu-shape-time12-16 (words, D cycles, ONE GPUSTAT read, D binary-searched for bit 26, up to 65,000):
 * once in the blanking (20,000 cycles after VBlank's IRQ) and at 16 starts 134 cycles apart across one display line
 * (200,000 + 134 j). At (192,128), right triangles: flat, gouraud, semi flat, semi gouraud 128x128; gouraud and flat
 * 256x64; 15-bit textured 128x128 with texture coordinates 0..127 (a texel a pixel) and 4-bit the same; gouraud-textured
 * 15-bit the same; 15-bit textured 64x64 over 0..255 (minified: a texture-cache fill nearly every pixel).
 *
 * READ IT LIKE THIS. Per shape the blanking's D, then the 16 starts' D, each minus the blanking's. No assertions.
 *
 * THE RULE: run it here first, the runner's way: `node tools/hwrunner.js gpu-shape-time17 --emu --lock=0 --video=24000`.
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
static void defs() {
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("flat 128", 0x20, V, U, 2); }
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("gour 128", 0x30, V, U, 2); }
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("semi 128", 0x22, V, U, 2); }
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("semi gour 128", 0x32, V, U, 2); }
    { static const int V[6] = { 0, 0, 256, 0, 0, 64 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("gour 256x64", 0x30, V, U, 2); }
    { static const int V[6] = { 0, 0, 256, 0, 0, 64 }, U[6] = { 0, 0, 0, 0, 0, 0 }; poly10("flat 256x64", 0x20, V, U, 2); }
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 127, 0, 0, 127 }; poly10("tex 128", 0x24, V, U, 2); }
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 127, 0, 0, 127 }; poly10("tex4 128", 0x24, V, U, 0); }
    { static const int V[6] = { 0, 0, 128, 0, 0, 128 }, U[6] = { 0, 0, 127, 0, 0, 127 }; poly10("gtex 128", 0x34, V, U, 2); }
    { static const int V[6] = { 0, 0, 64, 0, 0, 64 }, U[6] = { 0, 0, 255, 0, 0, 255 }; poly10("tex 64 min", 0x24, V, U, 2); }
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
    { const uint32_t t0 = now(); const uint32_t w = g_off + 134u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 65000;
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
    printf("\ngpu/gpu-shape-time17\n");
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
    static uint32_t base[16], r26[16][32];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        probe(S[i], 6000); waitReady(); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
        g_off = 20000u; base[i] = search(S[i], 0x04000000u, 0);
        g_off = 200000u;
        for (int j = 0; j < 16; j++) r26[i][j] = search(S[i], 0x04000000u, j);
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then at 200,000 + 134 j cycles after VBlank's IRQ less the blanking's, j = 0..15\n");
    for (int i = 0; i < NS; i++) {
        printf("%-12s %5u :", S[i].name, (unsigned)base[i]);
        for (int j = 0; j < 16; j++) printf(" %d", (int)r26[i][j] - (int)base[i]);
        printf("\n");
    }
    printf("\ngpu/gpu-shape-time17: Done.\n");
    for (;;) VSync(0);
}
