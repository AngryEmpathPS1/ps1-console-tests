/*
 * gpu/gpu-shape-time58 - the new GPU's line hold, in the blanking, for every kind gpu-shape-time56/57 did not draw (2026-10-18).
 *
 * WHY. gpu-shape-time56/57 on the SCPH-7001 found a ~23-cycle hold at each line start, in the blanking too, at the place
 * the active lines' fetch sits (~3,362-3,422 ticks into the line), felt by what the row being drawn cannot hide: flat rows
 * from ~16 pixels wide hide part of it, gouraud rows from ~6 hide it all, OPAQUE RECTANGLES are held too (1-8 wide all of
 * it, 16 wide 3, 32 wide 10, 64 wide 8), and every kind feels it whole in its last ~100-200 cycles. This core charges the
 * hold in the blanking only for the semi rectangle (GPU_RECTS3.stall, v0.377.117) - and nothing yet says what sprites,
 * semi sprites, fills, lines or copies do with it, or how many holds a drawing that crosses several line starts feels.
 *
 * WHAT IT DOES. As gpu-shape-time57 (words, D cycles, ONE GPUSTAT read, D binary-searched for bit 26) at 32 starts 68
 * cycles apart (a whole line) from 20,000 cycles after VBlank's IRQ, all in the blanking. Every shape 48 rows tall unless
 * named: sprites (64h, 4-bit cached and 15-bit) W 4, 16, 64; semi sprites (66h, 4-bit) W 16, 64; semi rectangles (62h) W
 * 1, 4, 16, 64; flat rectangles (60h) W 16, 64 (gpu-shape-time57's, the link between the two runs); fills (02h) W 16,
 * 32, 64, 128; flat lines (40h) 64 and 256 across, 48 and 200 down, 64x48 diagonal; a gouraud line (50h) 256 across;
 * copies (80h) W 16, 64 from (512,256); and a flat rectangle and a 4-bit sprite 64 x 200 - drawings of several lines.
 *
 * READ IT LIKE THIS. Per shape its fastest D, then the 32 starts' D less it: the plateau's height is the hold that kind
 * feels, its length the drawing's, a step at its end the end catch; a tall drawing shows a plateau per line start it meets
 * (or not). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time58 --ntsc --frames=80000 --quiet=80000`.
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
/* a VRAM-to-VRAM copy (80h) of W x H from (512,256) to the shapes' corner */
static void copyS(const char* nm, int w, int h) {
    Shape& s = add(nm, 2);
    W(s, 0x80000000u); W(s, xy(512, 256)); W(s, xy(192, 128)); W(s, xy(w, h));
}
static void defs() {
    static char nm[40][24];
    int k = 0;
    static const int WS[3] = { 4, 16, 64 };
    for (int i = 0; i < 3; i++) { sprintf(nm[k], "spr4 %d", WS[i]); sprite(nm[k++], 0x64u, 0, WS[i], 48); }
    for (int i = 0; i < 3; i++) { sprintf(nm[k], "spr15 %d", WS[i]); sprite(nm[k++], 0x64u, 2, WS[i], 48); }
    sprite("semispr4 16", 0x66u, 0, 16, 48); k++;
    sprite("semispr4 64", 0x66u, 0, 64, 48); k++;
    static const int WM[4] = { 1, 4, 16, 64 };
    for (int i = 0; i < 4; i++) { sprintf(nm[k], "semi rect %d", WM[i]); rect(nm[k++], 0x62u, WM[i], 48); }
    rect("rect 16", 0x60u, 16, 48); k++;
    rect("rect 64", 0x60u, 64, 48); k++;
    static const int WF[4] = { 16, 32, 64, 128 };
    for (int i = 0; i < 4; i++) { sprintf(nm[k], "fill %d", WF[i]); rect(nm[k++], 0x02u, WF[i], 48); }
    lineS("line 64 across", 0x40u, 64, 0); k++;
    lineS("line 256 across", 0x40u, 256, 0); k++;
    lineS("line 48 down", 0x40u, 0, 48); k++;
    lineS("line 200 down", 0x40u, 0, 200); k++;
    lineS("line 64x48", 0x40u, 64, 48); k++;
    lineS("gline 256", 0x50u, 256, 0); k++;
    copyS("copy 16", 16, 48); k++;
    copyS("copy 64", 64, 48); k++;
    rect("rect 64x200", 0x60u, 64, 200); k++;
    sprite("spr4 64x200", 0x64u, 0, 64, 200); k++;
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
static const uint32_t MAXD = 32000;   /* the 64 x 200 sprite reads ~14,900 here: room for a slower console */
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
    printf("\ngpu/gpu-shape-time58\n");
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
    printf("\ngpu/gpu-shape-time58: Done.\n");
    for (;;) VSync(0);
}
