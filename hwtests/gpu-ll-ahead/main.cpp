/*
 * gpu/gpu-ll-ahead - how far a GPU linked list runs ahead behind a drawing triangle (2026-11-05 night, for the second SCPH-5501)
 *
 * WHY. psx-spx (the live site, Rix 2026-11-05): "The DMA reads the next node header when the GPU is not holding the request and at most
 * one word is queued in the FIFO ... behind a drawing triangle a chain of 1-word nodes gets two nodes in (a 1-word node is always
 * followed at once by the next one), a chain of 2-, 4- or 8-word nodes, triangles or rectangles gets one, and a GP0(E1h) node plus a
 * 3-word sprite node go in as a pair ... Header-only nodes (empty OT entries) add no words, so with the FIFO empty a chain of them is
 * walked to the end within a few hundred cycles." This emulator sends a node only when the FIFO is EMPTY (M223, from TUTO3's bus
 * capture: packets behind a textured triangle reach the bus 0-9 cycles after it starts to draw) - the same for nodes of 2 words and
 * more, one node fewer after a 1-word node. Every game's ordering table holds 1-word nodes (GP0(E1h) and the like).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, GP1(04h)=2. A list: a flat triangle across most of a 320x240 area (thousands
 * of cycles of drawing), then 32 nodes of one kind K - 0 header only, 1 / 2 / 4 / 8 GP0(00h) words, S a GP0(E1h) node and a 3-word 8x8
 * sprite node in turn, E 1-word GP0(E1h) nodes, F 4-word ones - and the end. CHCR2 01000401h, then D cycles (200, 1,000, 3,000; Timer 2 polled) and MADR2 and GPUSTAT read
 * back to back: MADR2 is the next node the list will fetch. Then the list and the GPU waited out. 3 tries a cell at 3 places of the
 * DRAM refresh. A line: A K D try n s - n the nodes of the 32 already fetched (from MADR2), s GPUSTAT bits 28 / 26 / 25 as 0 or 1.
 *
 * READ IT LIKE THIS. n at D 200 and 1,000 (the triangle still drawing): Rix's reading gives K 1 two, K 2 / 4 / 8 one, K S two (a pair),
 * K 0 all 32; an empty-FIFO rule gives K 1 and K S one. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-ll-ahead --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define NODES 32

static uint32_t list[8 + NODES * 9 + 4] __attribute__((aligned(16)));
static uint32_t nodeAt[NODES + 1];   /* each chain node's address (masked as MADR2 reads it); [NODES] the end node */
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }
static uint32_t addr24(const uint32_t* p) { return (uint32_t)p & 0xFFFFFFu; }

/* the list for kind k: the triangle node, NODES chain nodes, the end */
static void build(int k) {
    uint32_t* p = list;
    uint32_t* tri = p;
    p += 5;   /* header + 4 words: GP0(20h) flat triangle */
    tri[1] = 0x20406080u; tri[2] = (0u << 16) | 0u; tri[3] = (0u << 16) | 319u; tri[4] = (239u << 16) | 160u;
    for (int i = 0; i < NODES; i++) {
        nodeAt[i] = addr24(p);
        int words = 0;
        if (k == 'E' || k == 'F') {   /* GP0(E1h) words: a state command, not a NOP (E one a node, F four) */
            words = k == 'E' ? 1 : 4;
            for (int w = 0; w < words; w++) p[1 + w] = 0xE1000400u;
        } else if (k == 'S') {
            if (i & 1) { p[1] = 0x74808080u; p[2] = (100u << 16) | 100u; p[3] = 0u; words = 3; }   /* GP0(74h) 8x8 sprite: colour, x y, u v clut */
            else { p[1] = 0xE1000400u; words = 1; }                                               /* GP0(E1h) the texpage as set */
        } else {
            words = k;
            for (int w = 0; w < words; w++) p[1 + w] = 0x00000000u;                                  /* GP0(00h) */
        }
        const uint32_t* next = p + 1 + words;
        p[0] = ((uint32_t)words << 24) | addr24(next);
        p = (uint32_t*)next;
    }
    nodeAt[NODES] = addr24(p);
    p[0] = 0x00FFFFFFu;   /* the end */
    tri[0] = (4u << 24) | nodeAt[0];
}
static uint32_t waitD(uint32_t t0, uint32_t d) { uint32_t t; do { t = read16(T2_COUNT); } while (((t - t0) & 0xFFFFu) < d); return t; }
static void place(uint32_t p) { for (int i = 0; i < 4000; i++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; } }
static int fetched(uint32_t m) { m &= 0xFFFFFFu; for (int i = 0; i <= NODES; i++) if (nodeAt[i] == m) return i; if (m == 0xFFFFFFu) return NODES; return -1; }

static const int KS[8] = { 0, 1, 2, 4, 8, 'S', 'E', 'F' };
static const uint32_t DS[3] = { 200, 1000, 3000 };
static int16_t resN[8][3][3]; static uint8_t resS[8][3][3];
static void one(int ki, int di, int tr, int keep) {
    build(KS[ki]);
    gpuIdle();
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000002u);
    write32(MADR2, addr24(list));
    write32(BCR2, 0);
    place((uint32_t)tr * 85u);
    const uint32_t t0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000401u);
    waitD(t0, DS[di]);
    const uint32_t m = read32(MADR2), st = read32(GP1);
    for (int i = 0; i < 2000000 && (read32(CHCR2) & 0x01000000u); i++) {}
    gpuIdle();
    write32(GP1, 0x04000000u);
    if (!keep) return;
    resN[ki][di][tr] = (int16_t)fetched(m);
    resS[ki][di][tr] = (uint8_t)(((st >> 28) & 1) << 2 | ((st >> 26) & 1) << 1 | ((st >> 25) & 1));
}
int main() {
    printf("\ngpu/gpu-ll-ahead\n");
    printf("A K D try n s (a list behind a drawing flat triangle: 32 nodes of kind K - 0 header only, 1/2/4/8 GP0(00h) words, S GP0(E1h) "
           "and a 3-word sprite in turn; n the nodes fetched D cycles after the kick (from MADR2), s GPUSTAT bits 28 26 25). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int ki = 0; ki < 8; ki++) one(ki, 0, 0, 0);   /* the code cached */
    for (int ki = 0; ki < 8; ki++) for (int di = 0; di < 3; di++) for (int tr = 0; tr < 3; tr++) one(ki, di, tr, 1);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int ki = 0; ki < 8; ki++) for (int di = 0; di < 3; di++) for (int tr = 0; tr < 3; tr++) {
        const int k = KS[ki];
        if (k >= 'A') printf("A %c", k); else printf("A %d", k);
        printf(" %u %d %d %d%d%d\n", (unsigned)DS[di], tr, (int)resN[ki][di][tr], (resS[ki][di][tr] >> 2) & 1, (resS[ki][di][tr] >> 1) & 1, resS[ki][di][tr] & 1);
    }
    printf("\ngpu/gpu-ll-ahead: Done.\n");
    for (;;) VSync(0);
}
