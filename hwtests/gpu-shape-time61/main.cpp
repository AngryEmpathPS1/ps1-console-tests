/*
 * gpu/gpu-shape-time61 - POLY-LINES BEYOND THE FLAT SHALLOW ONES, and the shallow rises v0.377.188 assumed (2026-10-25 night).
 *
 * WHY. gpu-shape-time60 on the SCPH-7501 measured flat shallow lines (rises 0-8, 16-255 pixels) and flat poly-lines of 64-wide
 * segments rising 0-2 rows: a shallow line's per-pixel rate falls with its rise (1.513 cycles a pixel at a rise of 1 ... 1.953
 * from 8), and each later segment of a poly-line costs its own line less ~23 cycles - the line's set-up (40) less ~11 of its
 * own. v0.377.188 (src/psx.js GPU_LINE2.shallow / .segment) took both, and ASSUMED: rises 5 and 7 (6 at 64 pixels only, 3 at
 * 64 only), lines under 16 pixels never dearer than the formula, a steep or diagonal segment's set-up the shallow one's, a
 * gouraud segment paying its whole 98-tick gouraud set-up each, a semi one nothing more.
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat lines (40h) from (192,128) by (dx,dy): 4,1; 8,1; 8,2; 8,3; 64,5; 64,7; 128,3; 128,5;
 * 128,6; 128,7; 255,3; 255,8. Gouraud (50h) 128,1 and 128,2; semi (42h) 128,1, 128,2 and 255,1. Poly-lines of 1-4 segments
 * from (192,128), zigzag (down dy, back up): flat 48h DIAGONAL (64 wide, 64 down) and STEEP (16 wide, 64 down); gouraud 58h
 * level and 1 row (64 wide; the colours alternate); semi 4Ah level and 1 row (64 wide). A poly's words before the last four
 * written untimed, so N segments less N - 1 is one segment.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time61 --ntsc --frames=60000 --quiet=60000`.
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
/* a line from (192,128) by (dx,dy): c 40h flat, 42h semi, 50h gouraud (colours 408020h, 204080h) */
static void line11(const char* nm, int c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F);
    W(s, Q(192, 0, 0));
    if (c & 0x10) W(s, G2);
    W(s, Q(192, dx, dy));
}
/* a poly-line from (192,128) of n segments w wide, alternately dy down and back up, ended by 55555555h: c 48h flat, 4Ah semi,
   58h gouraud (the colours alternate 204080h / 408020h) */
static void polyLine(const char* nm, int c, int n, int w, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F);
    W(s, Q(192, 0, 0));
    for (int i = 1; i <= n; i++) {
        if (c & 0x10) W(s, (i & 1) ? G2 : F);
        W(s, Q(192, w * i, (i & 1) ? dy : 0));
    }
    W(s, 0x55555555u);
}
static void defs() {
    line11("4,1", 0x40, 4, 1);
    line11("8,1", 0x40, 8, 1);
    line11("8,2", 0x40, 8, 2);
    line11("8,3", 0x40, 8, 3);
    line11("64,5", 0x40, 64, 5);
    line11("64,7", 0x40, 64, 7);
    line11("128,3", 0x40, 128, 3);
    line11("128,5", 0x40, 128, 5);
    line11("128,6", 0x40, 128, 6);
    line11("128,7", 0x40, 128, 7);
    line11("255,3", 0x40, 255, 3);
    line11("255,8", 0x40, 255, 8);
    line11("gour 128,1", 0x50, 128, 1);
    line11("gour 128,2", 0x50, 128, 2);
    line11("semi 128,1", 0x42, 128, 1);
    line11("semi 128,2", 0x42, 128, 2);
    line11("semi 255,1", 0x42, 255, 1);
    polyLine("diag 1x64,64", 0x48, 1, 64, 64);
    polyLine("diag 2x64,64", 0x48, 2, 64, 64);
    polyLine("diag 3x64,64", 0x48, 3, 64, 64);
    polyLine("diag 4x64,64", 0x48, 4, 64, 64);
    polyLine("steep 1x16,64", 0x48, 1, 16, 64);
    polyLine("steep 2x16,64", 0x48, 2, 16, 64);
    polyLine("steep 3x16,64", 0x48, 3, 16, 64);
    polyLine("steep 4x16,64", 0x48, 4, 16, 64);
    polyLine("gpoly 1x64,0", 0x58, 1, 64, 0);
    polyLine("gpoly 2x64,0", 0x58, 2, 64, 0);
    polyLine("gpoly 3x64,0", 0x58, 3, 64, 0);
    polyLine("gpoly 4x64,0", 0x58, 4, 64, 0);
    polyLine("gpoly 1x64,1", 0x58, 1, 64, 1);
    polyLine("gpoly 2x64,1", 0x58, 2, 64, 1);
    polyLine("gpoly 3x64,1", 0x58, 3, 64, 1);
    polyLine("gpoly 4x64,1", 0x58, 4, 64, 1);
    polyLine("spoly 1x64,0", 0x4A, 1, 64, 0);
    polyLine("spoly 2x64,0", 0x4A, 2, 64, 0);
    polyLine("spoly 3x64,0", 0x4A, 3, 64, 0);
    polyLine("spoly 4x64,0", 0x4A, 4, 64, 0);
    polyLine("spoly 1x64,1", 0x4A, 1, 64, 1);
    polyLine("spoly 2x64,1", 0x4A, 2, 64, 1);
    polyLine("spoly 3x64,1", 0x4A, 3, 64, 1);
    polyLine("spoly 4x64,1", 0x4A, 4, 64, 1);
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
    printf("\ngpu/gpu-shape-time61\n");
    printf("A shape's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set\n");
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
    printf("\ngpu/gpu-shape-time61: Done.\n");
    for (;;) VSync(0);
}
