/*
 * gpu/gpu-wh - what a draw costs by width and height, by kind, and by place (2026-11-01)
 *
 * WHY. The first SCPH-5501's boot of 2026-11-01 reads plain draws slower than any log before it: flat 8x8 69 -> 85, 16x16 183 -> 215, 8x128 789 -> 1,045, 2 a
 * row and 2 a draw. But flat 64x4 pays 2 a row where a 15-bit sprite 64x4 pays nothing, 4x64 about 1 a row, and semi-transparent rows nothing. So the cost
 * is not simply a row's. A second opinion (ChatGPT) put the matrix first; it has to run before the RESET that may end the state, and again after it.
 *
 * WHAT IT DOES. Interrupts off, no disc, in the vertical blanking, 20,000 cycles after the VBlank IRQ. The command's words stored back to back, D cycles of
 * waiting, one GPUSTAT read, D binary-searched for the first at which bit 26 reads set (as hwtests/gpu-shape-time; one frame a probe). Kinds: 0 flat 60h,
 * 1 flat with the mask checked (E6h bit 1), 2 semi-transparent 62h, 3 15-bit sprite 65h on cached texels, 4 VRAM copy 80h, each at widths 1, 2, 3, 4, 6, 8,
 * 12, 16, 24, 32, 64, 128 and heights 1..8, 16, 32, 64 at (192,128); then flat 8x16 at places across VRAM.
 *
 * READ IT LIKE THIS. W kind w h d, and P x y d for the places. d is cycles from the last word's store to the read, plus a constant shared with this core.
 * No assertions - compare against your emulator, and against the same console after a RESET.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-wh --ntsc --frames=30000 --quiet=20000`.
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
    else { W[0] = 0x80000000u; W[1] = xy(512, 256); W[2] = P; W[3] = S; WN = 4; }
}
static const uint8_t WS[12] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 64, 128 }, HS[11] = { 1, 2, 3, 4, 5, 6, 7, 8, 16, 32, 64 };
static uint16_t res[5][12][11], place[40];
static const uint16_t PL[28][2] = { {0,0},{1,0},{2,0},{3,0},{4,0},{8,0},{15,0},{16,0},{17,0},{32,0},{64,0},{128,0},{256,0},{512,0},{1000,0},{192,1},{192,2},{192,3},{192,16},{192,127},{192,128},{192,255},{192,256},{192,257},{192,300},{192,400},{192,480},{192,495} };

int main() {
    printf("\ngpu/gpu-wh\n");
    printf("W kind w h d; P x y d. d the first wait at which GPUSTAT bit 26 reads set, in the blanking. No assertions.\n\n");
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
    for (int k = 0; k < 5; k++) {
        write32(GP0, k == 1 ? 0xE6000002u : 0xE6000000u);
        for (int a = 0; a < 12; a++) {
            uint32_t hint = 0;
            for (int b = 0; b < 11; b++) {
                shape(k, WS[a], HS[b], 192, 128);
                waitReady(); probe(MAXD / 16); waitReady(); probe(MAXD / 16); waitReady();   /* the code and the texels warm */
                const uint32_t d = search(0);
                res[k][a][b] = (uint16_t)d; hint = d;
            }
            (void)hint;
        }
    }
    write32(GP0, 0xE6000000u);
    for (int i = 0; i < 28; i++) {
        shape(0, 8, 16, PL[i][0], PL[i][1]);
        waitReady(); probe(MAXD / 16); waitReady();
        place[i] = (uint16_t)search(0);
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int k = 0; k < 5; k++) for (int a = 0; a < 12; a++) for (int b = 0; b < 11; b++) printf("W %d %d %d %u\n", k, WS[a], HS[b], (unsigned)res[k][a][b]);
    for (int i = 0; i < 28; i++) printf("P %d %d %u\n", PL[i][0], PL[i][1], (unsigned)place[i]);
    printf("\ngpu/gpu-wh: Done.\n");
    for (;;) VSync(0);
}
