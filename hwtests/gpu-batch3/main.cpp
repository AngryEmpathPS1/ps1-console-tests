/*
 * gpu/gpu-batch3 - does a rectangle hide part of its cost under the command before it by its COMMAND (fixed size against
 * variable size) or by its SIZE (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-batch2 on the SCPH-1001 (two runs alike): a flat 16x16 rectangle (78h, fixed size) costs ~30
 * cycles less after any long primitive than this core charges, alone the same as here; a semi one and a sprite hide
 * nothing. But gpu-batch's sixteen flat 64x64 rectangles (60h, variable size) back to back cost each what this core
 * charges with no overlap (2,487.5 against 2,484.5), where its sixteen 16x16 (78h) hide ~25 apiece - and the gpu-scan4 /
 * gpu-scan5 batches on the SCPH-7501 (16x16, 78h) hide ~29. psx-spx's FIFO prefetch: a busy GPU takes 1 word of a fixed
 * size untextured rectangle and 2 of a variable-size or textured one ("not all parameters") - so neither runs fully
 * ahead. Which is it: the command, or the size?
 *
 * WHAT IT DOES. As gpu-batch2: interrupts off, Timer 2 at the system clock, in the vertical blanking, each cell the
 * fastest of 3 runs at 8 start points an eighth of a line apart, from the first GP0 word to GPUSTAT bits 28+26.
 *  - ALONE and AFTER A: twelve rectangles B - flat 78h 16x16 and 60h 16x16, 70h 8x8 and 60h 8x8, 68h 1x1 and 60h 1x1,
 *    60h 32x32, 60h 64x16, 60h 16x64, 60h 64x64, a 15-bit sprite 7Dh 16x16 and 65h 16x16 (cached texels) - alone, after
 *    a flat 64x64 (60h) and after a 64x64 fill, B's words written as soon as bit 28 reads set after A's.
 *  - BATCHES: N = 1, 2, 4, 8 of 78h 16x16, 60h 16x16, 70h 8x8, 60h 8x8, 60h 32x32 and 60h 64x64, each written when bit
 *    28 reads set (the words precomputed, unlike gpu-batch).
 * A at (0,16), B at (192,128) (a batch's i-th at (192 + 8(i&3), 128 + 8(i>>2))); texture page (512,256).
 *
 * READ IT LIKE THIS. Against this core: B after A less B alone, by command and by size. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-batch3 --ntsc --frames=4000 --quiet=4000`.
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

struct Prim { const char* name; int n; uint32_t w[4]; };
static Prim A[2], B[12];
static Prim batch[6][8];
static const char* const BN[6] = { "78h 16", "60h 16", "70h 8", "60h 8", "60h 32", "60h 64" };

/* B kind k at (x,y) into p */
static void mk(int k, int x, int y, Prim* p) {
    switch (k) {
    case 0:  p->name = "78h 16x16"; p->n = 2; p->w[0] = 0x78408020u; p->w[1] = xy(x, y); break;
    case 1:  p->name = "60h 16x16"; p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(16, 16); break;
    case 2:  p->name = "70h 8x8";   p->n = 2; p->w[0] = 0x70408020u; p->w[1] = xy(x, y); break;
    case 3:  p->name = "60h 8x8";   p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(8, 8); break;
    case 4:  p->name = "68h 1x1";   p->n = 2; p->w[0] = 0x68408020u; p->w[1] = xy(x, y); break;
    case 5:  p->name = "60h 1x1";   p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(1, 1); break;
    case 6:  p->name = "60h 32x32"; p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(32, 32); break;
    case 7:  p->name = "60h 64x16"; p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(64, 16); break;
    case 8:  p->name = "60h 16x64"; p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(16, 64); break;
    case 9:  p->name = "60h 64x64"; p->n = 3; p->w[0] = 0x60408020u; p->w[1] = xy(x, y); p->w[2] = xy(64, 64); break;
    case 10: p->name = "7Dh 16x16"; p->n = 3; p->w[0] = 0x7D808080u; p->w[1] = xy(x, y); p->w[2] = uv(0, 0, CLUT); break;
    default: p->name = "65h 16x16"; p->n = 4; p->w[0] = 0x65808080u; p->w[1] = xy(x, y); p->w[2] = uv(0, 0, CLUT); p->w[3] = xy(16, 16); break;
    }
}
static const int BK[6] = { 0, 1, 2, 3, 6, 9 };

static void defs() {
    A[0].name = "flat 64x64"; A[0].n = 3; A[0].w[0] = 0x60204080u; A[0].w[1] = xy(0, 16); A[0].w[2] = xy(64, 64);
    A[1].name = "fill 64x64"; A[1].n = 3; A[1].w[0] = 0x02204080u; A[1].w[1] = xy(0, 16); A[1].w[2] = xy(64, 64);
    for (int k = 0; k < 12; k++) mk(k, 192, 128, &B[k]);
    for (int j = 0; j < 6; j++) for (int i = 0; i < 8; i++) mk(BK[j], 192 + 8 * (i & 3), 128 + 8 * (i >> 2), &batch[j][i]);
}

static void put(const Prim* p) { for (int j = 0; j < p->n; j++) write32(GP0, p->w[j]); }
static void sync(int j) {
    waitReady();
    write32(GP0, 0xE1000400u | TPAGE);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static uint32_t finish(uint32_t t0) {
    uint32_t t;
    for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 4000000u) break; }
    return t;
}
/* a, then b (if any) when bit 28 reads set */
static uint32_t pair(const Prim* a, const Prim* b) {
    uint32_t best = 0xFFFFFFFFu;
    for (int j = 0; j < 8; j++) for (int r = 0; r < 3; r++) {
        sync(j);
        const uint32_t t0 = now();
        put(a);
        if (b) { while (!(read32(GP1) & 0x10000000u) && now() - t0 < 4000000u) {} put(b); }
        const uint32_t t = finish(t0);
        best = t < best ? t : best;
    }
    return best;
}
/* n of batch kind k, each when bit 28 reads set */
static uint32_t run(int k, int n) {
    uint32_t best = 0xFFFFFFFFu;
    for (int j = 0; j < 8; j++) for (int r = 0; r < 3; r++) {
        sync(j);
        const uint32_t t0 = now();
        for (int i = 0; i < n; i++) {
            while (!(read32(GP1) & 0x10000000u) && now() - t0 < 4000000u) {}
            put(&batch[k][i]);
        }
        const uint32_t t = finish(t0);
        best = t < best ? t : best;
    }
    return best;
}

static uint32_t tA[2], tB[12], tAB[2][12], tN[6][4];

int main() {
    printf("\ngpu/gpu-batch3\n");
    printf("Rectangles by command and size, alone and after a long primitive, and in batches; fastest of 3 runs x 8 start\n");
    printf("points in the blanking, cycles from the first word to bits 28+26. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    waitReady();
    for (int a = 0; a < 2; a++) tA[a] = pair(&A[a], 0);
    for (int b = 0; b < 12; b++) tB[b] = pair(&B[b], 0);
    for (int a = 0; a < 2; a++) for (int b = 0; b < 12; b++) tAB[a][b] = pair(&A[a], &B[b]);
    static const int NN[4] = { 1, 2, 4, 8 };
    for (int k = 0; k < 6; k++) for (int j = 0; j < 4; j++) tN[k][j] = run(k, NN[j]);
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("alone A:  %s %u  %s %u\n\n", A[0].name, (unsigned)tA[0], A[1].name, (unsigned)tA[1]);
    printf("B            alone  after %s  after %s   (after - T(A))\n", A[0].name, A[1].name);
    for (int b = 0; b < 12; b++)
        printf("%-10s %7u %8u %6d %8u %6d\n", B[b].name, (unsigned)tB[b], (unsigned)tAB[0][b], (int)(tAB[0][b] - tA[0]),
               (unsigned)tAB[1][b], (int)(tAB[1][b] - tA[1]));
    printf("\nbatch          N=1      2      4      8\n");
    for (int k = 0; k < 6; k++) printf("%-10s %7u %6u %6u %6u\n", BN[k], (unsigned)tN[k][0], (unsigned)tN[k][1], (unsigned)tN[k][2], (unsigned)tN[k][3]);
    printf("\ngpu/gpu-batch3: Done.\n");
    for (;;) VSync(0);
}
