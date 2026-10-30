/*
 * gpu/gpu-shape-time7 - what a shaded polygon's set-up grows with (2026-10-14 small hours).
 *
 * WHY. A flat triangle's set-up (bit 28) is 24 + max(2, lg|dx0|) + lg|dy0| cycles over its FIRST edge (gpu-shape-time3-5,
 * all 57 exact, v0.377.103). The shaded kinds were measured on right triangles whose first edge runs along X only:
 * gouraud 165 + lg W, textured 108 + 2 lg W, gouraud-textured 250 + lg W - and gpu-shape-time6's 320-wide textured ones read
 * 119, under the 64-wide one's 120, so the textured term is not the width. In a DMA stream a small textured triangle
 * runs at its set-up plus the packet's delivery (gpu-dmastream), so this set-up is every small textured triangle's cost.
 *
 * WHAT IT DOES. As gpu-shape-time3-6 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking), at (192,128):
 *   tex u/v N    15-bit textured 16x16 TL, its texture coordinates (0,0) (N,0) (0,N): N = 0, 1, 4, 15, 64, 255
 *   tex W        15-bit textured W x 16 TL with texture coordinates (0,0) (15,0) (0,15): W = 4, 16, 64, 256
 *   tex vert     15-bit textured 16x16 TL sent (0,0),(0,16),(16,0) - its first edge along Y
 *   tex diag     15-bit textured 16x16 sent (16,0),(0,16),(16,16) - its first edge diagonal (BR)
 *   gour vert / gour diag   the same two for gouraud
 *   gour flat colour    gouraud 16x16 TL with all three colours the same
 *   gour colour N       gouraud 16x16 TL with colours differing by N in red only: N = 1, 16, 255
 *   gtex u/v N   gouraud-textured 16x16 TL, texture coordinates 0..N: N = 1, 15, 255
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time7 --ntsc --frames=60000 --quiet=60000`.
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
/* a triangle from (192,128): c 30h gouraud / 24h textured / 34h both; vertices, texture coordinates and colours given */
static void tri7(const char* nm, int c, const int* v, const int* t, const uint32_t* col) {
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0, tx = (c & 4) != 0;
    W(s, ((uint32_t)c << 24) | (g ? col[0] : (tx ? T : F)));
    for (int i = 0; i < 3; i++) {
        if (g && i > 0) W(s, col[i]);
        W(s, Q(192, v[2 * i], v[2 * i + 1]));
        if (tx) W(s, uv(t[2 * i] & 255, t[2 * i + 1] & 255, i == 0 ? CLUT : i == 1 ? tpage(2) : 0));
    }
}
static void defs() {
    static const int TL16[6] = { 0, 0, 16, 0, 0, 16 }, VERT[6] = { 0, 0, 0, 16, 16, 0 }, DIAG[6] = { 16, 0, 0, 16, 16, 16 };
    static const uint32_t COL[3] = { 0x00204080u, 0x00804020u, 0x00408020u }, SAME[3] = { 0x00404040u, 0x00404040u, 0x00404040u };
    static const uint32_t C1[3] = { 0x00404040u, 0x00404041u, 0x00404040u }, C16[3] = { 0x00404040u, 0x00404050u, 0x00404040u }, C255[3] = { 0x00404000u, 0x004040FFu, 0x00404000u };
    static const int UV0[6] = { 0, 0, 0, 0, 0, 0 }, UV1[6] = { 0, 0, 1, 0, 0, 1 }, UV4[6] = { 0, 0, 4, 0, 0, 4 }, UV15[6] = { 0, 0, 15, 0, 0, 15 };
    static const int UV64[6] = { 0, 0, 64, 0, 0, 64 }, UV255[6] = { 0, 0, 255, 0, 0, 255 };
    static const int UVV[6] = { 0, 0, 0, 15, 15, 0 }, UVD[6] = { 15, 0, 0, 15, 15, 15 };
    tri7("tex u/v 0", 0x24, TL16, UV0, COL); tri7("tex u/v 1", 0x24, TL16, UV1, COL); tri7("tex u/v 4", 0x24, TL16, UV4, COL);
    tri7("tex u/v 15", 0x24, TL16, UV15, COL); tri7("tex u/v 64", 0x24, TL16, UV64, COL); tri7("tex u/v 255", 0x24, TL16, UV255, COL);
    static const int W4[6] = { 0, 0, 4, 0, 0, 16 }, W64[6] = { 0, 0, 64, 0, 0, 16 }, W256[6] = { -64, 0, 192, 0, -64, 16 };
    tri7("tex W 4", 0x24, W4, UV15, COL); tri7("tex W 16", 0x24, TL16, UV15, COL); tri7("tex W 64", 0x24, W64, UV15, COL); tri7("tex W 256", 0x24, W256, UV15, COL);
    tri7("tex vert", 0x24, VERT, UVV, COL); tri7("tex diag", 0x24, DIAG, UVD, COL);
    tri7("gour vert", 0x30, VERT, UV0, COL); tri7("gour diag", 0x30, DIAG, UV0, COL); tri7("gour TL", 0x30, TL16, UV0, COL);
    tri7("gour flat colour", 0x30, TL16, UV0, SAME); tri7("gour colour 1", 0x30, TL16, UV0, C1);
    tri7("gour colour 16", 0x30, TL16, UV0, C16); tri7("gour colour 255", 0x30, TL16, UV0, C255);
    tri7("gtex u/v 1", 0x34, TL16, UV1, COL); tri7("gtex u/v 15", 0x34, TL16, UV15, COL); tri7("gtex u/v 255", 0x34, TL16, UV255, COL);
}

/* the words before the last four (if any) written just before, then the last 2-4 stored back to back and timed */
static uint32_t probe(const Shape& s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    if (s.n <= 4) return s.n == 2 ? probe2(s.w, n, k) : s.n == 3 ? probe3(s.w, n, k) : probe4(s.w, n, k);
    for (int i = 0; i < s.n - 4; i++) write32(GP0, s.w[i]);
    return probe4(s.w + s.n - 4, n, k);
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
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
    printf("\ngpu/gpu-shape-time7\n");
    printf("A lone polygon or line's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set\n");
    printf("and with bit 28 set, fastest..slowest over eight start points in the blanking. No assertions.\n\n");
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
    static uint32_t r26[64][2], r28[64][2];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        probe(S[i], 6000); waitReady(); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
        uint32_t a = 0xFFFFFFFFu, b = 0, c = 0xFFFFFFFFu, d = 0;
        for (int j = 0; j < 8; j++) {
            const uint32_t x = search(S[i], 0x04000000u, j), y = search(S[i], 0x10000000u, j);
            a = x < a ? x : a; b = x > b ? x : b; c = y < c ? y : c; d = y > d ? y : d;
        }
        r26[i][0] = a; r26[i][1] = b; r28[i][0] = c; r28[i][1] = d;
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("shape               bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-18s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-shape-time7: Done.\n");
    for (;;) VSync(0);
}
