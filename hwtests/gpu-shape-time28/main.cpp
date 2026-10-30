/*
 * gpu/gpu-shape-time28 - the new GPU's slope-one rows and its drawing direction (2026-10-15).
 *
 * WHY. gpu-shape-time18/19/22/23 on the SCPH-9001 (2026-10-15, two runs): (1) a flat triangle whose left edge steps
 * EXACTLY one pixel a row costs ~0.6-0.9 a row more than its neighbours - TR 63x64 1193, 64x64 1259, 65x64 1229 (TL, its
 * left edge vertical, 1199 1201 1231); TR 16x16 153 against 15x16 135 and 17x16 147; BR 16x16 148 against BL 136; semi
 * TR 16x16 233 against TL 221 where every other semi TR = its TL - while a walk of any other slope costs a flat or semi
 * row nothing below ~5 pixels a row (this core's model: smooth). (2) One triangle's six vertex orders split by their
 * parity: the set-up (bit 28) as this core has it in all 30, but the drawing 110 / 115 (flat TL 16x16), 1168 / 1171
 * (64x64), 99 / 106 (flat BL 16x16), 182 / 179 and 2224 / 2219 (gouraud TL) - the parity picks the drawing order
 * (mednafen's core vertex: (0,0) top-down or (0,H) bottom-up): rows that WIDEN as drawn are cheaper on a flat one,
 * dearer on a gouraud one. This maps both.
 *
 * WHAT IT DOES. As gpu-shape-time23 (words, D cycles, ONE GPUSTAT read, D binary-searched for bits 26 and 28 at eight
 * start points a line in the blanking), at x 0, Y 128:
 *   A  flat TR (0,0),(W,0),(W,64) and TL (0,0),(W,0),(0,64) at W 60 62 63 64 65 66 68
 *   B  flat TR W x W at 8 24 32 48 96 128, and 31 / 33 x 32, 127 / 129 x 128
 *   C  flat TR at slope 2 (63 64 65 x 32) and slope 1/2 (31 32 33 x 64)
 *   D  semi (22h) and gouraud (30h) TR 63 64 65 x 64
 *   E  flat TL and BL in orders 1 (ABC: TL top-down, BL bottom-up) and 2 (ACB: the other way) at 16x16 32x32 64x64
 *      16x64 64x16; gouraud TL in both at 32x32 and 16x64
 *
 * READ IT LIKE THIS. Per shape bit 26's D and bit 28's, fastest..slowest; bit 26 less bit 28 is the drawing. Predictions
 * in PREDICTED.txt, written before any console ran it. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time28 --ntsc --frames=40000 --quiet=40000`.
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
/* a flat triangle (opaque, 20h) with its vertices in this order, from (x0,128) */
static void tri3(const char* nm, int x0, int ax, int ay, int bx, int by, int cx, int cy) {
    Shape& s = add(nm, 2);
    W(s, 0x20000000u | F); W(s, Q(x0, ax, ay)); W(s, Q(x0, bx, by)); W(s, Q(x0, cx, cy));
}
/* a flat quad (opaque, 28h) with its vertices in this order: (v0,v1,v2) then (v1,v2,v3) */
static void quad4(const char* nm, int x0, int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, 0x28000000u | F); W(s, Q(x0, ax, ay)); W(s, Q(x0, bx, by)); W(s, Q(x0, cx, cy)); W(s, Q(x0, dx, dy));
}
/* a line from (192,128) by (dx,dy): 40h flat, 50h gouraud */
static void line2(const char* nm, int c, int dx, int dy) {
    Shape& s = add(nm, 2);
    W(s, ((uint32_t)c << 24) | F); W(s, Q(192, 0, 0)); if (c & 0x10) W(s, G2); W(s, Q(192, dx, dy));
}
/* a triangle from three vertices in the order given: c = 20h flat, 30h gouraud */
static void vtri(const char* nm, uint32_t c, int x0, const int* P3) {
    Shape& s = add(nm, 2);
    const bool g = (c & 0x10) != 0;
    W(s, (c << 24) | F);
    for (int k = 0; k < 3; k++) { if (g && k) W(s, k == 1 ? G2 : G3); W(s, Q(x0, P3[2 * k], P3[2 * k + 1])); }
}
static void defs() {
    static char nm[96][28];
    int k = 0;
    static const int WA[7] = { 60, 62, 63, 64, 65, 66, 68 };
    for (int i = 0; i < 7; i++) for (int t = 0; t < 2; t++) {
        const int w = WA[i];
        const int P3[6] = { 0, 0, w, 0, t ? 0 : w, 64 };
        sprintf(nm[k], "flat %s %dx64", t ? "TL" : "TR", w); vtri(nm[k++], 0x20, 0, P3);
    }
    static const int WB[10][2] = { { 8, 8 }, { 24, 24 }, { 32, 32 }, { 48, 48 }, { 96, 96 }, { 128, 128 }, { 31, 32 }, { 33, 32 }, { 127, 128 }, { 129, 128 } };
    for (int i = 0; i < 10; i++) { const int w = WB[i][0], h = WB[i][1]; const int P3[6] = { 0, 0, w, 0, w, h }; sprintf(nm[k], "flat TR %dx%d", w, h); vtri(nm[k++], 0x20, 0, P3); }
    static const int WC[6][2] = { { 63, 32 }, { 64, 32 }, { 65, 32 }, { 31, 64 }, { 32, 64 }, { 33, 64 } };
    for (int i = 0; i < 6; i++) { const int w = WC[i][0], h = WC[i][1]; const int P3[6] = { 0, 0, w, 0, w, h }; sprintf(nm[k], "flat TR %dx%d", w, h); vtri(nm[k++], 0x20, 0, P3); }
    for (int c = 0; c < 2; c++) for (int w = 63; w <= 65; w++) {
        const int P3[6] = { 0, 0, w, 0, w, 64 };
        sprintf(nm[k], "%s TR %dx64", c ? "gour" : "semi", w); vtri(nm[k++], c ? 0x30 : 0x22, 0, P3);
    }
    static const int WE[5][2] = { { 16, 16 }, { 32, 32 }, { 64, 64 }, { 16, 64 }, { 64, 16 } };
    for (int i = 0; i < 5; i++) for (int t = 0; t < 2; t++) for (int o = 0; o < 2; o++) {
        const int w = WE[i][0], h = WE[i][1];
        const int TLv[3][2] = { { 0, 0 }, { w, 0 }, { 0, h } }, BLv[3][2] = { { 0, 0 }, { 0, h }, { w, h } };
        const int (*V)[2] = t ? BLv : TLv; const int ord[3] = { 0, o ? 2 : 1, o ? 1 : 2 };
        int P3[6]; for (int j = 0; j < 3; j++) { P3[2 * j] = V[ord[j]][0]; P3[2 * j + 1] = V[ord[j]][1]; }
        sprintf(nm[k], "flat %s %dx%d o%d", t ? "BL" : "TL", w, h, o + 1); vtri(nm[k++], 0x20, 0, P3);
    }
    static const int WG[2][2] = { { 32, 32 }, { 16, 64 } };
    for (int i = 0; i < 2; i++) for (int o = 0; o < 2; o++) {
        const int w = WG[i][0], h = WG[i][1];
        const int TLv[3][2] = { { 0, 0 }, { w, 0 }, { 0, h } }; const int ord[3] = { 0, o ? 2 : 1, o ? 1 : 2 };
        int P3[6]; for (int j = 0; j < 3; j++) { P3[2 * j] = TLv[ord[j]][0]; P3[2 * j + 1] = TLv[ord[j]][1]; }
        sprintf(nm[k], "gour TL %dx%d o%d", w, h, o + 1); vtri(nm[k++], 0x30, 0, P3);
    }
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
    printf("\ngpu/gpu-shape-time28\n");
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
    printf("shape                  bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-21s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-shape-time28: Done.\n");
    for (;;) VSync(0);
}
