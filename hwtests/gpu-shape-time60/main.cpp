/*
 * gpu/gpu-shape-time60 - SHALLOW LINES: a line's cost against a rise of 0-8 rows at 16-255 pixels, alone and as a poly-line's
 * segments (2026-10-25 night).
 *
 * WHY. src/psx.js GPU_LINE2's KNOWN-OPEN: hwtests/gpu-lines on the SCPH-7501 timed poly-lines of LEVEL-ish segments (64 wide,
 * 2 rows each) at ~104 cycles a segment where this core charges ~127 - and no single 2-row line had been timed. gpu-shape-time11
 * (the SCPH-9001, this method) fitted every tilted line from 4 rows as 24.14 + 1.953 a pixel + 2.091 a row, a level one 34 +
 * 1.0 a pixel, and found the first tilts at 33 pixels cheaper than that (firstTilt: dy 1 / 2 / 3 by 9.8 / 3.9 / 2.0) - measured
 * at 33 pixels only. Is a shallow line drawn nearer a level line's 1 a pixel at any length, or is it the poly-line?
 *
 * WHAT IT DOES. As gpu-shape-time11 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat lines (40h, 408020h) from (192,128) by (dx,dy): dx 16 with dy 0 / 1 / 2; dx 64 with
 * dy 0 / 1 / 2 / 3 / 4 / 6 / 8; dx 128 with 0 / 1 / 2 / 4 / 8; dx 255 with 0 / 1 / 2 / 4. Flat poly-lines (48h, ended by
 * 55555555h) of 1-4 segments 64 wide from (192,128): level, zigzag 1 row, zigzag 2 rows (down, up, down, up) - a poly's words
 * before the last four written untimed, so N segments less N - 1 is one segment.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time60 --ntsc --frames=60000 --quiet=60000`.
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
/* a poly-line from (192,128) of n segments 64 wide, alternately dy down and back up: 48h flat, ended by 55555555h */
static void poly60(const char* nm, int n, int dy) {
    Shape& s = add(nm, 2);
    W(s, 0x48000000u | F);
    for (int i = 0; i <= n; i++) W(s, Q(192, 64 * i, (i & 1) ? dy : 0));
    W(s, 0x55555555u);
}
static void defs() {
    line11("16,0", 0x40, 16, 0);
    line11("16,1", 0x40, 16, 1);
    line11("16,2", 0x40, 16, 2);
    line11("64,0", 0x40, 64, 0);
    line11("64,1", 0x40, 64, 1);
    line11("64,2", 0x40, 64, 2);
    line11("64,3", 0x40, 64, 3);
    line11("64,4", 0x40, 64, 4);
    line11("64,6", 0x40, 64, 6);
    line11("64,8", 0x40, 64, 8);
    line11("128,0", 0x40, 128, 0);
    line11("128,1", 0x40, 128, 1);
    line11("128,2", 0x40, 128, 2);
    line11("128,4", 0x40, 128, 4);
    line11("128,8", 0x40, 128, 8);
    line11("255,0", 0x40, 255, 0);
    line11("255,1", 0x40, 255, 1);
    line11("255,2", 0x40, 255, 2);
    line11("255,4", 0x40, 255, 4);
    poly60("poly 1x64,0", 1, 0);
    poly60("poly 2x64,0", 2, 0);
    poly60("poly 3x64,0", 3, 0);
    poly60("poly 4x64,0", 4, 0);
    poly60("poly 1x64,1", 1, 1);
    poly60("poly 2x64,1", 2, 1);
    poly60("poly 3x64,1", 3, 1);
    poly60("poly 4x64,1", 4, 1);
    poly60("poly 1x64,2", 1, 2);
    poly60("poly 2x64,2", 2, 2);
    poly60("poly 3x64,2", 3, 2);
    poly60("poly 4x64,2", 4, 2);
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
    printf("\ngpu/gpu-shape-time60\n");
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
    printf("\ngpu/gpu-shape-time60: Done.\n");
    for (;;) VSync(0);
}
