/*
 * gpu/gpu-batch2 - which primitive's set-up runs under which one's drawing: pairs, and the second one's words arriving
 * at chosen times around the first one's end (2026-10-13 morning).
 *
 * WHY. hwtests/gpu-batch on the SCPH-1001 (two runs alike): sixteen flat 16x16 rectangles fed one at a time on
 * GPUSTAT bit 28 cost ~235 cycles each after the first where one alone costs this core's 261 and the console's own single
 * sits on it (374 / 369 with the feeding) - ~30 cycles of each rectangle hidden under the one before - and the same for
 * 64x64 ones and for 15-bit sprites on cached texels, while SEMI 16x16 rectangles hide nothing (alone 613, each next
 * ~493). gpu-scan4 / gpu-scan5 on the SCPH-7501: sixteen flat 16x16 ~185 each where one alone is ~202. M175 found a
 * POLYGON set up while the one before it draws; this asks which pairs do it for the rest, and how the overlap grows with
 * how early the second one's words arrive.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; in the vertical blanking, each cell the fastest of three
 * runs at each of eight start points an eighth of a line apart (20,000 + 271j cycles after the VBlank IRQ) - so a short
 * run avoids the SCPH-1001's +29 at a line's start (gpu-scanline2) and a long one crosses as few as it can - timed from
 * the first GP0 word to GPUSTAT bits 28+26.
 *  - ALONE: five long primitives A (flat rectangle 64x64, semi 64x64, fill 64x64, a flat triangle ~128x64, a flat
 *    64x64 drawn twice as two 64x32) and eight short ones B (flat 16x16 rectangle, semi 16x16, 15-bit 16x16 sprite and
 *    4-bit 16x16 sprite on the same texels every run, flat triangle ~16x16, fill 16x16, copy 16x16, flat line 16x14).
 *  - PAIRS: A then B, B's words written as soon as bit 28 reads set after A's (while A draws).
 *  - SWEEPS: A then B with B's words written at T(A) + delta cycles after A's first (T(A) this console's own A alone),
 *    delta -600 .. +200: flat 64x64 then flat 16x16, then semi 16x16, then the 15-bit sprite; semi 64x64 then flat 16x16.
 * A at (0,16), B at (192,128); the texture page at (512,256), the CLUT at (0,480).
 *
 * READ IT LIKE THIS. A pair's cost of B is T(A;B) - T(A); against B alone (less the feeding, ~the same in both) the
 * difference is what of B ran under A. A sweep's T against delta: flat where B's words arrived early enough to hide its
 * set-up, rising one for one once they arrive later than that; the knee is the hidden part. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-batch2 --frames=9000 --quiet=9000`.
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

struct Prim { const char* name; int n; int depth; uint32_t w[8]; };
static Prim A[5], B[8];

static void defs() {
    Prim* p;
    p = &A[0]; p->name = "flat 64x64";  p->depth = 2; p->n = 3; p->w[0] = 0x60204080u; p->w[1] = xy(0, 16); p->w[2] = xy(64, 64);
    p = &A[1]; p->name = "semi 64x64";  p->depth = 2; p->n = 3; p->w[0] = 0x62204080u; p->w[1] = xy(0, 16); p->w[2] = xy(64, 64);
    p = &A[2]; p->name = "fill 64x64";  p->depth = 2; p->n = 3; p->w[0] = 0x02204080u; p->w[1] = xy(0, 16); p->w[2] = xy(64, 64);
    p = &A[3]; p->name = "flat tri 128"; p->depth = 2; p->n = 4; p->w[0] = 0x20204080u; p->w[1] = xy(0, 16); p->w[2] = xy(128, 16); p->w[3] = xy(0, 80);
    p = &A[4]; p->name = "flat 2x64x32"; p->depth = 2; p->n = 6; p->w[0] = 0x60204080u; p->w[1] = xy(0, 16); p->w[2] = xy(64, 32);
                                                                p->w[3] = 0x60204080u; p->w[4] = xy(0, 48); p->w[5] = xy(64, 32);
    p = &B[0]; p->name = "flat 16";     p->depth = 2; p->n = 2; p->w[0] = 0x78408020u; p->w[1] = xy(192, 128);
    p = &B[1]; p->name = "semi 16";     p->depth = 2; p->n = 2; p->w[0] = 0x7A408020u; p->w[1] = xy(192, 128);
    p = &B[2]; p->name = "sprite15";    p->depth = 2; p->n = 3; p->w[0] = 0x7D808080u; p->w[1] = xy(192, 128); p->w[2] = uv(0, 0, CLUT);
    p = &B[3]; p->name = "sprite4";     p->depth = 0; p->n = 3; p->w[0] = 0x7D808080u; p->w[1] = xy(192, 128); p->w[2] = uv(0, 0, CLUT);
    p = &B[4]; p->name = "tri 16";      p->depth = 2; p->n = 4; p->w[0] = 0x20408020u; p->w[1] = xy(192, 128); p->w[2] = xy(208, 128); p->w[3] = xy(192, 144);
    p = &B[5]; p->name = "fill 16";     p->depth = 2; p->n = 3; p->w[0] = 0x02408020u; p->w[1] = xy(192, 128); p->w[2] = xy(16, 16);
    p = &B[6]; p->name = "copy 16";     p->depth = 2; p->n = 4; p->w[0] = 0x80000000u; p->w[1] = xy(512, 300); p->w[2] = xy(192, 128); p->w[3] = xy(16, 16);
    p = &B[7]; p->name = "line 16";     p->depth = 2; p->n = 3; p->w[0] = 0x40408020u; p->w[1] = xy(192, 128); p->w[2] = xy(207, 141);
}

static void put(const Prim* p) { for (int j = 0; j < p->n; j++) write32(GP0, p->w[j]); }

/* One timed run at start point j: a, then b (if any) when bit 28 reads set (delay < 0) or at delay cycles. */
static uint32_t once(const Prim* a, const Prim* b, int32_t delay, int j) {
    waitReady();
    write32(GP0, 0xE1000400u | tpage(b ? b->depth : a->depth));
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
    const uint32_t t0 = now();
    put(a);
    if (b) {
        if (delay < 0) { while (!(read32(GP1) & 0x10000000u) && now() - t0 < 4000000u) {} }
        else { while (now() - t0 < (uint32_t)delay) {} }
        put(b);
    }
    uint32_t t;
    for (;;) { t = now() - t0; if ((read32(GP1) & 0x14000000u) == 0x14000000u) break; if (t > 4000000u) break; }
    return t;
}
/* the fastest over eight start points x three runs */
static uint32_t cell(const Prim* a, const Prim* b, int32_t delay) {
    uint32_t best = 0xFFFFFFFFu;
    for (int j = 0; j < 8; j++) for (int r = 0; r < 3; r++) { const uint32_t t = once(a, b, delay, j); best = t < best ? t : best; }
    return best;
}

static uint32_t tA[5], tB[8], tAB[5][8];
static const int32_t DELTAS[14] = { -600, -400, -300, -200, -150, -100, -75, -50, -25, 0, 25, 50, 100, 200 };
static const int SW_A[4] = { 0, 0, 0, 1 }, SW_B[4] = { 0, 1, 2, 0 };
static uint32_t tSw[4][14];

int main() {
    printf("\ngpu/gpu-batch2\n");
    printf("Pairs of primitives, the second's words while the first draws; fastest of 3 runs x 8 start points in the\n");
    printf("blanking, cycles from the first word to bits 28+26. No assertions.\n\n");
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
    write32(GP0, 0x02F8F8F8u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 2));
    waitReady();
    for (int a = 0; a < 5; a++) tA[a] = cell(&A[a], 0, -1);
    for (int b = 0; b < 8; b++) tB[b] = cell(&B[b], 0, -1);
    for (int a = 0; a < 5; a++) for (int b = 0; b < 8; b++) tAB[a][b] = cell(&A[a], &B[b], -1);
    for (int s = 0; s < 4; s++) for (int d = 0; d < 14; d++) {
        const int32_t delay = (int32_t)tA[SW_A[s]] + DELTAS[d];
        tSw[s][d] = cell(&A[SW_A[s]], &B[SW_B[s]], delay < 0 ? 0 : delay);
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("alone A:");
    for (int a = 0; a < 5; a++) printf("  %s %u", A[a].name, (unsigned)tA[a]);
    printf("\nalone B:");
    for (int b = 0; b < 8; b++) printf("  %s %u", B[b].name, (unsigned)tB[b]);
    printf("\n\nA then B, B fed on bit 28: T(A;B)\n");
    printf("%-13s", "A \\ B");
    for (int b = 0; b < 8; b++) printf(" %8s", B[b].name);
    printf("\n");
    for (int a = 0; a < 5; a++) {
        printf("%-13s", A[a].name);
        for (int b = 0; b < 8; b++) printf(" %8u", (unsigned)tAB[a][b]);
        printf("\n");
    }
    printf("\nB's cost after A, T(A;B) - T(A)\n");
    printf("%-13s", "A \\ B");
    for (int b = 0; b < 8; b++) printf(" %8s", B[b].name);
    printf("\n");
    for (int a = 0; a < 5; a++) {
        printf("%-13s", A[a].name);
        for (int b = 0; b < 8; b++) printf(" %8d", (int)(tAB[a][b] - tA[a]));
        printf("\n");
    }
    printf("\nB's words at T(A) + delta: T\n");
    printf("%-22s", "delta");
    for (int d = 0; d < 14; d++) printf(" %5d", (int)DELTAS[d]);
    printf("\n");
    for (int s = 0; s < 4; s++) {
        char nm[32]; int k = 0;
        for (const char* c = A[SW_A[s]].name; *c && k < 12; c++) nm[k++] = *c;
        nm[k++] = ' '; nm[k++] = '+'; nm[k++] = ' ';
        for (const char* c = B[SW_B[s]].name; *c && k < 30; c++) nm[k++] = *c;
        nm[k] = 0;
        printf("%-22s", nm);
        for (int d = 0; d < 14; d++) printf(" %5u", (unsigned)tSw[s][d]);
        printf("\n");
    }
    printf("\ngpu/gpu-batch2: Done.\n");
    for (;;) VSync(0);
}
