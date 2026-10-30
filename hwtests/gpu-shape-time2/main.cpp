/*
 * gpu/gpu-shape-time2 - gpu-shape-time's exact timing for the rest of the GPU's commands: textured rectangles by depth
 * and cache, copies, lines, triangles and a quad (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-shape-time (a lone primitive's words, D cycles, ONE GPUSTAT read, D binary-searched) settled the old
 * GPU's flat and semi rectangles and fills to a cycle on the SCPH-1001 (v0.377.96) - and left the textured rectangle
 * open: a cached 16x16 15-bit sprite 41 cycles over this core, 8x8 +11, 64x4 / 4x64 missing the cache every row 25
 * under (GPU_OLD_RECT's set-up, GPU_TEXR_OLD2's pixel and line), gpu-cmd-ready's 4-bit 16x16 at 232 against 192. The
 * same instrument over each other kind gives every family's small shapes their stall-free cost, both GPUs.
 *
 * WHAT IT DOES. As gpu-shape-time: interrupts off, the GPU idle, in the vertical blanking at eight start points an
 * eighth of a line apart; the command's words (any before its last four written just before), then D cycles, ONE
 * GPUSTAT read; D binary-searched for bit 26, and for bit 28. Each shape run twice first (code and texels warm), its
 * texture page set by GP0(E1h) before that. Shapes at (192,128); texels at (512,256) - 15-bit page X 512, 8-bit and
 * 4-bit pages X 512 too - filled with one colour, CLUT at (0,480); a copy from (512,300):
 *   15-bit sprite (65h raw) 4x4, 8x8, 16x16, 32x32, 32x8, 8x32 (cached); 64x16, 16x64 (missing every row)
 *   15-bit 64h modulated 16x16, 67h semi 16x16
 *   8-bit 65h 16x16, 32x32, 64x8 (cached: the 8-bit cache is 64x32)
 *   4-bit 65h 16x16, 64x64 (cached), 128x8 (u past 64: missing)
 *   copies (80h) 16x16, 64x4, 4x64, 64x64, 128x32
 *   lines (40h) level 16 and 64, diagonal 16, steep 64; gouraud (50h) level 64
 *   triangles: flat (20h) legs 8, 16, 64; gouraud (30h) legs 16; textured 15-bit (24h) legs 16; flat quad (28h) 16x16
 *   fills 32x32, 128x8, 8x128
 *
 * READ IT LIKE THIS. Per shape, bit 26's D fastest..slowest over the eight start points and bit 28's the same, against
 * this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time2 --ntsc --frames=30000 --quiet=30000`.
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

struct Shape { const char* name; int depth; int n; uint32_t w[8]; };
static Shape S[48];
static int NS = 0;
static Shape& add(const char* name, int depth) { Shape& s = S[NS++]; s.name = name; s.depth = depth; s.n = 0; return s; }
static void W(Shape& s, uint32_t v) { s.w[s.n++] = v; }
static void defs() {
    const uint32_t P = xy(192, 128), F = 0x408020u, T = 0x808080u;
    struct { const char* nm; int d; int w; int h; uint32_t c; } tex[] = {
        { "spr15 4x4", 2, 4, 4, 0x65 }, { "spr15 8x8", 2, 8, 8, 0x65 }, { "spr15 16x16", 2, 16, 16, 0x65 },
        { "spr15 32x32", 2, 32, 32, 0x65 }, { "spr15 32x8", 2, 32, 8, 0x65 }, { "spr15 8x32", 2, 8, 32, 0x65 },
        { "spr15 64x16 miss", 2, 64, 16, 0x65 }, { "spr15 16x64 miss", 2, 16, 64, 0x65 },
        { "spr15 64h 16x16", 2, 16, 16, 0x64 }, { "spr15 semi 16x16", 2, 16, 16, 0x67 },
        { "spr8 16x16", 1, 16, 16, 0x65 }, { "spr8 32x32", 1, 32, 32, 0x65 }, { "spr8 64x8", 1, 64, 8, 0x65 },
        { "spr4 16x16", 0, 16, 16, 0x65 }, { "spr4 64x64", 0, 64, 64, 0x65 }, { "spr4 128x8 miss", 0, 128, 8, 0x65 },
    };
    for (unsigned i = 0; i < sizeof(tex) / sizeof(tex[0]); i++) {
        Shape& s = add(tex[i].nm, tex[i].d);
        W(s, (tex[i].c << 24) | T); W(s, P); W(s, uv(0, 0, CLUT)); W(s, xy(tex[i].w, tex[i].h));
    }
    struct { const char* nm; int w; int h; } cp[] = { { "copy 16x16", 16, 16 }, { "copy 64x4", 64, 4 }, { "copy 4x64", 4, 64 },
        { "copy 64x64", 64, 64 }, { "copy 128x32", 128, 32 } };
    for (unsigned i = 0; i < sizeof(cp) / sizeof(cp[0]); i++) {
        Shape& s = add(cp[i].nm, 2); W(s, 0x80000000u); W(s, xy(512, 300)); W(s, P); W(s, xy(cp[i].w, cp[i].h));
    }
    { Shape& s = add("line level 16", 2); W(s, 0x40000000u | F); W(s, P); W(s, xy(207, 128)); }
    { Shape& s = add("line level 64", 2); W(s, 0x40000000u | F); W(s, P); W(s, xy(255, 128)); }
    { Shape& s = add("line diag 16", 2); W(s, 0x40000000u | F); W(s, P); W(s, xy(207, 143)); }
    { Shape& s = add("line steep 64", 2); W(s, 0x40000000u | F); W(s, P); W(s, xy(208, 191)); }
    { Shape& s = add("line gour 64", 2); W(s, 0x50000000u | F); W(s, P); W(s, 0x00204080u); W(s, xy(255, 128)); }
    { Shape& s = add("tri flat 8", 2); W(s, 0x20000000u | F); W(s, P); W(s, xy(200, 128)); W(s, xy(192, 136)); }
    { Shape& s = add("tri flat 16", 2); W(s, 0x20000000u | F); W(s, P); W(s, xy(208, 128)); W(s, xy(192, 144)); }
    { Shape& s = add("tri flat 64", 2); W(s, 0x20000000u | F); W(s, P); W(s, xy(256, 128)); W(s, xy(192, 192)); }
    { Shape& s = add("tri gour 16", 2); W(s, 0x30000000u | F); W(s, P); W(s, 0x00204080u); W(s, xy(208, 128)); W(s, 0x00802040u); W(s, xy(192, 144)); }
    { Shape& s = add("tri tex15 16", 2); W(s, 0x24000000u | T); W(s, P); W(s, uv(0, 0, CLUT)); W(s, xy(208, 128)); W(s, uv(15, 0, tpage(2))); W(s, xy(192, 144)); W(s, uv(0, 15, 0)); }
    { Shape& s = add("quad flat 16", 2); W(s, 0x28000000u | F); W(s, P); W(s, xy(208, 128)); W(s, xy(192, 144)); W(s, xy(208, 144)); }
    { Shape& s = add("fill 32x32", 2); W(s, 0x02000000u | F); W(s, P); W(s, xy(32, 32)); }
    { Shape& s = add("fill 128x8", 2); W(s, 0x02000000u | F); W(s, P); W(s, xy(128, 8)); }
    { Shape& s = add("fill 8x128", 2); W(s, 0x02000000u | F); W(s, P); W(s, xy(16, 128)); }
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
    printf("\ngpu/gpu-shape-time2\n");
    printf("A lone primitive's last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set and\n");
    printf("with bit 28 set, fastest..slowest over eight start points in the blanking. No assertions.\n\n");
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
    static uint32_t r26[48][2], r28[48][2];
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
    printf("\ngpu/gpu-shape-time2: Done.\n");
    for (;;) VSync(0);
}
