/*
 * gpu/gpu-dmastream2 - the DMA stream by SIZE: what of a primitive the next one hides, triangle, rectangle and line
 * (2026-10-13 night).
 *
 * WHY. hwtests/gpu-dmastream on the SCPH-7501: in a DMA2 linked list a polygon costs about max(its set-up, its drawing)
 * (M175's pipeline, v0.377.99's set-ups) - but flat and semi triangles 8-12 cycles UNDER their own drawing (a tail the
 * next overlaps), 16x16 rectangles ~10-15 under a lone one (where CPU-fed, gpu-batch3, they hide nothing), and a line
 * max(set-up, drawing) + 4 (v0.377.100). One size each; this runs the sizes, to fit the tail rather than guess it.
 *
 * WHAT IT DOES. As gpu-dmastream (a DMA2 linked list, timed from the CHCR write to DMA done and GPUSTAT 28+26, fastest of
 * 3 x 8 start points in the blanking), N = 1, 8, 32: flat triangles legs 4, 8, 16, 32; semi flat 8, 16, 32; flat
 * rectangles 78h 16x16, 60h 8x8, 16x16, 32x32; semi 62h 8x8, 16x16; a 15-bit sprite 7Dh 16x16 (cached); flat lines 4,
 * 16, 64 long; a fill 16x16; a copy 16x16.
 *
 * READ IT LIKE THIS. Each kind's (N=32 - N=8) / 24 against its lone cost (gpu-shape-time, -time3). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dmastream2 --ntsc --frames=12000 --quiet=12000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u
#define DPCR    0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t CLUT = (480u << 6) | 0u;
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */

struct Kind { const char* name; int n; uint32_t w[10]; };
static Kind K[20];
static int NK = 0;
static Kind& add(const char* nm) { Kind& k = K[NK++]; k.name = nm; k.n = 0; return k; }
static void W(Kind& k, uint32_t v) { k.w[k.n++] = v; }
static uint32_t P(int x, int y) { return xy(192 + x, 128 + y); }
static void tri(const char* nm, uint32_t c, int L) { Kind& k = add(nm); W(k, c | 0x408020u); W(k, P(0, 0)); W(k, P(L, 0)); W(k, P(0, L)); }
static void rect(const char* nm, uint32_t c, int w, int h) { Kind& k = add(nm); W(k, c | 0x408020u); W(k, P(0, 0)); if ((c >> 27) == 0xC) W(k, xy(w, h)); }
static void defs() {
    const uint32_t T = 0x808080u;
    tri("tri flat 4", 0x20000000u, 4); tri("tri flat 8", 0x20000000u, 8); tri("tri flat 16", 0x20000000u, 16); tri("tri flat 32", 0x20000000u, 32);
    tri("tri semi 8", 0x22000000u, 8); tri("tri semi 16", 0x22000000u, 16); tri("tri semi 32", 0x22000000u, 32);
    rect("rect 78h 16", 0x78000000u, 16, 16); rect("rect 60h 8", 0x60000000u, 8, 8); rect("rect 60h 16", 0x60000000u, 16, 16);
    rect("rect 60h 32", 0x60000000u, 32, 32); rect("semi 62h 8", 0x62000000u, 8, 8); rect("semi 62h 16", 0x62000000u, 16, 16);
    { Kind& k = add("sprite 7Dh 16"); W(k, 0x7D000000u | T); W(k, P(0, 0)); W(k, uv(0, 0, CLUT)); }
    { Kind& k = add("line 4"); W(k, 0x40408020u); W(k, P(0, 0)); W(k, P(3, 0)); }
    { Kind& k = add("line 16"); W(k, 0x40408020u); W(k, P(0, 0)); W(k, P(15, 0)); }
    { Kind& k = add("line 64"); W(k, 0x40408020u); W(k, P(0, 0)); W(k, P(63, 0)); }
    { Kind& k = add("fill 16"); W(k, 0x02408020u); W(k, P(0, 0)); W(k, xy(16, 16)); }
    { Kind& k = add("copy 16"); W(k, 0x80000000u); W(k, xy(512, 300)); W(k, P(0, 0)); W(k, xy(16, 16)); }
}
static uint32_t list[32 * 6 + 4];
/* the linked list of n packets of kind k; returns its address */
static uint32_t build(const Kind& k, int n) {
    uint32_t* p = list;
    for (int i = 0; i < n; i++) {
        uint32_t* next = p + 1 + k.n;
        const uint32_t link = (i == n - 1) ? 0x00FFFFFFu : ((uint32_t)next & 0x00FFFFFFu);
        p[0] = ((uint32_t)k.n << 24) | link;
        for (int j = 0; j < k.n; j++) p[1 + j] = k.w[j];
        p = next;
    }
    return (uint32_t)list & 0x00FFFFFFu;
}
static void sync(int j) {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static uint32_t once(uint32_t head, int j) {
    sync(j);
    write32(GP1, 0x04000002u);   /* DMA direction CPU -> GP0 */
    write32(D2_MADR, head);
    write32(D2_BCR, 0);
    const uint32_t t0 = now();
    write32(D2_CHCR, 0x01000401u);
    uint32_t t;
    for (;;) {
        const uint32_t c = read32(D2_CHCR), s = read32(GP1);
        t = now() - t0;
        if (!(c & 0x01000000u) && (s & 0x14000000u) == 0x14000000u) break;
        if (t > 4000000u) break;
    }
    write32(GP1, 0x04000000u);
    return t;
}

int main() {
    printf("\ngpu/gpu-dmastream2\n");
    printf("N primitives in a DMA2 linked list, timed from the CHCR write to DMA done and bits 28+26; fastest of 3 runs x 8\n");
    printf("start points in the blanking. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    waitReady();
    static const int NN[3] = { 1, 8, 32 };
    static uint32_t r[20][3];
    for (int k = 0; k < NK; k++) for (int i = 0; i < 3; i++) {
        const uint32_t head = build(K[k], NN[i]);
        uint32_t best = 0xFFFFFFFFu;
        once(head, 0);   /* the texels and the code warm */
        for (int j = 0; j < 8; j++) for (int rr = 0; rr < 3; rr++) { const uint32_t t = once(head, j); best = t < best ? t : best; }
        r[k][i] = best;
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("kind              N=1      8     32   (32-8)/24 x10\n");
    for (int k = 0; k < NK; k++)
        printf("%-14s %6u %6u %6u   %6u\n", K[k].name, (unsigned)r[k][0], (unsigned)r[k][1], (unsigned)r[k][2], (unsigned)((r[k][2] - r[k][1]) * 10 / 24));
    printf("\ngpu/gpu-dmastream2: Done.\n");
    for (;;) VSync(0);
}
