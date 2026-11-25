/*
 * gpu/gpu-spr-mix - A 16-ROW SPRITE WHOSE ROWS MIX DRAWN AND TRANSPARENT TEXELS, AND A SEMI-TRANSPARENT ONE THAT DRAWS (the GPU
 * session, 2026-11-25).
 *
 * WHY. hwtests/gpu-replay (a game's own draws on the consoles; experiments/fuzz-all/gpuseedscore.js) on v0.377.382: Ridge Racer and
 * FF7 within ~4 cycles RMS, and the largest misses left are Valkyrie Profile's and FF7's SPRITES - read against what each sprite is
 * (experiments/gpureach/sprfeat.js): a 4-bit or 8-bit sprite that draws every texel or none is within 3, while one whose rows are
 * partly transparent misses by its pattern (a 16x16 whose rows draw only pixels 12-15: 203 on four consoles, 188 here; ten clear
 * rows then six drawn: -6; 8-bit 32x16 / 48x16 with ragged rows -13 / -19, another +13), and a semi-transparent 16x16 that draws
 * all 256 texels reads 503 against 475 (-28). hwtests/gpu-spr-fill / -fill2, the sprite fills' law (GPU_FILL_WB's sprites), time
 * only rows that draw EVERY texel or NONE, and semi only where nothing is drawn. Textured draws with some transparent texels are
 * 13% of the GPU's busy time in games.js's seven games, semi sprites 1% (experiments/gpureach/).
 *
 * WHAT IT DOES. gpu-spr-fill's exact timing (interrupts off, the sprite's four words stored back to back, D cycles in a 3-cycle
 * loop, ONE GPUSTAT read; D binary-searched for bit 26 and for bit 28, at four start points in the vertical blanking, one probe a
 * frame). Sprites W x 16 at (x,128), the games' commands - 64h (modulated, 808080h) and 66h (its semi twin) - from textures whose
 * texels are index 1 (drawn: its CLUT entry 7FFFh) or index 0 (clear: 0000h), 4-bit (page 512,256) and 8-bit (page 640,256):
 *   the grid   16 x 16, every row the same 16-bit mask (bit k = pixel k drawn): ffff 0000 | 000f 00f0 0f00 f000 (four drawn) |
 *              fff0 ff0f f0ff 0fff (four clear) | 00ff ff00 07ff | 0001 0080 0100 8000 (one pixel) | 5555 aaaa 3333 cccc
 *   by rows    r10c6d (ten clear rows, then six drawn), r6d10c, ralt (drawn and clear rows alternating)
 *   the games' w15 (Valkyrie Profile's window 15, semi: full rows thinning to none) and w12 (its 8-bit 16x16)
 *   each at x 0 and x 18 (the games' usual place in a 32-pixel write block), "cold" (GP0(01h) before the probe: the texel and
 *   CLUT caches empty) and "prev" (the same sprite drawn, and waited for, before the probe)
 *   raw twins  65h / 67h of ffff, 0000, f000 and 00ff (4-bit, x 0) - gpu-spr-fill's commands
 *   x 2        ffff and f000 at x 2 (window 8's own place)
 *   wide       the games' ragged 8-bit sprites: w18 (32x16), w22 (32x16), w23 (48x16) at x 0 and at their own x mod 32
 *
 * READ IT LIKE THIS. Per row bit 26's D and bit 28's, fastest..slowest over the four starts. ffff against 0000: what the writes
 * cost a row; the four-drawn and four-clear rows: whether a drawn run's place in the row matters; r10c6d against the uniform rows:
 * whether rows cost independently; 66h against 64h on every pattern: what semi costs a drawn pixel on these rows; cold less prev:
 * the fills and the CLUT's reload. PREDICTED.txt has this core's rows. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-mix --emu`.
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
static uint32_t tpage(int depth) { return (depth ? 10u : 8u) | (1u << 4) | ((uint32_t)depth << 7); }   /* 4-bit X 512, 8-bit X 640; Y 256 */
static uint32_t clutOf(int depth) { return (uint32_t)(480 + depth) << 6; }                             /* (0,480) / (0,481) */

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

/* the patterns: 16 rows of masks, bit k = pixel k drawn; a slot is (u 64 col, v 16 row) in its page */
#define NPAT 26
static const char* const PNAME[NPAT] = { "ffff", "0000", "000f", "00f0", "0f00", "f000", "fff0", "ff0f", "f0ff", "0fff", "00ff", "ff00", "07ff",
    "0001", "0080", "0100", "8000", "5555", "aaaa", "3333", "cccc", "r10c6d", "r6d10c", "ralt", "w15", "w12" };
static const uint16_t UNI[21] = { 0xffff, 0x0000, 0x000f, 0x00f0, 0x0f00, 0xf000, 0xfff0, 0xff0f, 0xf0ff, 0x0fff, 0x00ff, 0xff00, 0x07ff,
    0x0001, 0x0080, 0x0100, 0x8000, 0x5555, 0xaaaa, 0x3333, 0xcccc };
static const uint16_t W15[16] = { 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xefdf, 0xcfdb, 0x8dd9, 0x05d9, 0x01d0, 0x0010, 0, 0, 0 };
static const uint16_t W12[16] = { 0x3ff8, 0x3ff8, 0x3ff8, 0x3ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8, 0x1ff8 };
struct Mask { uint32_t lo, hi; };
static uint32_t rowMask(int p, int r) {
    if (p < 21) return UNI[p];
    if (p == 21) return r < 10 ? 0 : 0xffff;
    if (p == 22) return r < 6 ? 0xffff : 0;
    if (p == 23) return (r & 1) ? 0 : 0xffff;
    if (p == 24) return W15[r];
    return W12[r];
}
static uint32_t bitOf(const Mask& m, int k) { return k < 32 ? (m.lo >> k) & 1u : (m.hi >> (k - 32)) & 1u; }
/* the games' wide 8-bit sprites (Valkyrie Profile's windows 18, 22, 23), slots (u 128, v 0 / 16 / 32) */
#define NWIDE 3
static const char* const WNAME[NWIDE] = { "w18", "w22", "w23" };
static const int WW[NWIDE] = { 32, 32, 48 }, WX[NWIDE] = { 25, 30, 0 };
static const Mask WIDE[NWIDE][16] = {
    { { 0x3ffffffu, 0x0u }, { 0x3ffffffu, 0x0u }, { 0x3ffffffu, 0x0u }, { 0x3ffffffu, 0x0u }, { 0x3ffffffu, 0x0u }, { 0x1ffffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u }, { 0xfffffu, 0x0u } },
    { { 0, 0 }, { 0, 0 }, { 0x3f800000u, 0x0u }, { 0x3fc00000u, 0x0u }, { 0x3fc00000u, 0x0u }, { 0x3fe00000u, 0x0u }, { 0x7ff00000u, 0x0u }, { 0x7ff80000u, 0x0u }, { 0x7ff80000u, 0x0u }, { 0x7ffc0000u, 0x0u }, { 0x7ffe0000u, 0x0u }, { 0x7fff0000u, 0x0u }, { 0x7fff0000u, 0x0u }, { 0x7fff8000u, 0x0u }, { 0x7fffc000u, 0x0u }, { 0x3fffe000u, 0x0u } },
    { { 0xffffe000u, 0x3u }, { 0xffffe000u, 0x0u }, { 0x3ffff000u, 0x0u }, { 0x3ffff800u, 0x0u }, { 0x7ffff800u, 0x0u }, { 0x7ffff000u, 0x0u }, { 0xfffff000u, 0x0u }, { 0xfffffe00u, 0x0u }, { 0xfffffe00u, 0xfu }, { 0xfffffc00u, 0xfu }, { 0xfffffc00u, 0x3u }, { 0xfffffc00u, 0x3u }, { 0xfffffc00u, 0x3u }, { 0xfffffc00u, 0xffu }, { 0xffffffffu, 0xfffu }, { 0xffffffffu, 0x7fffu } } };

/* a W x 16 texture of index 1 / 0 by the masks, at the slot's place in the depth's page (by the CPU, the GPU idle around it) */
static void texture(int depth, int u0, int v0, int w, const Mask* rows) {
    const int per = depth ? 2 : 4, wHalf = w / per, x = (depth ? 640 : 512) + u0 / per, y = 256 + v0;
    waitReady();
    write32(GP0, 0xA0000000u); write32(GP0, xy(x, y)); write32(GP0, ((uint32_t)16 << 16) | (uint32_t)wHalf);
    uint32_t word = 0; int half = 0;
    for (int r = 0; r < 16; r++) for (int k = 0; k < wHalf; k++) {
        uint32_t h = 0;
        for (int t = 0; t < per; t++) if (bitOf(rows[r], k * per + t)) h |= 1u << (t * (16 / per));
        if (half) { write32(GP0, word | (h << 16)); half = 0; } else { word = h; half = 1; }
    }
    if (half) write32(GP0, word);
    waitReady();
}

struct Shape { char name[26]; uint8_t depth, prev; uint32_t w[4]; };
#define MAXS 500
static Shape S[MAXS];
static int NS = 0;
static void spr(uint32_t c, int depth, int x, int u0, int v0, int w, int prev, const char* pat) {
    if (NS >= MAXS) return;
    Shape& s = S[NS++]; s.depth = (uint8_t)depth; s.prev = (uint8_t)prev;
    sprintf(s.name, "%d %02x x%-2d %s %s", depth ? 8 : 4, (unsigned)c, x, prev ? "prev" : "cold", pat);
    s.w[0] = (c << 24) | 0x808080u; s.w[1] = xy(x, 128); s.w[2] = uv(u0, v0, clutOf(depth)); s.w[3] = (16u << 16) | (uint32_t)w;
}
static void defs() {
    static const uint32_t CM[2] = { 0x64, 0x66 }, RAW[2] = { 0x65, 0x67 };
    static const int XS[2] = { 0, 18 }, TW[4] = { 0, 1, 5, 10 };
    for (int d = 0; d < 2; d++) for (int p = 0; p < NPAT; p++) for (int c = 0; c < 2; c++) for (int xi = 0; xi < 2; xi++) for (int pv = 0; pv < 2; pv++)
        spr(CM[c], d, XS[xi], 64 * (p / 16), 16 * (p % 16), 16, pv, PNAME[p]);
    for (int i = 0; i < 4; i++) for (int c = 0; c < 2; c++) for (int pv = 0; pv < 2; pv++) spr(RAW[c], 0, 0, 0, 16 * TW[i], 16, pv, PNAME[TW[i]]);
    for (int i = 0; i < 2; i++) for (int c = 0; c < 2; c++) for (int pv = 0; pv < 2; pv++) spr(CM[c], 0, 2, 0, 16 * (i ? 5 : 0), 16, pv, PNAME[i ? 5 : 0]);
    for (int i = 0; i < NWIDE; i++) for (int xi = 0; xi < 2; xi++) for (int pv = 0; pv < 2; pv++) { if (xi && WX[i] == 0) continue; spr(0x64, 1, xi ? WX[i] : 0, 128, 16 * i, WW[i], pv, WNAME[i]); }
}

static uint32_t probe(const Shape& s, uint32_t d) { const uint32_t n = d / 3, k = d - 3 * n; return probe4(s.w, n, k); }
/* before a probe: "prev" - the same sprite drawn and finished; "cold" - GP0(01h), the caches empty */
static void prime(const Shape& s) {
    waitReady();
    if (s.prev) { for (int i = 0; i < 4; i++) write32(GP0, s.w[i]); } else write32(GP0, 0x01000000u);
    waitReady();
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 4095;
static uint32_t search(const Shape& s, uint32_t bit, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        prime(s);
        sync(j);
        const uint32_t st = probe(s, mid);
        if (st & bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static uint16_t r26[MAXS][2], r28[MAXS][2];
int main() {
    printf("\ngpu/gpu-spr-mix\n");
    printf("A 4- / 8-bit sprite W x 16 (64h, its semi twin 66h; 65h / 67h raw), its four words stored back to back, D cycles, one GPUSTAT\n");
    printf("read: the first D with bit 26 set and with bit 28 set, fastest..slowest over four start points in the blanking. Its rows by a\n");
    printf("mask (bit k = pixel k drawn, the rest transparent): 16 rows alike, or r10c6d / r6d10c / ralt by rows, or a game's own (w..).\n");
    printf("cold: after GP0(01h). prev: after the same sprite, finished. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | tpage(0));
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x02000000u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));                    /* both pages: index 0 */
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));                        /* the CLUTs: 7FFFh ... */
    waitReady();
    for (int d = 0; d < 2; d++) {                                                                         /* ... and entry 0 0000h */
        write32(GP0, 0xA0000000u); write32(GP0, xy(0, 480 + d)); write32(GP0, (1u << 16) | 2u); write32(GP0, 0x7FFF0000u); waitReady();
    }
    for (int d = 0; d < 2; d++) for (int p = 0; p < NPAT; p++) {
        Mask rows[16]; for (int r = 0; r < 16; r++) { rows[r].lo = rowMask(p, r); rows[r].hi = 0; }
        texture(d, 64 * (p / 16), 16 * (p % 16), 16, rows);
    }
    for (int i = 0; i < NWIDE; i++) texture(1, 128, 16 * i, WW[i], WIDE[i]);
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        prime(S[i]); probe(S[i], 3000); waitReady(); prime(S[i]); probe(S[i], 3000); waitReady();   /* the code and the texels warm */
        uint32_t a = 0xFFFFFFFFu, b = 0, c = 0xFFFFFFFFu, d = 0;
        for (int j = 0; j < 4; j++) {
            const uint32_t x = search(S[i], 0x04000000u, j), y = search(S[i], 0x10000000u, j);
            a = x < a ? x : a; b = x > b ? x : b; c = y < c ? y : c; d = y > d ? y : d;
        }
        r26[i][0] = (uint16_t)a; r26[i][1] = (uint16_t)b; r28[i][0] = (uint16_t)c; r28[i][1] = (uint16_t)d;
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("row                         bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-26s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-spr-mix: Done.\n");
    for (;;) VSync(0);
}
