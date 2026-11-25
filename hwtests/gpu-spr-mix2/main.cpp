/*
 * gpu/gpu-spr-mix2 - A SEMI-TRANSPARENT SPRITE'S WRITES AS A QUEUE, AND A SPRITE'S MIXED ROWS AFTER GP0(01h) (the GPU session,
 * 2026-11-25).
 *
 * WHY. hwtests/gpu-spr-mix on the PSone, the second SCPH-5501, the SCPH-7001 and the SCPH-9001 (STATUS "gpu-spr-mix on four
 * consoles") gave the OPAQUE sprite's rule (experiments/audit/land_sprblk.js) and left two things it could not decide:
 *  - a SEMI sprite (66h) reads as a write queue: 16 rows alike cost by the pairs from a row's first drawn pair to its END, drawn or
 *    not (200 / 216 / 232 / 248 at 4 / 5 / 6 / 7 pairs, 319 at 8, 183 at 2 - nothing at 2), while a drawn row followed by a clear
 *    one is nearly free (rows alternating 174 against the opaque 167), six drawn rows then ten clear +31 and ten clear then six
 *    drawn +52 over the opaque. That is a writer slower than the fetch with a buffer between - but it measured one width, one
 *    height, five first pairs and three row orders: not the writer's cost a pair, the buffer's depth, or the first row's start.
 *  - an 8-bit sprite after GP0(01h) (two texel fills a row): over the all-clear 686, a row whose first drawn pixel is 0-1 costs
 *    +68, 2-4 +51, 7 +35, 8 +51, 12 +35, 15 +4 - not monotonic, six places measured; and a 4-bit row drawing ONLY its last pixel
 *    costs +4 after GP0(01h) where it costs +19 after the same sprite.
 *
 * WHAT IT DOES. gpu-spr-mix's exact timing (interrupts off, the sprite's four words stored back to back, D cycles in a 3-cycle
 * loop, ONE GPUSTAT read; D binary-searched for bit 26 at four start points in the vertical blanking, one probe a frame). One
 * texture slot a depth, written again for each row of the table: a W x H sprite at (0,128) whose row r draws pixels a .. W-1
 * (index 1, its CLUT entry 7FFFh) if the row is one of its drawn rows, and nothing (index 0, 0000h) otherwise. 64h and 66h, 4-bit
 * (page 512,256) and 8-bit (page 640,256); "prev" after the same sprite, "cold" after GP0(01h).
 *   first      16 x 16, a = 0 .. 15, every row drawn: 4-bit 64h and 66h, prev and cold; 8-bit 64h and 66h cold (and two prev)
 *   widths     8 x 16 (a 0 2 4 6 7), 32 x 16 (a 0 2 4 8 12 16 20 24 28 30 31), 64 x 16 (a 0 16 32 48 56 62): 4-bit prev, 64h / 66h;
 *              32 x 16 8-bit cold; 32 x 16 and 64 x 16 4-bit cold at a few a
 *   heights    16 wide, a 0, H = 1 2 3 4 5 6 8 12 32 (64h / 66h, 4-bit prev) and a 8, H = 1 2 3 4 8 (66h): the first rows, before
 *              the queue is full
 *   rows       16 x 16, a 0: dK (the first K rows drawn, the rest clear), cK (the last K drawn), alt11 / alt22 / alt31 / alt13 /
 *              alt44 (N drawn, M clear, repeating) - 64h / 66h 4-bit prev; a few at a 8, at 32 wide, and 8-bit cold
 *
 * READ IT LIKE THIS. Per row bit 26's D, fastest..slowest over the four starts. "first" 66h prev by a: the writer's cost by the
 * span; "heights": T(H) - T(H-1) is a row's cost while the queue fills, then the steady one; "rows" dK: where the drawn rows stop
 * being free is the buffer's depth. 8-bit cold by a: where the fills land against the first drawn texel. PREDICTED.txt has this
 * core's rows. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-mix2 --emu`.
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


/* a row of the table: a W x H sprite, row r drawn from pixel a to its end if bit r of rows is set */
struct Shape { char name[40]; uint8_t depth, prev, w, h, a; uint32_t rows; uint32_t wd[4]; };
#define MAXS 420
static Shape S[MAXS];
static int NS = 0;
static void spr(uint32_t c, int depth, int prev, int w, int h, int a, uint32_t rows, const char* rname) {
    if (NS >= MAXS) return;
    Shape& s = S[NS++]; s.depth = (uint8_t)depth; s.prev = (uint8_t)prev; s.w = (uint8_t)w; s.h = (uint8_t)h; s.a = (uint8_t)a; s.rows = rows;
    sprintf(s.name, "%d %02x %s %2dx%-2d a%-2d %s", depth ? 8 : 4, (unsigned)c, prev ? "prev" : "cold", w, h, a, rname);
    s.wd[0] = (c << 24) | 0x808080u; s.wd[1] = xy(0, 128); s.wd[2] = uv(0, 0, clutOf(depth)); s.wd[3] = ((uint32_t)h << 16) | (uint32_t)w;
}
static const uint32_t ALL = 0xFFFFFFFFu;
/* the first k of h rows drawn; the last k; n drawn then m clear, repeating */
static uint32_t firstK(int k) { return k >= 32 ? ALL : ((1u << k) - 1u); }
static uint32_t lastK(int h, int k) { return firstK(h) & ~firstK(h - k); }
static uint32_t alt(int n, int m) { uint32_t v = 0; for (int r = 0; r < 32; r++) if (r % (n + m) < n) v |= 1u << r; return v; }
static void rowsOf(uint32_t c, int depth, int prev, int w, int a, bool few) {
    static const int DK[7] = { 1, 2, 3, 4, 5, 8, 12 }, CK[5] = { 1, 2, 3, 4, 8 }, AN[5] = { 1, 2, 3, 1, 4 }, AM[5] = { 1, 2, 1, 3, 4 };
    char nm[12];
    for (int i = 0; i < 7; i++) { if (few && (DK[i] == 3 || DK[i] == 5 || DK[i] == 12)) continue; sprintf(nm, "d%d", DK[i]); spr(c, depth, prev, w, 16, a, firstK(DK[i]), nm); }
    for (int i = 0; i < 5; i++) { if (few && (CK[i] == 2 || CK[i] == 3)) continue; sprintf(nm, "c%d", CK[i]); spr(c, depth, prev, w, 16, a, lastK(16, CK[i]), nm); }
    for (int i = 0; i < 5; i++) { if (few && i > 1) continue; sprintf(nm, "alt%d%d", AN[i], AM[i]); spr(c, depth, prev, w, 16, a, alt(AN[i], AM[i]), nm); }
}
static void defs() {
    static const uint32_t CM[2] = { 0x64, 0x66 };
    static const int A8[5] = { 0, 2, 4, 6, 7 }, A32[11] = { 0, 2, 4, 8, 12, 16, 20, 24, 28, 30, 31 }, A64[6] = { 0, 16, 32, 48, 56, 62 };
    static const int HS[9] = { 1, 2, 3, 4, 5, 6, 8, 12, 32 }, HS8[5] = { 1, 2, 3, 4, 8 };
    static const int C32[5] = { 0, 8, 16, 24, 31 }, C64[5] = { 0, 16, 32, 48, 63 }, B32[9] = { 0, 4, 8, 12, 16, 20, 24, 28, 31 };
    /* first: 16 x 16, a 0..15 */
    for (int c = 0; c < 2; c++) for (int pv = 1; pv >= 0; pv--) for (int a = 0; a < 16; a++) spr(CM[c], 0, pv, 16, 16, a, ALL, "all");
    for (int c = 0; c < 2; c++) for (int a = 0; a < 16; a++) spr(CM[c], 1, 0, 16, 16, a, ALL, "all");
    for (int c = 0; c < 2; c++) { spr(CM[c], 1, 1, 16, 16, 0, ALL, "all"); spr(CM[c], 1, 1, 16, 16, 8, ALL, "all"); }
    spr(0x64, 0, 1, 16, 16, 0, 0, "none"); spr(0x64, 0, 0, 16, 16, 0, 0, "none"); spr(0x64, 1, 0, 16, 16, 0, 0, "none");
    /* widths */
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 5; i++) spr(CM[c], 0, 1, 8, 16, A8[i], ALL, "all");
        for (int i = 0; i < 11; i++) spr(CM[c], 0, 1, 32, 16, A32[i], ALL, "all");
        for (int i = 0; i < 6; i++) spr(CM[c], 0, 1, 64, 16, A64[i], ALL, "all");
        for (int i = 0; i < 9; i++) spr(CM[c], 1, 0, 32, 16, B32[i], ALL, "all");
        for (int i = 0; i < 5; i++) spr(CM[c], 0, 0, 32, 16, C32[i], ALL, "all");
        for (int i = 0; i < 5; i++) spr(CM[c], 0, 0, 64, 16, C64[i], ALL, "all");
    }
    spr(0x64, 0, 1, 8, 16, 0, 0, "none"); spr(0x64, 0, 1, 32, 16, 0, 0, "none"); spr(0x64, 0, 1, 64, 16, 0, 0, "none");
    spr(0x64, 1, 0, 32, 16, 0, 0, "none"); spr(0x64, 0, 0, 32, 16, 0, 0, "none"); spr(0x64, 0, 0, 64, 16, 0, 0, "none");
    /* heights */
    for (int c = 0; c < 2; c++) for (int i = 0; i < 9; i++) spr(CM[c], 0, 1, 16, HS[i], 0, ALL, "all");
    for (int i = 0; i < 5; i++) spr(0x66, 0, 1, 16, HS8[i], 8, ALL, "all");
    for (int i = 0; i < 9; i++) spr(0x64, 0, 1, 16, HS[i], 0, 0, "none");
    /* rows */
    for (int c = 0; c < 2; c++) rowsOf(CM[c], 0, 1, 16, 0, false);
    rowsOf(0x66, 0, 1, 16, 8, true);
    for (int c = 0; c < 2; c++) rowsOf(CM[c], 0, 1, 32, 0, true);
    for (int c = 0; c < 2; c++) rowsOf(CM[c], 1, 0, 16, 0, true);
}

/* the shape's texture at its depth's slot (u 0, v 0): index 1 where drawn, 0 elsewhere (by the CPU, the GPU idle around it) */
static void texture(const Shape& s) {
    const int per = s.depth ? 2 : 4, wHalf = s.w / per, x = s.depth ? 640 : 512, y = 256;
    waitReady();
    write32(GP0, 0xA0000000u); write32(GP0, xy(x, y)); write32(GP0, ((uint32_t)s.h << 16) | (uint32_t)wHalf);
    uint32_t word = 0; int half = 0;
    for (int r = 0; r < s.h; r++) for (int k = 0; k < wHalf; k++) {
        uint32_t h = 0;
        if ((s.rows >> r) & 1u) for (int t = 0; t < per; t++) if (k * per + t >= s.a) h |= 1u << (t * (16 / per));
        if (half) { write32(GP0, word | (h << 16)); half = 0; } else { word = h; half = 1; }
    }
    if (half) write32(GP0, word);
    waitReady();
}

static uint32_t probe(const Shape& s, uint32_t d) { const uint32_t n = d / 3, k = d - 3 * n; return probe4(s.wd, n, k); }
/* before a probe: "prev" - the same sprite drawn and finished; "cold" - GP0(01h), the caches empty */
static void prime(const Shape& s) {
    waitReady();
    if (s.prev) { for (int i = 0; i < 4; i++) write32(GP0, s.wd[i]); } else write32(GP0, 0x01000000u);
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
    printf("\ngpu/gpu-spr-mix2\n");
    printf("A 4- / 8-bit sprite W x H (64h, its semi twin 66h), its four words stored back to back, D cycles, one GPUSTAT read: the first\n");
    printf("D with bit 26 set, fastest..slowest over four start points in the blanking. A drawn row draws pixels a .. W-1; the rows: all,\n");
    printf("none, dK (the first K drawn), cK (the last K drawn), altNM (N drawn, M clear, repeating). cold: after GP0(01h). prev: after\n");
    printf("the same sprite, finished. No assertions.\n\n");
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
    printf("\ngpu/gpu-spr-mix2: Done.\n");
    for (;;) VSync(0);
}
