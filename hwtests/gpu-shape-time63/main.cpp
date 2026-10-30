/*
 * gpu/gpu-shape-time63 - A POLY-LINE'S LATER SEGMENTS BY KIND: tilted, level and steep, 1-4 segments (2026-10-25 night).
 *
 * WHY. v0.377.188-189 (src/psx.js GPU_LINE2.segment, 18 ticks) charge every later segment of a poly-line its own line less
 * ~23 cycles, fitted on 64-wide segments rising 0-2 (gpu-shape-time60) and confirmed on diagonal and steep ones (61, 62). But
 * gpu-shape-time62's 2-segment TILTED poly-lines (64 wide, rises 8 / 16 / 32) read 6-7 cycles dearer than that, its level
 * ones (128 / 255 wide) 3 cheaper - from one second segment each. Is it the second segment only (gst60's third segments
 * matched where its second did not), the rise, or the width? And where between a tilted and a steep segment does the next
 * vertex start to be taken early (steep: 6-50 cycles before the end; tilted and level: within 4)?
 *
 * WHAT IT DOES. As gpu-shape-time60 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight start
 * points a shape in the blanking). Flat poly-lines (48h, zigzag down dy and back, ended by 55555555h) from (192,128) of 1-4
 * segments: 64 wide rising 4, 8, 16, 32 and 48; 128 wide rising 8; 32 wide rising 16; level 128 wide; steep 16 wide 32 down.
 * A poly's words before the last four written untimed, so N segments less N - 1 is one segment.
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest, against this core's run (PREDICTED.txt). No
 * assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time63 --ntsc --frames=60000 --quiet=60000`.
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
/* a poly-line from (192,128) of n segments w wide, alternately dy down and back up, ended by 55555555h (48h flat) */
static void polyLine(const char* nm, int c, int n, int w, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F);
    W(s, Q(192, 0, 0));
    for (int i = 1; i <= n; i++) W(s, Q(192, w * i, (i & 1) ? dy : 0));
    W(s, 0x55555555u);
}
static void defs() {
    polyLine("tilt 1x64,4", 0x48, 1, 64, 4);
    polyLine("tilt 2x64,4", 0x48, 2, 64, 4);
    polyLine("tilt 3x64,4", 0x48, 3, 64, 4);
    polyLine("tilt 4x64,4", 0x48, 4, 64, 4);
    polyLine("tilt 1x64,8", 0x48, 1, 64, 8);
    polyLine("tilt 2x64,8", 0x48, 2, 64, 8);
    polyLine("tilt 3x64,8", 0x48, 3, 64, 8);
    polyLine("tilt 4x64,8", 0x48, 4, 64, 8);
    polyLine("tilt 1x64,16", 0x48, 1, 64, 16);
    polyLine("tilt 2x64,16", 0x48, 2, 64, 16);
    polyLine("tilt 3x64,16", 0x48, 3, 64, 16);
    polyLine("tilt 4x64,16", 0x48, 4, 64, 16);
    polyLine("tilt 1x64,32", 0x48, 1, 64, 32);
    polyLine("tilt 2x64,32", 0x48, 2, 64, 32);
    polyLine("tilt 3x64,32", 0x48, 3, 64, 32);
    polyLine("tilt 4x64,32", 0x48, 4, 64, 32);
    polyLine("tilt 1x64,48", 0x48, 1, 64, 48);
    polyLine("tilt 2x64,48", 0x48, 2, 64, 48);
    polyLine("tilt 3x64,48", 0x48, 3, 64, 48);
    polyLine("tilt 4x64,48", 0x48, 4, 64, 48);
    polyLine("tilt 1x128,8", 0x48, 1, 128, 8);
    polyLine("tilt 2x128,8", 0x48, 2, 128, 8);
    polyLine("tilt 3x128,8", 0x48, 3, 128, 8);
    polyLine("tilt 4x128,8", 0x48, 4, 128, 8);
    polyLine("tilt 1x32,16", 0x48, 1, 32, 16);
    polyLine("tilt 2x32,16", 0x48, 2, 32, 16);
    polyLine("tilt 3x32,16", 0x48, 3, 32, 16);
    polyLine("tilt 4x32,16", 0x48, 4, 32, 16);
    polyLine("level 1x128,0", 0x48, 1, 128, 0);
    polyLine("level 2x128,0", 0x48, 2, 128, 0);
    polyLine("level 3x128,0", 0x48, 3, 128, 0);
    polyLine("level 4x128,0", 0x48, 4, 128, 0);
    polyLine("steep 1x16,32", 0x48, 1, 16, 32);
    polyLine("steep 2x16,32", 0x48, 2, 16, 32);
    polyLine("steep 3x16,32", 0x48, 3, 16, 32);
    polyLine("steep 4x16,32", 0x48, 4, 16, 32);
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
    printf("\ngpu/gpu-shape-time63\n");
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
    printf("\ngpu/gpu-shape-time63: Done.\n");
    for (;;) VSync(0);
}
