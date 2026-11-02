/*
 * gpu/gpu-whv2 - GP0(03h) "Timing Control" at 0, 4, 8 and 15 on the kinds and widths gpu-whv left out (2026-11-02)
 *
 * WHY. The first SCPH-5501's slow boot was GP0(03h) = 2, sent by our own test (hwtests/dma-len00; STATUS 2026-11-02, "The slow boot is GP0(03h)").
 * gpu-wh at 0 and at 2 on that console price value 2 exactly: flat rectangles 2 a draw and 0.5 a row per pair of pixels, 4 pairs at most (2 a
 * row from width 8); mask-checked and semi-transparent 2 a draw; sprites as flat to 32 wide, nothing at 64; copies 2 a row. hwtests/gpu-timectl
 * timed the 16 values to a 43-cycle step only. hwtests/gpu-whv then found one rule in all 16 (a row takes at least 2 + w/2 + v cycles, the first row
 * all of v) on flat rectangles by width, but semi-transparent, mask-checked and sprite rows at 8 wide only, and no fill or line at all.
 *
 * WHAT IT DOES. As gpu-wh: interrupts off, no disc, in the vertical blanking, the command's words stored back to back, D cycles of waiting, one
 * GPUSTAT read, D binary-searched for the first at which bit 26 reads set. First the shapes as found (A), then for values 0, 4, 8, 15 (V v):
 * semi-transparent 62h and mask-checked 60h of width 1 4 16 64 at heights 1 and 32; 15-bit sprites 65h 16 and 32 wide x32; flat 5 and 7 wide x32;
 * a fill 02h 16x32 and 64x32; a flat line 40h 32 across and 32 down. GP0(03h) is left at 0.
 *
 * READ IT LIKE THIS. A/V v, then kind w h d per shape (kinds: 0 flat, 1 mask, 2 semi, 3 sprite, 6 fill, 7 line: w across, h down). d is cycles from the last
 * word's store to the read, plus a constant shared by every row: subtract value 0's. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-whv2 --ntsc --frames=30000 --quiet=20000`.
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
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */

/* The words stored back to back, n loop turns (3 cycles each) and k (0..2) NOPs, one GPUSTAT read. */
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

static uint32_t W[4]; static int WN;
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return WN == 3 ? probe3(W, n, k) : probe4(W, n, k);
}
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
}
static const uint32_t MAXD = 60000;
static uint32_t search(uint32_t hint) {
    uint32_t lo = 0, hi = MAXD + 1;
    if (hint) { lo = hint > 300 ? hint - 300 : 0; hi = hint + 300; sync(); if (probe(lo) & 0x04000000u) lo = 0; else { sync(); if (!(probe(hi) & 0x04000000u)) { lo = hi; hi = MAXD + 1; } } }
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync();
        const uint32_t st = probe(mid);
        if (st & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}
static void shape(int kind, int w, int h, int x, int y) {
    const uint32_t P = xy(x, y), F = 0x408020u, T = 0x808080u, S = xy(w, h);
    if (kind == 0 || kind == 1) { W[0] = 0x60000000u | F; W[1] = P; W[2] = S; WN = 3; }
    else if (kind == 2) { W[0] = 0x62000000u | F; W[1] = P; W[2] = S; WN = 3; }
    else if (kind == 3) { W[0] = 0x65000000u | T; W[1] = P; W[2] = uv(0, 0, CLUT); W[3] = S; WN = 4; }
    else if (kind == 4) { W[0] = 0x80000000u; W[1] = xy(512, 256); W[2] = P; W[3] = S; WN = 4; }
    else if (kind == 5) { W[0] = 0x20000000u | F; W[1] = P; W[2] = xy(x + w, y); W[3] = xy(x, y + h); WN = 4; }
    else if (kind == 6) { W[0] = 0x02000000u | F; W[1] = P; W[2] = S; WN = 3; }
    else { W[0] = 0x40000000u | F; W[1] = P; W[2] = xy(x + w, y + h); WN = 3; }
}
struct Sh { uint8_t k, w, h; };
static const Sh SH[] = {
    {2,1,1},{2,1,32},{2,4,1},{2,4,32},{2,16,1},{2,16,32},{2,64,1},{2,64,32}, {1,1,1},{1,1,32},{1,4,1},{1,4,32},{1,16,1},{1,16,32},{1,64,1},{1,64,32},
    {3,16,32},{3,32,32}, {0,5,32},{0,7,32}, {6,16,32},{6,64,32}, {7,32,0},{7,0,32} };
static const int NSH = sizeof SH / sizeof SH[0];
static uint16_t res[5][NSH];
static const int VS[4] = { 0, 4, 8, 15 };

static void all(int row) {
    for (int i = 0; i < NSH; i++) {
        write32(GP0, SH[i].k == 1 ? 0xE6000002u : 0xE6000000u);
        shape(SH[i].k, SH[i].w, SH[i].h, 192, 128);
        waitReady(); probe(MAXD / 16); waitReady(); probe(MAXD / 16); waitReady();   /* the code and the texels warm */
        res[row][i] = (uint16_t)search(0);
    }
    write32(GP0, 0xE6000000u);
}

int main() {
    printf("\ngpu/gpu-whv2\n");
    printf("A as found, then V v = GP0(03h) 0, 4, 8, 15: kind w h d per shape (0 flat, 1 mask, 2 semi, 3 sprite, 6 fill, 7 line). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    all(0);
    for (int j = 0; j < 4; j++) {
        waitReady();
        write32(GP0, 0x03000000u | (uint32_t)VS[j]);
        waitReady();
        all(j + 1);
    }
    waitReady();
    write32(GP0, 0x03000000u);
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int r = 0; r < 5; r++) {
        if (r == 0) printf("A"); else printf("V %d", VS[r - 1]);
        for (int i = 0; i < NSH; i++) printf(" %d.%d.%d:%u", SH[i].k, SH[i].w, SH[i].h, (unsigned)res[r][i]);
        printf("\n");
    }
    printf("GP0(03h) left at 0\n\ngpu/gpu-whv2: Done.\n");
    for (;;) VSync(0);
}
