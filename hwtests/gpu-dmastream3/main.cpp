/*
 * gpu/gpu-dmastream3 - a DMA stream's floor: how long a packet takes to reach the GPU, apart from its set-up (2026-10-13
 * night).
 *
 * WHY. gpu-dmastream / -dmastream2 on the SCPH-9001 and 7501: a stream of small primitives runs at max(drawing, set-up +
 * the packet's delivery) - gouraud legs-16 triangles 191 cycles each (set-up 169), gouraud-textured 281 (254), level
 * lines 35 (17), flat legs-4 triangles 42.8 (26). A model with the packet's words reaching the GPU as they come off the
 * bus (header 5 + burst 4 + a word a cycle after the FIFO empties) gives the first three within 1% and the flat
 * triangles 3.4 cycles dear (46.2): does a flat triangle start its set-up before its last word (its first edge, v1 - v0,
 * is in after the third word)? And is a word a cycle?
 *
 * WHAT IT DOES. As gpu-dmastream2 (N = 1, 8, 32 packets in a DMA2 linked list, timed from the CHCR write to DMA done and
 * bits 28+26; fastest of 3 runs x 8 start points in the blanking; (32-8)/24 x10 the per-packet rate in tenths of a
 * cycle). Kinds, at (192,128):
 *   flat 4         the flat legs-4 triangle (0,0),(4,0),(0,4) - dmastream2's
 *   flat 4 nop1/2/4  the same with 1, 2, 4 GP0(00h) NOPs before it in its packet: the words' cost
 *   flat 4 BR      (4,0),(0,4),(4,4): its first edge diagonal - the set-up 2 cycles longer (gpu-shape-time5)
 *   flat 16x1      (0,0),(16,0),(0,1): one row, its set-up 2 longer than legs 4's by its first edge alone
 *   flat 4 v1last  (0,0),(0,4),(4,0) with the vertices sent v0, v2, v1 order: the first edge (0,4) - set-up as legs 4
 *   quad flat 4    (0,0),(4,0),(0,4),(4,4)
 *   gour 4         gouraud legs 4 (set-up ~167), and gour 4 nop2
 *   line 4 nop2    dmastream2's level line 4 behind 2 NOPs
 *
 * READ IT LIKE THIS. Per kind the three times and the per-packet rate, against this core's run. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dmastream3 --ntsc --frames=40000 --quiet=40000`.
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
static void tri3(const char* nm, int nops, uint32_t c, int ax, int ay, int bx, int by, int cx, int cy) {
    Kind& k = add(nm);
    for (int i = 0; i < nops; i++) W(k, 0);
    W(k, c | 0x408020u); W(k, P(ax, ay)); W(k, P(bx, by)); W(k, P(cx, cy));
}
static void defs() {
    tri3("flat 4", 0, 0x20000000u, 0, 0, 4, 0, 0, 4);
    tri3("flat 4 nop1", 1, 0x20000000u, 0, 0, 4, 0, 0, 4);
    tri3("flat 4 nop2", 2, 0x20000000u, 0, 0, 4, 0, 0, 4);
    tri3("flat 4 nop4", 4, 0x20000000u, 0, 0, 4, 0, 0, 4);
    tri3("flat 4 BR", 0, 0x20000000u, 4, 0, 0, 4, 4, 4);
    tri3("flat 16x1", 0, 0x20000000u, 0, 0, 16, 0, 0, 1);
    tri3("flat 4 v1last", 0, 0x20000000u, 0, 0, 0, 4, 4, 0);
    { Kind& k = add("quad flat 4"); W(k, 0x28408020u); W(k, P(0, 0)); W(k, P(4, 0)); W(k, P(0, 4)); W(k, P(4, 4)); }
    { Kind& k = add("gour 4"); W(k, 0x30408020u); W(k, P(0, 0)); W(k, 0x00204080u); W(k, P(4, 0)); W(k, 0x00802040u); W(k, P(0, 4)); }
    { Kind& k = add("gour 4 nop2"); W(k, 0); W(k, 0); W(k, 0x30408020u); W(k, P(0, 0)); W(k, 0x00204080u); W(k, P(4, 0)); W(k, 0x00802040u); W(k, P(0, 4)); }
    { Kind& k = add("line 4 nop2"); W(k, 0); W(k, 0); W(k, 0x40408020u); W(k, P(0, 0)); W(k, P(3, 0)); }
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
    printf("\ngpu/gpu-dmastream3\n");
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
    printf("\ngpu/gpu-dmastream3: Done.\n");
    for (;;) VSync(0);
}
