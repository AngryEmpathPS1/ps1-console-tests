/*
 * gpu/gpu-dmastream - N primitives fed by a DMA2 linked list, each packet in the FIFO as soon as the one before frees it:
 * does a polygon's set-up run under the drawing before it (2026-10-13 night).
 *
 * WHY. hwtests/gpu-shape-time3 on the SCPH-1001 and SCPH-7501 alike: a polygon's set-up (GPUSTAT bit 28, held until it
 * draws - M175) is flat 26-30 cycles, gouraud 168-171, textured 114-120, semi none, and a lone triangle's whole cost
 * (bit 26) sits on this core's new-GPU model. v0.377.99 (candidate) gave the new GPU those set-ups and kept each lone
 * total by moving the difference into the drawing - so in a stream, where the set-up runs under the drawing before it,
 * every flat triangle cost 7 cycles more and every semi one 32: Sony's SMESH samples drew half the words in their window
 * and MIPMAP's display changed 4 times where it changed 36. gpu-batch's triangles, fed by the CPU on bit 28, read the
 * same on both cores and the 7501 (the CPU too late for the set-up to hide). A DMA stream is what the samples do.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, in the vertical blanking (eight start points an eighth of a
 * line apart, fastest of three each): a linked list of N packets (N = 1, 2, 4, 8, 16, 32) of one kind, GP1(04h) = 2,
 * DMA2 MADR = the list, CHCR 01000401h; timed from the CHCR write to DMA2 done and GPUSTAT bits 28+26 set. Kinds, all at
 * (192,128): flat triangle legs 8 and 16, semi flat 16, gouraud 16, textured 15-bit 16 (cached texels), gouraud-textured
 * 16, a flat quad 16, a flat 16x16 rectangle (78h), a semi one (7Ah), a 15-bit sprite 16x16 (7Dh, cached), a flat line
 * and a gouraud line 16 long.
 *
 * READ IT LIKE THIS. Each kind's cycles by N; the slope from 16 to 32 is a primitive's cost in a DMA stream, against
 * gpu-shape-time3's lone set-up and total. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dmastream --ntsc --frames=12000 --quiet=12000`.
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
static Kind K[12];
static int NK = 0;
static Kind& add(const char* nm) { Kind& k = K[NK++]; k.name = nm; k.n = 0; return k; }
static void W(Kind& k, uint32_t v) { k.w[k.n++] = v; }
static uint32_t P(int x, int y) { return xy(192 + x, 128 + y); }
static void defs() {
    const uint32_t F = 0x408020u, T = 0x808080u, G2 = 0x00204080u, G3 = 0x00802040u, TP = TPAGE;
    { Kind& k = add("tri flat 8");   W(k, 0x20000000u | F); W(k, P(0, 0)); W(k, P(8, 0)); W(k, P(0, 8)); }
    { Kind& k = add("tri flat 16");  W(k, 0x20000000u | F); W(k, P(0, 0)); W(k, P(16, 0)); W(k, P(0, 16)); }
    { Kind& k = add("tri semi 16");  W(k, 0x22000000u | F); W(k, P(0, 0)); W(k, P(16, 0)); W(k, P(0, 16)); }
    { Kind& k = add("tri gour 16");  W(k, 0x30000000u | F); W(k, P(0, 0)); W(k, G2); W(k, P(16, 0)); W(k, G3); W(k, P(0, 16)); }
    { Kind& k = add("tri tex 16");   W(k, 0x24000000u | T); W(k, P(0, 0)); W(k, uv(0, 0, CLUT)); W(k, P(16, 0)); W(k, uv(15, 0, TP)); W(k, P(0, 16)); W(k, uv(0, 15, 0)); }
    { Kind& k = add("tri gtex 16");  W(k, 0x34000000u | T); W(k, P(0, 0)); W(k, uv(0, 0, CLUT)); W(k, G2); W(k, P(16, 0)); W(k, uv(15, 0, TP)); W(k, G3); W(k, P(0, 16)); W(k, uv(0, 15, 0)); }
    { Kind& k = add("quad flat 16"); W(k, 0x28000000u | F); W(k, P(0, 0)); W(k, P(16, 0)); W(k, P(0, 16)); W(k, P(16, 16)); }
    { Kind& k = add("rect 78h 16");  W(k, 0x78000000u | F); W(k, P(0, 0)); }
    { Kind& k = add("rect 7Ah 16");  W(k, 0x7A000000u | F); W(k, P(0, 0)); }
    { Kind& k = add("sprite 7Dh 16"); W(k, 0x7D000000u | T); W(k, P(0, 0)); W(k, uv(0, 0, CLUT)); }
    { Kind& k = add("line flat 16"); W(k, 0x40000000u | F); W(k, P(0, 0)); W(k, P(15, 0)); }
    { Kind& k = add("line gour 16"); W(k, 0x50000000u | F); W(k, P(0, 0)); W(k, G2); W(k, P(15, 0)); }
}

static uint32_t list[32 * 11 + 4];
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
    printf("\ngpu/gpu-dmastream\n");
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
    static const int NN[6] = { 1, 2, 4, 8, 16, 32 };
    static uint32_t r[12][6];
    for (int k = 0; k < NK; k++) for (int i = 0; i < 6; i++) {
        const uint32_t head = build(K[k], NN[i]);
        uint32_t best = 0xFFFFFFFFu;
        once(head, 0);   /* the texels and the code warm */
        for (int j = 0; j < 8; j++) for (int rr = 0; rr < 3; rr++) { const uint32_t t = once(head, j); best = t < best ? t : best; }
        r[k][i] = best;
    }
    waitReady();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("kind              N=1      2      4      8     16     32\n");
    for (int k = 0; k < NK; k++)
        printf("%-14s %6u %6u %6u %6u %6u %6u\n", K[k].name, (unsigned)r[k][0], (unsigned)r[k][1], (unsigned)r[k][2], (unsigned)r[k][3], (unsigned)r[k][4], (unsigned)r[k][5]);
    printf("\ngpu/gpu-dmastream: Done.\n");
    for (;;) VSync(0);
}
