/*
 * gpu/gpu-spr-mix4 - A WIDE SPRITE'S ROWS AFTER GP0(01h): WHERE ITS WRITES FALL AMONG SEVERAL TEXEL FILLS A ROW (the GPU session,
 * 2026-11-25).
 *
 * WHY. hwtests/gpu-spr-mix3 on four consoles, read as a cost a row (experiments/gpureach/coldrows.js: T = none + first + rows x c
 * to the cycle at every first drawn pixel a), gave a bus walk that prints every OPAQUE cold row 16 and 32 wide at both depths to
 * the cycle - experiments/gpureach/opqgraph.js: a row's drawn pairs are one write of 3 + floor(pairs / 2) cycles, ready 2 after
 * the row's last pair, behind the next row's first fill, and the fill after that waits for its rest. At 64 wide (four fills a row
 * at 4 bits, a write INSIDE the row) it does not: c = 8 13 4 11 5 3 1 1 at a 0 8 16 24 32 40 48 56 and the one-row sprite 16 21 6 10
 * 12 10 8 6 over the clear one - a span that starts inside a line of texels in the row's first half pays ~7 a row more, one that
 * starts in the last line of the first 32 pixels ends 6 sooner than the last block's own write. Nine places 8 apart cannot say
 * where the blocks are or what the write inside the row waits for; the semi rows 32 and 64 wide are open the same way (+29 / 45 /
 * 23 / 31 / 17 a single row). On 24 sprite-heavy discs the sprites that fetch texels are 96% of the sprite pixels and the semi ones
 * are 93% rows wider than 16 (experiments/gpureach/semicount.js, semicount2.js).
 *
 * WHAT IT DOES. gpu-spr-mix3's exact timing and textures (a W x H sprite at (0,128); bit 26's D at four start points in the
 * blanking), every row cold: GP0(01h) before each probe. 64h and 66h:
 *   4-bit  64 wide   a 0 4 8 .. 60 62, H 1 2 4
 *   4-bit  48 wide   a 0 4 8 .. 44 46, H 1 2          (three lines: the first 32 pixels and half the next)
 *   4-bit 128 wide   a 0 8 16 24 32 40 48 64 72 80 96 112 126, H 1 2
 *   4-bit  64 wide   SPANS: pixels a .. B-1 only - 0..16 0..32 0..48 16..32 16..48 32..48 8..24 24..40, H 1 2
 *   8-bit  64 wide   a 0 4 8 .. 60 62, H 1 2 4       (eight lines of 8 texels)
 *   8-bit  48 wide   a 0 4 8 .. 44 46, H 1 2
 *   none             every size, nothing drawn
 *
 * READ IT LIKE THIS. Per row bit 26's D, fastest..slowest; the drawn sprite less "none" of its size is what the writes cost.
 * H 1: one row's own; T(2) - T(1): where the row before's writes meet this row's fills. A span against the full row from the same
 * a: what the pixels behind it add. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js hwtests/build/gpu-spr-mix4.exe --emu`.
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


/* a row of the table: a W x H sprite, every row drawn from pixel a to pixel b - 1 (nothing where a >= b) */
struct Shape { char name[40]; uint8_t depth, w, h, a, b; uint32_t wd[4]; };
#define MAXS 440
static Shape S[MAXS];
static int NS = 0;
static void spr(uint32_t c, int depth, int w, int h, int a, int b) {
    if (NS >= MAXS) return;
    Shape& s = S[NS++]; s.depth = (uint8_t)depth; s.w = (uint8_t)w; s.h = (uint8_t)h; s.a = (uint8_t)a; s.b = (uint8_t)b;
    char rname[12];
    if (a >= b) sprintf(rname, "none"); else if (b >= w) sprintf(rname, "all"); else sprintf(rname, "to%d", b);
    sprintf(s.name, "%d %02x cold %3dx%-2d a%-3d %s", depth ? 8 : 4, (unsigned)c, w, h, a, rname);
    s.wd[0] = (c << 24) | 0x808080u; s.wd[1] = xy(0, 128); s.wd[2] = uv(0, 0, clutOf(depth)); s.wd[3] = ((uint32_t)h << 16) | (uint32_t)w;
}
static void defs() {
    static const uint32_t CM[2] = { 0x64, 0x66 };
    static const int HW[3] = { 1, 2, 4 };
    static const int A128[13] = { 0, 8, 16, 24, 32, 40, 48, 64, 72, 80, 96, 112, 126 };
    static const int SP[8][2] = { { 0, 16 }, { 0, 32 }, { 0, 48 }, { 16, 32 }, { 16, 48 }, { 32, 48 }, { 8, 24 }, { 24, 40 } };
    for (int d = 0; d < 2; d++) {
        for (int hi = 0; hi < 3; hi++) {
            spr(0x64, d, 64, HW[hi], 0, 0);
            for (int c = 0; c < 2; c++) { for (int a = 0; a < 64; a += 4) spr(CM[c], d, 64, HW[hi], a, 64); spr(CM[c], d, 64, HW[hi], 62, 64); }
        }
        for (int hi = 0; hi < 2; hi++) {
            spr(0x64, d, 48, HW[hi], 0, 0);
            for (int c = 0; c < 2; c++) { for (int a = 0; a < 48; a += 4) spr(CM[c], d, 48, HW[hi], a, 48); spr(CM[c], d, 48, HW[hi], 46, 48); }
        }
        if (d) continue;
        for (int hi = 0; hi < 2; hi++) {
            spr(0x64, d, 128, HW[hi], 0, 0);
            for (int c = 0; c < 2; c++) for (int i = 0; i < 13; i++) spr(CM[c], d, 128, HW[hi], A128[i], 128);
        }
        for (int hi = 0; hi < 2; hi++) for (int c = 0; c < 2; c++) for (int i = 0; i < 8; i++) spr(CM[c], d, 64, HW[hi], SP[i][0], SP[i][1]);
    }
}

/* the shape's texture at its depth's slot (u 0, v 0): index 1 where drawn, 0 elsewhere (by the CPU, the GPU idle around it) */
static void texture(const Shape& s) {
    const int per = s.depth ? 2 : 4, wHalf = s.w / per, x = s.depth ? 640 : 512, y = 256;
    waitReady();
    write32(GP0, 0xA0000000u); write32(GP0, xy(x, y)); write32(GP0, ((uint32_t)s.h << 16) | (uint32_t)wHalf);
    uint32_t word = 0; int half = 0;
    for (int r = 0; r < s.h; r++) for (int k = 0; k < wHalf; k++) {
        uint32_t h = 0;
        for (int t = 0; t < per; t++) if (k * per + t >= s.a && k * per + t < s.b) h |= 1u << (t * (16 / per));
        if (half) { write32(GP0, word | (h << 16)); half = 0; } else { word = h; half = 1; }
    }
    if (half) write32(GP0, word);
    waitReady();
}

static uint32_t probe(const Shape& s, uint32_t d) { const uint32_t n = d / 3, k = d - 3 * n; return probe4(s.wd, n, k); }
/* before a probe: GP0(01h), the caches empty */
static void prime(const Shape& s) {
    (void)s;
    waitReady();
    write32(GP0, 0x01000000u);
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

static uint16_t r26[MAXS][2];
int main() {
    printf("\ngpu/gpu-spr-mix4\n");
    printf("A 4- / 8-bit sprite W x H (64h, its semi twin 66h), its four words stored back to back, D cycles, one GPUSTAT read: the first\n");
    printf("D with bit 26 set, fastest..slowest over four start points in the blanking. A drawn row draws pixels a .. W-1 (toB: a .. B-1;\n");
    printf("none: nothing). Every row cold: after GP0(01h). No assertions.\n\n");
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
    for (int i = 0; i < NS; i++) {
        texture(S[i]);
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        prime(S[i]); probe(S[i], 3000); waitReady(); prime(S[i]); probe(S[i], 3000); waitReady();   /* the code and the texels warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 4; j++) { const uint32_t x = search(S[i], 0x04000000u, j); a = x < a ? x : a; b = x > b ? x : b; }
        r26[i][0] = (uint16_t)a; r26[i][1] = (uint16_t)b;
    }
    waitReady();
    write32(GP0, 0xE1000400u | tpage(2));
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("row                              bit 26\n");
    for (int i = 0; i < NS; i++) printf("%-32s %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1]);
    printf("\ngpu/gpu-spr-mix4: Done.\n");
    for (;;) VSync(0);
}
