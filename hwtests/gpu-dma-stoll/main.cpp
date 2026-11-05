/*
 * gpu/gpu-dma-stoll - gpu-dma-storun's dense RAM stores during a GPU LINKED-LIST transfer: how many stores the walk's gaps let through,
 * node by node, for packets of P words (2026-11-05, for the SCPH-5501).
 *
 * WHY. gpu-dma-storun, -storow, -stolast and -stoiter measured the CPU's stores and reads against SyncMode 1 uploads, and v0.377.281-282
 * model them - for SyncMode 1 only: a linked list keeps the older rules (two stores a gap). A game's frame is drawn by a linked list (its
 * ordering table) while the CPU writes the next frame's packets: a homebrew's own CPU time read 2.4% above this emulator's in its
 * drawing (STATUS "gpu-dma-stolast4", the Portal note - later found to be the console's printf over the serial cable inside the
 * window it timed, 0.2% once taken out). This times the stores against a list.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. A linked list of 256 nodes in RAM, each a header (the next node's address,
 * P words) and P words of GP0(00h) (a NOP the GPU takes at once; P 1, 4, 8, 15); CHCR2 started in SyncMode 2 (the kick), then R (300)
 * units of `sw t4, 0(cell); addiu; k nops; bnez; nop` (k 0, 4, 8), then MADR2 read (the node the walk is at) and a Timer 2 read; then
 * CHCR2 polled to the end. Four tries at four places of the refresh. A line: L P k try t node end (node: how many nodes the walk passed).
 *
 * READ IT LIKE THIS. Stores a node = R / node, the node period = t / node, for each P and k. No assertions - compare against your
 * emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-stoll --frames=3000 --quiet=1500`.
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
#define NODES 256
#define MAXP 15

static uint32_t list[NODES * (MAXP + 1)] __attribute__((aligned(16)));
static volatile uint32_t cell[64] __attribute__((aligned(16)));
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

#define STK(K) \
static __attribute__((noinline, noclone)) uint32_t st##K(uint32_t r, uint32_t chcr, uint32_t *madr) { \
    uint32_t t0, t1, m; \
    __asm__ volatile( \
        ".set push\n.set noreorder\n" \
        "lui   $t0, 0x1f80\n" \
        "move  $t4, %3\n" \
        "lhu   %0, 0x1120($t0)\n" \
        "sw    %4, 0x10a8($t0)\n" \
        "1:\n" \
        "sw    $t4, 0(%5)\n" \
        "addiu $t4, $t4, -1\n" \
        ".rept " #K "\nnop\n.endr\n" \
        "bnez  $t4, 1b\n" \
        "nop\n" \
        "lw    %2, 0x10a0($t0)\n" \
        "lhu   %1, 0x1120($t0)\n" \
        "nop\n" \
        ".set pop\n" \
        : "=&r"(t0), "=&r"(t1), "=&r"(m) \
        : "r"(r), "r"(chcr), "r"(cell) \
        : "t0", "t4", "memory"); \
    *madr = m & 0xFFFFFFu; return (uint16_t)(t1 - t0); \
}
STK(0) STK(4) STK(8)
typedef uint32_t (*Stk)(uint32_t, uint32_t, uint32_t *);
static const Stk STF[3] = { st0, st4, st8 };
static const uint32_t KS[3] = { 0, 4, 8 };

static void build(uint32_t p) {
    for (uint32_t n = 0; n < NODES; n++) {
        uint32_t *node = &list[n * (p + 1)];
        const uint32_t next = n + 1 < NODES ? ((uint32_t)&list[(n + 1) * (p + 1)] & 0xFFFFFFu) : 0xFFFFFFu;
        node[0] = (p << 24) | next;
        for (uint32_t w = 1; w <= p; w++) node[w] = 0x00000000u;   /* GP0(00h): a NOP */
    }
}

static void one(uint32_t p, int ki, int tr, uint32_t ph, int print) {
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(MADR2, (uint32_t)list & 0xFFFFFCu);
    write32(BCR2, 0);
    for (int i = 0; i < 4000; i++) { const uint32_t d = (ph - read16(T2_COUNT)) & 0xFFu; if (d < 8) break; }
    uint32_t madr = 0;
    const uint32_t t = STF[ki](300, 0x01000401u, &madr);
    const uint16_t s0 = read16(T2_COUNT);
    for (int i = 0; i < 20000 && (read32(CHCR2) & 0x01000000u); i++) {}
    const uint16_t e = read16(T2_COUNT);
    gpuIdle();
    write32(GP1, 0x04000000u);
    const uint32_t base = (uint32_t)list & 0xFFFFFFu;
    const uint32_t node = madr >= base && madr < base + NODES * (p + 1) * 4 ? (madr - base) / ((p + 1) * 4) : (madr == 0xFFFFFFu ? NODES : 999);
    if (print) printf("L %u %u %d %u %u %u\n", (unsigned)p, (unsigned)KS[ki], tr, (unsigned)t, (unsigned)node, (unsigned)(uint16_t)(e - s0));
}

int main() {
    printf("\ngpu/gpu-dma-stoll\n");
    printf("L P k try t node end (300 units of sw to RAM; addiu; k nops; bnez; nop during a GPU linked list of 256 nodes of P NOP words; t their cycles, node the nodes the walk had passed (MADR2), end the list's rest to its poll). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    static const uint32_t PS[4] = { 1, 4, 8, 15 };
    for (int pi = 0; pi < 4; pi++) {
        build(PS[pi]);
        for (int ki = 0; ki < 3; ki++) one(PS[pi], ki, 0, 0, 0);   /* cache the code (not printed) */
        for (int ki = 0; ki < 3; ki++) for (int tr = 0; tr < 4; tr++) one(PS[pi], ki, tr, (uint32_t)tr * 64u, 1);
    }
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-stoll: Done.\n");
    for (;;) VSync(0);
}
