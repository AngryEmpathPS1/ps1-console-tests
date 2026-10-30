/*
 * gpu/gpu-dmastream4 - a DMA stream's period against the set-up's first edge (2026-10-14 small hours).
 *
 * WHY. gpu-dmastream3 on the SCPH-9001: flat triangles too small to be drawing-bound stream at 42.8 / 44.4 / 47.6-48.0
 * cycles a packet with set-ups (bit 28, alone: 24 + max(2, lg|dx0|) + lg|dy0|) of 26 / 27 / 28 - about 2.5 cycles a
 * packet for each cycle of set-up, where a stream bound by set-up + delivery should move 1 for 1; gouraud ones (set-up
 * ~167) sit on set-up + delivery exactly. Is a cycle of the multiply's operand length worth more in a stream than alone,
 * or does the FIFO empty (and the next packet start) before the set-up ends by an amount that depends on it?
 *
 * WHAT IT DOES. As gpu-dmastream2/3 (N = 1, 8, 32 packets in a DMA2 linked list, timed from the CHCR write to DMA done and
 * bits 28+26; fastest of 3 runs x 8 start points in the blanking; (32-8)/24 x10 the per-packet rate in tenths of a
 * cycle). Kinds, at (192,128):
 *   row W        one-row flat triangles (0,0),(W,0),(0,1), W = 4, 8, 16, 32, 64: the first edge (W,0), set-up 24 + lg W
 *   col H        H-row slivers (0,0),(0,H),(1,0), H = 4, 8: the first edge (0,H), set-up 26 + lg H
 *   diag 4       (0,0),(4,4),(0,4): the first edge (4,4), set-up 28
 *   gour row W   one-row gouraud triangles, W = 4, 16, 64: set-up 165 + lg W
 *
 * READ IT LIKE THIS. Per kind the three times and the per-packet rate, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dmastream4 --ntsc --frames=40000 --quiet=40000`.
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
static void tri3(const char* nm, uint32_t c, int ax, int ay, int bx, int by, int cx, int cy) {
    Kind& k = add(nm);
    const bool g = (c & 0x10000000u) != 0;
    W(k, c | 0x408020u); W(k, P(ax, ay)); if (g) W(k, 0x00204080u); W(k, P(bx, by)); if (g) W(k, 0x00802040u); W(k, P(cx, cy));
}
static void defs() {
    tri3("row 4", 0x20000000u, 0, 0, 4, 0, 0, 1);
    tri3("row 8", 0x20000000u, 0, 0, 8, 0, 0, 1);
    tri3("row 16", 0x20000000u, 0, 0, 16, 0, 0, 1);
    tri3("row 32", 0x20000000u, 0, 0, 32, 0, 0, 1);
    tri3("row 64", 0x20000000u, 0, 0, 64, 0, 0, 1);
    tri3("col 4", 0x20000000u, 0, 0, 0, 4, 1, 0);
    tri3("col 8", 0x20000000u, 0, 0, 0, 8, 1, 0);
    tri3("diag 4", 0x20000000u, 0, 0, 4, 4, 0, 4);
    tri3("gour row 4", 0x30000000u, 0, 0, 4, 0, 0, 1);
    tri3("gour row 16", 0x30000000u, 0, 0, 16, 0, 0, 1);
    tri3("gour row 64", 0x30000000u, 0, 0, 64, 0, 0, 1);
}
static uint32_t list[32 * 11 + 4];   /* up to 10 words a packet and its header */
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
    printf("\ngpu/gpu-dmastream4\n");
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
    printf("\ngpu/gpu-dmastream4: Done.\n");
    for (;;) VSync(0);
}
