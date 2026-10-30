/*
 * gpu/gpu-shape-time9 - a shaded set-up's attribute law, asked on shapes it was not fitted to (2026-10-14 small hours).
 *
 * WHY. gpu-shape-time7/8 on the SCPH-9001: over the flat set-up (24 + max(2, lg|dx0|) + lg|dy0|, v0.377.103) a gouraud
 * triangle's set-up adds 129 + max(2, lg|dR1|) + bitlen|dR2| - RED only, dR1 / dR2 the second / third vertex's red less the
 * first's - and a textured one 85 + the same of U (the texture word's low byte); a gouraud-textured one 217 + the colour
 * term, U unseen. Fitted on 16x16 triangles; these are the shapes that law has not seen, each with its prediction written
 * down before the run (scratch/gst9_predict.txt).
 *
 * WHAT IT DOES. As gpu-shape-time3-8 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking), at (192,128), colours from a base of 404040h:
 *   tex ...      15-bit textured 16x16 TL: U at the third vertex only (255, 1), V only, U + 255 at both, U 255 -> 0, and
 *                the same at 4 bits; one sent with its first edge along Y and U along X
 *   gtex ...     gouraud-textured, colours alike: U + 255 at the second, at the third, all U 0; red + 255 at the third with
 *                U + 255 at both; the raw-texture command (35h) with red + 255
 *   gour ...     gouraud: 320 wide, the diagonal (BR) one, red - 1 / + 1 at the second / third, red 80h -> FFh, 00h;
 *                semi (32h) with red + 255 at both
 *   gquad ...    gouraud 16x16 quads: colours alike, red + 255 at the fourth vertex, at the second; a textured quad with U
 *                + 255 at the fourth
 *   gline ...    gouraud lines 16 across 8 down: colours alike, red + 255, green + 255; a flat line beside them
 *   flat TL / gour TL    the 16x16 flat and alike-gouraud triangles again, the anchors
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time9 --ntsc --frames=60000 --quiet=60000`.
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
/* a polygon from (192,128): c its command; n vertices, their texture coordinates and colours; depth its texture's */
static void poly9(const char* nm, int c, int n, const int* v, const int* t, const uint32_t* col, int depth) {
    Shape& s = add(nm, depth);
    const bool g = (c & 0x10) != 0, tx = (c & 4) != 0;
    W(s, ((uint32_t)c << 24) | (g ? col[0] : (tx ? T : F)));
    for (int i = 0; i < n; i++) {
        if (g && i > 0) W(s, col[i]);
        W(s, Q(192, v[2 * i], v[2 * i + 1]));
        if (tx) W(s, uv(t[2 * i] & 255, t[2 * i + 1] & 255, i == 0 ? CLUT : i == 1 ? tpage(depth) : 0));
    }
}
/* a line from (192,128) to (208,136): c 40h flat / 50h gouraud, its two colours */
static void line9(const char* nm, int c, uint32_t c0, uint32_t c1) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | c0);
    W(s, Q(192, 0, 0));
    if (c & 0x10) W(s, c1);
    W(s, Q(192, 16, 8));
}
static void defs() {
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex u2 255", 0x24, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 1, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex u2 1", 0x24, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 255, 0, 255 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex v only", 0x24, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 255, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex u12 255", 0x24, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 255, 0, 0, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex u 255->0", 0x24, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 255, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex4 u12 255", 0x24, 3, V, U, C, 0); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex4 u2 255", 0x24, 3, V, U, C, 0); }
    { static const int V[6] = { 0, 0, 0, 16, 16, 0 }, U[6] = { 0, 0, 15, 0, 0, 15 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tex vert u1 15", 0x24, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 255, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("gtex u1 255", 0x34, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("gtex u2 255", 0x34, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("gtex u0", 0x34, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 255, 0, 255, 0 }; static const uint32_t C[3] = { 0x00404000u, 0x00404000u, 0x004040ffu }; poly9("gtex R2 u12", 0x34, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 15, 0, 0, 15 }; static const uint32_t C[3] = { 0x00404000u, 0x004040ffu, 0x00404000u }; poly9("gtex raw R1", 0x35, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 320, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404000u, 0x004040ffu, 0x00404000u }; poly9("gour 320 R1", 0x30, 3, V, U, C, 2); }
    { static const int V[6] = { 16, 0, 0, 16, 16, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404000u, 0x004040ffu, 0x00404000u }; poly9("gour BR R1", 0x30, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404001u, 0x00404000u, 0x00404001u }; poly9("gour R1 -1", 0x30, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404000u, 0x00404000u, 0x00404001u }; poly9("gour R2 +1", 0x30, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404080u, 0x004040ffu, 0x00404000u }; poly9("gour 80 FF 00", 0x30, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404000u, 0x004040ffu, 0x004040ffu }; poly9("gour semi R12", 0x32, 3, V, U, C, 2); }
    { static const int V[8] = { 0, 0, 16, 0, 0, 16, 16, 16 }, U[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; static const uint32_t C[4] = { 0x00404040u, 0x00404040u, 0x00404040u, 0x00404040u }; poly9("gquad same", 0x38, 4, V, U, C, 2); }
    { static const int V[8] = { 0, 0, 16, 0, 0, 16, 16, 16 }, U[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; static const uint32_t C[4] = { 0x00404000u, 0x00404000u, 0x00404000u, 0x004040ffu }; poly9("gquad R3", 0x38, 4, V, U, C, 2); }
    { static const int V[8] = { 0, 0, 16, 0, 0, 16, 16, 16 }, U[8] = { 0, 0, 0, 0, 0, 0, 0, 0 }; static const uint32_t C[4] = { 0x00404000u, 0x004040ffu, 0x00404000u, 0x00404000u }; poly9("gquad R1", 0x38, 4, V, U, C, 2); }
    { static const int V[8] = { 0, 0, 16, 0, 0, 16, 16, 16 }, U[8] = { 0, 0, 15, 0, 0, 15, 255, 15 }; static const uint32_t C[4] = { 0x00404040u, 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tquad u3 255", 0x2c, 4, V, U, C, 2); }
    { static const int V[8] = { 0, 0, 16, 0, 0, 16, 16, 16 }, U[8] = { 0, 0, 15, 0, 0, 15, 15, 15 }; static const uint32_t C[4] = { 0x00404040u, 0x00404040u, 0x00404040u, 0x00404040u }; poly9("tquad same", 0x2c, 4, V, U, C, 2); }
    line9("gline same", 0x50, 0x00404040u, 0x00404040u);
    line9("gline R 255", 0x50, 0x00404000u, 0x004040ffu);
    line9("gline G 255", 0x50, 0x00400040u, 0x0040ff40u);
    line9("line flat", 0x40, 0x00404040u, 0x00404040u);
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("flat TL", 0x20, 3, V, U, C, 2); }
    { static const int V[6] = { 0, 0, 16, 0, 0, 16 }, U[6] = { 0, 0, 0, 0, 0, 0 }; static const uint32_t C[3] = { 0x00404040u, 0x00404040u, 0x00404040u }; poly9("gour TL", 0x30, 3, V, U, C, 2); }
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
    printf("\ngpu/gpu-shape-time9\n");
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
    printf("\ngpu/gpu-shape-time9: Done.\n");
    for (;;) VSync(0);
}
