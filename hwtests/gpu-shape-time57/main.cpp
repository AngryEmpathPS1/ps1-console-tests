/*
 * gpu/gpu-shape-time57 - the new GPU's line hold against the width of the row being drawn: constant-width strips (2026-10-17).
 *
 * WHY. gpu-shape-time56 on the SCPH-7001 (six runs): the ~23-cycle hold at each line start, in the blanking too, is felt by
 * what the row being drawn cannot hide - a flat 64x64 triangle 22-23 on its last narrow rows, ~8 over its ~32-wide middle,
 * 3-5 on its first 64-wide rows; gouraud rows hide it unless 1-3 pixels wide, but a drawing's END catches it whole (the
 * 4-wide parallelogram 22-23 to its last row). A triangle's rows change width as it draws, so its rise mixes widths. Two
 * sweeps here: quads W wide (each half a triangle, its rows W -> 0 and 0 -> W - the hold by the widest row, in one shape
 * for every kind), and flat RECTANGLES W wide, whose rows ARE all W - the hold at one width, flat across the drawing, any
 * rise at its end the end catch alone (an opaque rectangle's hold is unmodelled too: GPU_RECTS3's is the semi one's).
 *
 * WHAT IT DOES. As gpu-shape-time56 (words, D cycles, ONE GPUSTAT read, D binary-searched for bit 26) at 32 starts 68 cycles
 * apart (2,176 cycles: a whole line) from 20,000 cycles after VBlank's IRQ, all in the blanking: quads (0,0),(W,0),(0,48),
 * (W,48) - two triangles, 48 rows a W-wide quad - flat (28h) and gouraud (38h) at W 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48,
 * 64, and 4-bit textured (2Ch, cached) at W 4, 16, 64; then flat rectangles (60h) W x 48 at W 1, 2, 4, 8, 16, 32, 64.
 *
 * READ IT LIKE THIS. Per strip its fastest D, then the 32 starts' D less it: the plateau's height is the hold at that width,
 * its length the drawing's, a step at its end the end catch. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time57 --ntsc --frames=80000 --quiet=80000`.
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
static void lineS(const char* nm, uint32_t c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, (c << 24) | F); W(s, xy(192, 128)); if (c & 0x10) W(s, G2); W(s, xy(192 + dx, 128 + dy));
}
/* a gouraud quad of four vertices in this order (gpu-shape-time55's) */
static void gquad4(const char* nm, uint32_t c, const int* V) {
    Shape& s = add(nm, 2);
    W(s, (c << 24) | F); W(s, Q(192, V[0], V[1])); W(s, G2); W(s, Q(192, V[2], V[3])); W(s, G3); W(s, Q(192, V[4], V[5])); W(s, 0x00408020u); W(s, Q(192, V[6], V[7]));
}
/* a flat quad (28h) */
static void fquad4(const char* nm, const int* V) {
    Shape& s = add(nm, 2);
    W(s, 0x28000000u | F); for (int i = 0; i < 4; i++) W(s, Q(192, V[2 * i], V[2 * i + 1]));
}
/* a 4-bit textured quad (2Ch), texels from (0,0) */
static void tquad4(const char* nm, const int* V) {
    Shape& s = add(nm, 0);
    W(s, 0x2C000000u | T);
    static const int U[8] = { 0, 0, 63, 0, 0, 47, 63, 47 };
    for (int i = 0; i < 4; i++) { W(s, Q(192, V[2 * i], V[2 * i + 1])); W(s, uv(U[2 * i] & 255, U[2 * i + 1] & 255, i == 0 ? CLUT : i == 1 ? tpage(0) : 0)); }
}
static void defs() {
    static char nm[40][24];
    static const int WS[12] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 }, WT[3] = { 4, 16, 64 };
    int k = 0;
    for (int i = 0; i < 12; i++) { const int w = WS[i], V[8] = { 0, 0, w, 0, 0, 48, w, 48 }; sprintf(nm[k], "flat strip %d", w); fquad4(nm[k++], V); }
    for (int i = 0; i < 12; i++) { const int w = WS[i], V[8] = { 0, 0, w, 0, 0, 48, w, 48 }; sprintf(nm[k], "gour strip %d", w); gquad4(nm[k++], 0x38, V); }
    for (int i = 0; i < 3; i++) { const int w = WT[i], V[8] = { 0, 0, w, 0, 0, 48, w, 48 }; sprintf(nm[k], "tex4 strip %d", w); tquad4(nm[k++], V); }
    static const int WR[7] = { 1, 2, 4, 8, 16, 32, 64 };
    for (int i = 0; i < 7; i++) { sprintf(nm[k], "rect %d", WR[i]); rect(nm[k++], 0x60u, WR[i], 48); }
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
    { const uint32_t t0 = now(); const uint32_t w = g_off + 68u * (uint32_t)j; while (now() - t0 < w) {} }
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
    printf("\ngpu/gpu-shape-time57\n");
    printf("A lone polygon's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set,\n");
    printf("at 32 start places across one line in the blanking. No assertions.\n\n");
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
    static uint32_t r26[40][32];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        probe(S[i], 6000); waitReady(); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
        for (int j = 0; j < 32; j++) r26[i][j] = search(S[i], 0x04000000u, j);
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D: the fastest of the 32 starts, then at 20,000 + 68 j cycles after VBlank's IRQ less it, j = 0..31\n");
    for (int i = 0; i < NS; i++) {
        uint32_t lo = r26[i][0];
        for (int j = 1; j < 32; j++) if (r26[i][j] < lo) lo = r26[i][j];
        printf("%-14s %5u :", S[i].name, (unsigned)lo);
        for (int j = 0; j < 32; j++) printf(" %d", (int)(r26[i][j] - lo));
        printf("\n");
    }
    printf("\ngpu/gpu-shape-time57: Done.\n");
    for (;;) VSync(0);
}
