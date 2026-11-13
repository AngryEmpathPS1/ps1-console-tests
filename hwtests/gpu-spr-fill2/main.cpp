/*
 * gpu/gpu-spr-fill2 - A DRAWN 4-BIT SPRITE'S ROW BY WHERE ITS FILLS FALL: start texel x width, and the destination's x
 * (2026-11-13).
 *
 * WHY. hwtests/gpu-spr-fill on the second SCPH-5501 and the PSone (twelve logs alike to a cycle or two): a clear fill costs 8.0
 * cycles in every pattern, a drawn one 10.1 and a row's first 8.9 - on rows of whole 32-pixel blocks. The drawn rows that END
 * in a part block behind a fill read 1.3-4.7 cycles a row LESS than that (u 0 W 80 one fill: -1.4; u 64 W 80: -2.3; u 48 W 80,
 * which begins with 16 cached texels: -4.6), and three rows cannot say what the rule is: the part block, the fill before it,
 * the cached run at the start, or where the fills fall in the destination's 32-pixel write blocks.
 *
 * WHAT IT DOES. gpu-spr-fill's exact timing and its priming (a raw 64 x 16 sprite from u 0, finished, before every probe - the
 * row's four cache lines hold texel groups 0-3). Raw 4-bit sprites W x 16, every texel drawn:
 *   G  the grid: u 0, 16, 32, 48, 64 by W 16, 32, 48, 64, 80, 96 at x 0 - every count of leading cached texels (64 - u, none
 *      from u 64) against every row end (whole blocks at W 32 / 64 / 96, a part block at 16 / 48 / 80)
 *   X  the same sprite at x 16 for u 0 / 48 / 64 at W 80 - the fills 16 pixels on against the destination's blocks, if the
 *      blocks are the destination's and not counted from the row's first pixel
 *   R  no priming, drawn again and again: W 96, 160, 192 (gpu-spr-fill's 128 / 256 / 320 were whole blocks too)
 *
 * READ IT LIKE THIS. Per row bit 26's D and bit 28's, fastest..slowest. Along a u row, each 16 texels more is 8 texel pairs
 * and, past the cached ones, a fill: the steps say what a fill costs by its place. X against its G twin: equal if the blocks
 * count from the row's first pixel. PREDICTED.txt has this core's rows. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-fill2 --emu`.
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

struct Shape { const char* name; int depth; int prime; uint32_t w[4]; uint32_t pw[4]; };
static Shape S[48];
static int NS = 0;
static const uint32_t T = 0x808080u;
/* a sprite W x 16 at (0,128) from texel (u0,0): c = 65h raw, 64h modulated, 67h / 66h their semi twins; clear = the 0000h CLUT;
   prime = a raw 64 x 16 from u 0 drawn, and waited for, before each probe */
static void spr(const char* nm, int x, int u0, int w, int prime) {
    Shape& s = S[NS++]; s.name = nm; s.depth = 0; s.prime = prime;
    s.w[0] = (0x65u << 24) | T; s.w[1] = xy(x, 128); s.w[2] = uv(u0, 0, CLUT); s.w[3] = (16u << 16) | (uint32_t)w;
    s.pw[0] = (0x65u << 24) | T; s.pw[1] = xy(x, 128); s.pw[2] = uv(0, 0, CLUT); s.pw[3] = (16u << 16) | 64u;
}
static void defs() {
    static char nm[48][28];
    int k = 0;
    static const int U[5] = { 0, 16, 32, 48, 64 }, W[6] = { 16, 32, 48, 64, 80, 96 }, UX[3] = { 0, 48, 64 }, WR[3] = { 96, 160, 192 };
    for (int i = 0; i < 5; i++) for (int j = 0; j < 6; j++) { sprintf(nm[k], "G u%d w%d", U[i], W[j]); spr(nm[k++], 0, U[i], W[j], 1); }
    for (int i = 0; i < 3; i++) { sprintf(nm[k], "X u%d w80 x16", UX[i]); spr(nm[k++], 16, UX[i], 80, 1); }
    for (int i = 0; i < 3; i++) { sprintf(nm[k], "R %dx16", WR[i]); spr(nm[k++], 0, 0, WR[i], 0); }
}

static uint32_t probe(const Shape& s, uint32_t d) { const uint32_t n = d / 3, k = d - 3 * n; return probe4(s.w, n, k); }
static void prime(const Shape& s) { if (!s.prime) return; waitReady(); for (int i = 0; i < 4; i++) write32(GP0, s.pw[i]); waitReady(); }
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 32000;
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

int main() {
    printf("\ngpu/gpu-spr-fill2\n");
    printf("A raw drawn 4-bit sprite W x 16, its last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set\n");
    printf("and with bit 28 set, fastest..slowest over eight start points in the blanking. G / X: after a 64 x 16 sprite from u 0 has\n");
    printf("finished, at x 0 / x 16. R: no priming, drawn again and again. No assertions.\n\n");
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
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* the texels: 7FFFh, no index 0 */
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));                        /* the CLUT that draws */
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 484)); write32(GP0, xy(256, 2));                        /* the CLUT of 0000h */
    waitReady();
    static uint32_t r26[48][2], r28[48][2];
    for (int i = 0; i < NS; i++) {
        waitReady(); write32(GP0, 0xE1000400u | tpage(S[i].depth)); waitReady();
        prime(S[i]); probe(S[i], 6000); waitReady(); prime(S[i]); probe(S[i], 6000); waitReady();   /* the code and the texels warm */
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
    printf("row                    bit 26        bit 28\n");
    for (int i = 0; i < NS; i++)
        printf("%-21s %5u..%-5u  %5u..%-5u\n", S[i].name, (unsigned)r26[i][0], (unsigned)r26[i][1], (unsigned)r28[i][0], (unsigned)r28[i][1]);
    printf("\ngpu/gpu-spr-fill2: Done.\n");
    for (;;) VSync(0);
}
