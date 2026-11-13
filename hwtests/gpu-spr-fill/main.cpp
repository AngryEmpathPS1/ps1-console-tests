/*
 * gpu/gpu-spr-fill - WHAT ONE TEXTURE FILL COSTS A 4- / 8-BIT SPRITE BY ITS PLACE IN THE ROW, AND WHAT A SPRITE THAT DRAWS NOTHING
 * COSTS (2026-11-13).
 *
 * WHY. The exact rows of gpu-shape-time2 / 42 / 44 / 58 / 59 (uncached raw 4- and 8-bit sprites 128 and 256 wide by 8 and 16 rows,
 * and 4-bit 64 x 200) read this core 5-64 cycles short, more with the fills and less with the rows: two terms fit them to 3
 * cycles - a fill 0.27 cycles dearer and each row about a cycle cheaper (STATUS "Proposed: a 4- / 8-bit sprite's fill costs
 * 14.23 ticks and a row's first 1.68 less"). In every one of those rows the first texel misses, so "the fill at a row's first
 * texel is cheaper" and "a row that has any fill is cheaper" print the same. And every one of them DRAWS its texels: ps1-tests'
 * gpu/bandwidth times a 320 x 240 4-bit sprite whose texels are all transparent, on the display, and reads this core 2.5% slow
 * where the drawn rows read it fast - nothing times a sprite that draws nothing in the blanking.
 *
 * THE CACHE, as this core has it: a 4-bit sprite's row has FOUR lines of 16 texels (the texture's 4-word group k sits in line
 * k & 3), an 8-bit row EIGHT of 8. So 64 texels across fit and are fetched once; a wider row evicts as it goes.
 *
 * WHAT IT DOES. gpu-shape-time42's exact timing (interrupts off, the words stored back to back, D cycles in a 3-cycle loop, ONE
 * GPUSTAT read, D binary-searched for bit 26 and for bit 28, at eight start points in the vertical blanking, one probe a
 * frame). Raw sprites (65h) W x 16 at (0,128) over a page at (512,256) whose texels are all non-zero, with a CLUT that draws
 * them ("drawn": every entry F8F8F8) or one that is all 0000h ("clear": nothing is written), each:
 *   A  drawn again and again, no priming: 4-bit W 64 (0 fills a row), 128 (8), 256 (16), 320 (20); 8-bit W 64 (0), 128 (16),
 *      256 (32)
 *   B  after a PRIMING sprite 64 x 16 from u 0 that has finished (so the row's lines hold groups 0-3, or 0-7 at 8-bit):
 *        4-bit  u 0 W 64   0 fills a row
 *               u 0 W 80   1, the row's LAST 16 texels
 *               u 48 W 80  4, none at the first texel
 *               u 64 W 64  4, the first texel's among them
 *               u 64 W 80  5, the first texel's among them
 *        8-bit  u 0 W 64 (0), u 0 W 72 (1, last), u 56 W 72 (8, none first), u 64 W 64 (8, first among them)
 *   C  4-bit 256 x 16: clear and modulated (64h), clear raw semi (67h), clear modulated semi (66h), drawn modulated (64h)
 *
 * READ IT LIKE THIS. Per row bit 26's D and bit 28's, fastest..slowest. A fill that is not a row's first: (u 48 W 80 less u 0 W
 * 80) / 3 a row. The first texel's: (u 64 W 64 less u 0 W 64) less three of those. If the two are equal the cheaper row is "a
 * row with a fill", if the first is about a cycle less it is the first fill. The same on the clear rows says what a fill costs
 * with no writes to turn round for; clear less drawn at W 64 is what a cached texel's write costs. C: whether modulation or
 * semi-transparency costs a sprite that draws nothing (gpu/bandwidth's two textured rows read alike on its console).
 * PREDICTED.txt has this core's rows before any console ran it. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-spr-fill --emu`.
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
static const uint32_t CLUTT = (484u << 6) | 0u;   /* a CLUT of 0000h: every texel transparent */
/* a sprite W x 16 at (0,128) from texel (u0,0): c = 65h raw, 64h modulated, 67h / 66h their semi twins; clear = the 0000h CLUT;
   prime = a raw 64 x 16 from u 0 drawn, and waited for, before each probe */
static void spr(const char* nm, uint32_t c, int d, int clear, int u0, int w, int prime) {
    Shape& s = S[NS++]; s.name = nm; s.depth = d; s.prime = prime;
    const uint32_t cl = clear ? CLUTT : CLUT;
    s.w[0] = (c << 24) | T; s.w[1] = xy(0, 128); s.w[2] = uv(u0, 0, cl); s.w[3] = (16u << 16) | (uint32_t)w;
    s.pw[0] = (0x65u << 24) | T; s.pw[1] = xy(0, 128); s.pw[2] = uv(0, 0, cl); s.pw[3] = (16u << 16) | 64u;
}
static void defs() {
    static char nm[48][28];
    int k = 0;
    static const int WA4[4] = { 64, 128, 256, 320 }, WA8[3] = { 64, 128, 256 };
    static const int B4[5][2] = { { 0, 64 }, { 0, 80 }, { 48, 80 }, { 64, 64 }, { 64, 80 } }, B8[4][2] = { { 0, 64 }, { 0, 72 }, { 56, 72 }, { 64, 64 } };
    for (int cl = 0; cl < 2; cl++) {
        for (int i = 0; i < 4; i++) { sprintf(nm[k], "4 %s A %dx16", cl ? "clear" : "drawn", WA4[i]); spr(nm[k++], 0x65, 0, cl, 0, WA4[i], 0); }
        for (int i = 0; i < 5; i++) { sprintf(nm[k], "4 %s B u%d w%d", cl ? "clear" : "drawn", B4[i][0], B4[i][1]); spr(nm[k++], 0x65, 0, cl, B4[i][0], B4[i][1], 1); }
    }
    for (int cl = 0; cl < 2; cl++) {
        for (int i = 0; i < 3; i++) { sprintf(nm[k], "8 %s A %dx16", cl ? "clear" : "drawn", WA8[i]); spr(nm[k++], 0x65, 1, cl, 0, WA8[i], 0); }
        for (int i = 0; i < 4; i++) { sprintf(nm[k], "8 %s B u%d w%d", cl ? "clear" : "drawn", B8[i][0], B8[i][1]); spr(nm[k++], 0x65, 1, cl, B8[i][0], B8[i][1], 1); }
    }
    sprintf(nm[k], "4 clear C 64h 256x16"); spr(nm[k++], 0x64, 0, 1, 0, 256, 0);
    sprintf(nm[k], "4 clear C 67h 256x16"); spr(nm[k++], 0x67, 0, 1, 0, 256, 0);
    sprintf(nm[k], "4 clear C 66h 256x16"); spr(nm[k++], 0x66, 0, 1, 0, 256, 0);
    sprintf(nm[k], "4 drawn C 64h 256x16"); spr(nm[k++], 0x64, 0, 0, 0, 256, 0);
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
    printf("\ngpu/gpu-spr-fill\n");
    printf("A raw 4- / 8-bit sprite W x 16, its last words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set\n");
    printf("and with bit 28 set, fastest..slowest over eight start points in the blanking. drawn / clear: its CLUT draws every texel /\n");
    printf("is all 0000h. A: drawn again and again. B: after a 64 x 16 sprite from u 0 has finished. No assertions.\n\n");
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
    printf("\ngpu/gpu-spr-fill: Done.\n");
    for (;;) VSync(0);
}
