/*
 * gpu-tri-lag2: DOES A BIG TRIANGLE PAY THE CPU INTAKE'S LAG? gpu-shape-time6's own big shapes, every word a load apart (a) and
 * every word back to back (b), in shape-time6's environment (the GPU session, 2026-11-23).
 *
 * WHY. gpu-tri-lag (STATUS "gpu-tri-lag on four consoles"): a flat triangle's back-to-back words cost the consoles 5 cycles over
 * spaced, after every lead-in. land_trislope models that lag, and it reads gpu-shape-time6 worse than the core without it
 * (STATUS "trislope v3 against no trislope") - most of all on the BIG shapes, which move +7 to +37 with the lag where the
 * consoles stay where the lag-free core is. Either the shape-time constants were fitted with the lag inside them (every
 * shape-time test stores its last words back to back), and a big triangle pays the lag like a small one; or a big triangle
 * does not pay it. b - a on these shapes says which.
 *
 * WHAT. gpu-tri-height's probe (VBlank + 1,500 cycles, Timer 0's line sync, T0 = 64; the words a load apart or back to back;
 * D cycles after the last word, ONE GPUSTAT read; D binary-searched for bit 26 and bit 28), and gpu-shape-time6's own probe and
 * start (st6: VBlank + 20,000, its first start point; the words before the last four written first). gpu-tri-height's
 * environment for all three (its 320x240 display and drawing area; shape-time6's texture fills), its own triangle the first row. Seven of its shapes: flat TL 2, flat TL 64x16, semi TR 320x16,
 * gour TL / TR 320x16, tex TL 320x16, clip TR 128x16 -64. Twice each. No assertions.
 *
 * READ IT. Per shape, b - a: the lag a shape of that size pays. st6 against b: shape-time6's start against tri-height's.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-tri-lag2 --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
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

/* gpu-tri-height's probe, as it is there: from a fresh Timer 0 reset delay to T0 = s, the first c words a load apart (the
   last c of sixteen slots), then the last b back to back from s0-s7, delay D (n x 3 + k), read GPUSTAT */
#define SLOTA(i) \
        "lw $t7, " #i "*4(%[w])\n" \
        "nop\n" \
        "sw $t7, 0x1810($t0)\n"
static uint32_t probeB(const uint32_t* w, uint32_t c, const uint32_t* v, uint32_t b, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\n"
        "lw $s0, 0(%[v])\n" "lw $s1, 4(%[v])\n" "lw $s2, 8(%[v])\n" "lw $s3, 12(%[v])\n"
        "lw $s4, 16(%[v])\n" "lw $s5, 20(%[v])\n" "lw $s6, 24(%[v])\n" "lw $s7, 28(%[v])\n"
        "la $t9, 14f\n"
        "sll $t2, %[b], 2\n"
        "subu $t9, $t9, $t2\n"          /* 14f - 4 x b: the last b of the eight stores */
        "lui $t0, 0x1f80\n"
        "la $t1, 2f\n"
        "sll $t2, %[k], 2\n"
        "subu $t1, $t1, $t2\n"
        "la $t8, 9f\n"
        "sll $t2, %[c], 2\n"
        "subu $t8, $t8, $t2\n"
        "sll $t2, %[c], 3\n"
        "subu $t8, $t8, $t2\n"
        "5:\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "sltiu $t5, $t3, 200\n"
        "bnez $t5, 5b\n"
        "nop\n"
        "6:\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "sltiu $t5, $t3, 64\n"
        "beqz $t5, 6b\n"
        "nop\n"
        "lhu $t3, 0x1100($t0)\n"
        "nop\n"
        "subu $t4, %[s], $t3\n"
        "7:\n"
        "addiu $t4, $t4, -3\n"
        "bgtz $t4, 7b\n"
        "nop\n"
        "la $t6, 8f\n"
        "sll $t5, $t4, 2\n"
        "addiu $t6, $t6, -8\n"
        "subu $t6, $t6, $t5\n"
        "jr $t6\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "8:\n"
        "jr $t8\n"
        "nop\n"
        SLOTA(0) SLOTA(1) SLOTA(2) SLOTA(3) SLOTA(4) SLOTA(5) SLOTA(6) SLOTA(7)
        SLOTA(8) SLOTA(9) SLOTA(10) SLOTA(11) SLOTA(12) SLOTA(13) SLOTA(14) SLOTA(15)
        "9:\n"
        "jr $t9\n"
        "nop\n"
        "sw $s0, 0x1810($t0)\n"
        "sw $s1, 0x1810($t0)\n"
        "sw $s2, 0x1810($t0)\n"
        "sw $s3, 0x1810($t0)\n"
        "sw $s4, 0x1810($t0)\n"
        "sw $s5, 0x1810($t0)\n"
        "sw $s6, 0x1810($t0)\n"
        "sw $s7, 0x1810($t0)\n"
        "14:\n"
        "beqz %[n], 3f\n"
        "nop\n"
        "4:\n"
        "addiu %[n], %[n], -1\n"
        "bnez %[n], 4b\n"
        "nop\n"
        "3:\n"
        "jr $t1\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "2:\n"
        "lw %[st], 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : [st] "=r"(st), [n] "+r"(n) : [w] "r"(w), [c] "r"(c), [b] "r"(b), [v] "r"(v), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "memory");
    return st;
}


/* gpu-shape-time6's probes, as they are there */
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


/* gpu-shape-time6's probes are above, as they are there */
struct Shape { const char* name; int depth; int n; uint32_t w[8]; };
static Shape S[8]; static int NS = 0;
static const uint32_t F = 0x408020u, T = 0x808080u, G2 = 0x00204080u, G3 = 0x00802040u;
static uint32_t Q(int x0, int x, int y) { return xy(x0 + x, 128 + y); }
static void tri6(const char* nm, int c, int x0, int ax, int ay, int bx, int by, int cx, int cy) {   /* gpu-shape-time6's tri6 */
    Shape& s = S[NS++]; s.name = nm; s.depth = 2; s.n = 0;
    const bool g = (c & 0x10) != 0, t = (c & 4) != 0;
    const int vx[3] = { ax, bx, cx }, vy[3] = { ay, by, cy };
    s.w[s.n++] = ((uint32_t)c << 24) | (t ? T : F);
    for (int i = 0; i < 3; i++) {
        if (g && i > 0) s.w[s.n++] = i == 1 ? G2 : G3;
        s.w[s.n++] = Q(x0, vx[i], vy[i]);
        if (t) s.w[s.n++] = uv(vx[i] & 31, vy[i] & 31, i == 0 ? CLUT : i == 1 ? tpage(2) : 0);
    }
}
static void defs() {
    { Shape& t = S[NS++]; t.name = "ft 32x4 (tri-height)"; t.depth = 2; t.n = 4; t.w[0] = 0x20808080u; t.w[1] = xy(72, 40); t.w[2] = xy(40, 44); t.w[3] = xy(72, 44); }   /* gpu-tri-height's own, to calibrate */
    tri6("flat TL 2", 0x20, 192, 0, 0, 2, 0, 0, 2);
    tri6("flat TL 64x16", 0x20, 0, 0, 0, 64, 0, 0, 16);
    tri6("semi TR 320x16", 0x22, 0, 0, 0, 320, 0, 320, 16);
    tri6("gour TL 320x16", 0x30, 0, 0, 0, 320, 0, 0, 16);
    tri6("gour TR 320x16", 0x30, 0, 0, 0, 320, 0, 320, 16);
    tri6("tex TL 320x16", 0x24, 0, 0, 0, 320, 0, 0, 16);
    tri6("clip TR 128x16 -64", 0x20, -64, 0, 0, 128, 0, 128, 16);
}
static uint32_t TA[16], TB[8];
static void ways(const Shape& s, bool bb) {   /* tri-height's tables: c words a load apart, then b back to back */
    for (int i = 0; i < 16; i++) TA[i] = 0;
    for (int i = 0; i < 8; i++) TB[i] = 0;
    const int nb = bb ? s.n : 0, nc = s.n - nb;
    for (int i = 0; i < nc; i++) TA[16 - nc + i] = s.w[i];
    for (int i = 0; i < nb; i++) TB[8 - nb + i] = s.w[nc + i];
}
static void syncTH() {   /* gpu-tri-height's */
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 1500u) {} }
}
static void syncST6() {   /* gpu-shape-time6's, its first start point */
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
}
static uint32_t once(int sh, int way, uint32_t d) {
    const Shape& s = S[sh];
    const uint32_t n = d / 3, k = d - 3 * n;
    if (way == 2) {   /* st6: shape-time6's probe() after its sync */
        waitReady(); write32(GP0, 0xE1000000u | tpage(s.depth));
        syncST6();
        if (s.n <= 4) return s.n == 2 ? probe2(s.w, n, k) : s.n == 3 ? probe3(s.w, n, k) : probe4(s.w, n, k);
        for (int i = 0; i < s.n - 4; i++) write32(GP0, s.w[i]);
        return probe4(s.w + s.n - 4, n, k);
    }
    ways(s, way == 1);
    waitReady(); write32(GP0, 0xE1000000u | tpage(s.depth));   /* as gpu-tri-height before each probe (its E1h; this test's texture page) */
    syncTH();
    return probeB(TA, (uint32_t)(way == 1 ? 0 : s.n), TB, (uint32_t)(way == 1 ? s.n : 0), 64, n, k);
}
static uint32_t search(int sh, int way, uint32_t bit) {
    uint32_t lo = 0, hi = 8001;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (once(sh, way, mid) & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static const char* const WAY[3] = { "a (spaced)", "b (back to back)", "st6" };
static uint16_t D[8][3][2][2];
int main() {
    printf("\ngpu/gpu-tri-lag2\n");
    printf("gpu-shape-time6's shapes, every word a load apart (a), back to back (b), and by shape-time6's own probe and start\n");
    printf("(st6): bit 26's and bit 28's first D after the last word, twice each. No assertions.\n\n");
    defs();
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end (tri-height's probe waits on it) */
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | tpage(2));   /* gpu-shape-time6's environment from here */
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    /* gpu-tri-height's drawing area and display from here (a drawing's time follows the display's fetch bursts: one
     * environment for every way, the one gpu-tri-height's consoles ran - its own triangle is the calibration row) */
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (239u << 10) | 319u); write32(GP0, 0xE5000000u);
    write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320x240 */
    write32(GP1, 0x05000000u | (256u << 10));
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int i = 0; i < NS; i++) for (int v = 0; v < 3; v++) for (int r = 0; r < 2; r++) {
        if (r == 0) { once(i, v, 9000); once(i, v, 9000); }   /* the code and the texels warm */
        D[i][v][0][r] = (uint16_t)search(i, v, 0x04000000u);
        D[i][v][1][r] = (uint16_t)search(i, v, 0x10000000u);
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2)); write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  %-20s :", "");
    for (int v = 0; v < 3; v++) printf(" %-18s|", WAY[v]);
    printf("\n");
    for (int i = 0; i < NS; i++) for (int b = 0; b < 2; b++) {
        printf("  %-20s :", b ? "  bit 28" : S[i].name);
        for (int v = 0; v < 3; v++) printf(" %6u %6u      |", (unsigned)D[i][v][b][0], (unsigned)D[i][v][b][1]);
        printf("\n");
    }
    printf("\ngpu/gpu-tri-lag2: Done.\n");
    for (;;) VSync(0);
    return 0;
}
