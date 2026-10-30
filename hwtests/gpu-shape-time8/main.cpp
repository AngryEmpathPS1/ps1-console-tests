/*
 * gpu/gpu-shape-time8 - a gouraud polygon's set-up against its colours (2026-10-14 small hours).
 *
 * WHY. gpu-shape-time7 on the SCPH-9001: a gouraud 16x16 triangle's set-up (bit 28) reads 159 cycles with all three
 * colours alike, 159 / 161 / 164 with red differing by 1 / 16 / 255 at its second vertex, and 170 with two channels in
 * steps of 96 - this core charges 169 whatever the colours. The set-up interpolates each channel, and its time grows with
 * the deltas' lengths, as the first edge's multiply does with its coordinates (v0.377.103): which deltas, how.
 *
 * WHAT IT DOES. As gpu-shape-time3-7 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking). Gouraud 16x16 right triangles (0,0),(16,0),(0,16) at (192,128), colours c0 c1 c2
 * from a base of 404040h (R G B 40h each, R from 0 where it climbs to 255):
 *   R1 d    red + d at the second vertex only: d = 2, 4, 8, 32, 64, 128
 *   R2 d    red + d at the third vertex only: d = 16, 255
 *   R12     red + 255 at the second and third
 *   Rneg    red - 255 at the second (255 down to 0)
 *   G1 d / B1 d    green / blue + d at the second: d = 16, 255
 *   RG / RGB    red and green / all three + 255 at the second
 *   gtex same / gtex R    gouraud-textured (15-bit, coordinates 0..15) with colours alike / red + 255 at the second
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time8 --ntsc --frames=60000 --quiet=60000`.
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
    static const int TL16[6] = { 0, 0, 16, 0, 0, 16 }, UV15[6] = { 0, 0, 15, 0, 0, 15 };
    { static const uint32_t C[3] = { 0x00404000u, 0x00404002u, 0x00404000u }; tri7("R1 2", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404004u, 0x00404000u }; tri7("R1 4", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404008u, 0x00404000u }; tri7("R1 8", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404020u, 0x00404000u }; tri7("R1 32", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404040u, 0x00404000u }; tri7("R1 64", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404080u, 0x00404000u }; tri7("R1 128", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404000u, 0x00404010u }; tri7("R2 16", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x00404000u, 0x004040ffu }; tri7("R2 255", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x004040ffu, 0x004040ffu }; tri7("R12", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x004040ffu, 0x00404000u, 0x004040ffu }; tri7("Rneg", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00400040u, 0x00401040u, 0x00400040u }; tri7("G1 16", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00400040u, 0x0040ff40u, 0x00400040u }; tri7("G1 255", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00004040u, 0x00104040u, 0x00004040u }; tri7("B1 16", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00004040u, 0x00ff4040u, 0x00004040u }; tri7("B1 255", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00400000u, 0x0040ffffu, 0x00400000u }; tri7("RG", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00000000u, 0x00ffffffu, 0x00000000u }; tri7("RGB", 0x30, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; tri7("gtex same", 0x34, TL16, UV15, C); }
    { static const uint32_t C[3] = { 0x00404000u, 0x004040ffu, 0x00404000u }; tri7("gtex R", 0x34, TL16, UV15, C); }
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
    printf("\ngpu/gpu-shape-time8\n");
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
    printf("\ngpu/gpu-shape-time8: Done.\n");
    for (;;) VSync(0);
}
