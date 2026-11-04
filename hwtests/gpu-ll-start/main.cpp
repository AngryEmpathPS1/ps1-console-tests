/*
 * gpu/gpu-ll-start - hwtests/gpu-ll-end with its S rows built: a 16-word block started 0-95 cycles after CHCR2's store of a one-node
 * GPU list, beside a second reading of its E rows (2026-11-04, for the SCPH-5501).
 *
 * WHY THE S ROWS ARE SAFE NOW. gpu-ll-end on the second SCPH-5501 (x3, console-logs/2026-11-04-SCPH-5501-u2-llend): the node starts
 * ~5 cycles after the store and holds the bus to its end, a 4-word node is over before the first probe and a 32-word one holds a
 * read from D 0. The S rows' MADR2 write comes 20 or more cycles after the store, so it cannot precede the header fetch, and no CPU
 * store lands inside the node's block. What is left of gpu-ll-end's text below is its own.
 *
 * gpu-ll-end's header:
 * WHY. hwtests/gp0-overrun on the second SCPH-5501: a node of N words reads ~69 + 1.06 N from the CHCR store to the poll that sees
 * Busy clear, this core a flat 61 - its list starts 42 cycles after the store (DMA_LL_LATENCY), after the poll's first timer read.
 * A knob core fits that test, fuzz-dma's and fuzz-bus's list rows and Redux's dma suite at once with the list starting at the blocks'
 * 5 cycles, its interrupt at its last node's end, and Busy set ~33 cycles more with the bus free (STATUS "IN FLIGHT after
 * v0.377.269"); v0.377.247's 37 with the interrupt late too hung Redux's suite. Nobody has watched the two bits, or a start written
 * between them (libgpu starts its next transfer from the DMA interrupt).
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, GP1(04h)=2, DICR's channel 2 enabled (bits 18, 23). E rows: one node of
 * N GP0(00h) words and the end code, CHCR2 01000401h, then D single cycles (a jump into nops, D = 0..95 by sleds of 32) and a raw
 * Timer 2 read, CHCR2 and DICR: while the node holds the bus the read waits for its end, so the time column stays flat and then
 * rises a cycle a D; Busy and the flag read as they are at that moment. 8 tries a cell at spread phases. S rows: the same node, and
 * at D a 16-word SyncMode 1 block of GP0(00h) (MADR2, BCR2, CHCR2 01000201h written there), then the poll; ran = MADR2 moved 64 bytes
 * on, and the cycles from the first store to the poll that reads it done. The S rows build only with -DLLSTART=<first D>, once the E
 * rows have shown the console's node over before it: a MADR2 write the walk can still meet sends RAM to GP0 as commands (the console's
 * DMA works from MADR; this core keeps the address it started with, so the emulator cannot show the danger).
 *
 * READ IT LIKE THIS. E N D tries t-fewest t-mean busy flag: t from the timer read before the store; busy and flag the tries that read
 * them set. S N D tries ran t-mean. The D where flag turns 8 is the interrupt; where busy turns 0, Busy's clear. No assertions.
 */
#define LLSTART 0   /* the S rows from D 0: see WHY THE S ROWS ARE SAFE NOW */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define DICR     0x1F8010F4u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* n & 31 more cycles, one at a time: a jump into a run of 32 nops, n from its end (cached code, 1 cycle each). */
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 32\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n & 31u) : "t0", "t1");
}

static uint32_t list[80] __attribute__((aligned(16)));
static uint32_t blk[16] __attribute__((aligned(16)));
static void build(uint32_t n) { list[0] = (n << 24) | 0x00FFFFFFu; for (uint32_t i = 1; i <= n; i++) list[i] = 0; }
static void quiet() {   /* the channel stopped, its flag acknowledged, the GPU idle */
    write32(CHCR2, 0);
    write32(DICR, 0x00840000u | 0x04000000u);
    for (int i = 0; i < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {}
}

struct E { uint16_t t; bool busy, flag; };
static __attribute__((noinline, noclone)) E probe(uint32_t n, uint32_t d) {
    quiet();
    build(n);
    write32(MADR2, (uint32_t)list & 0x1FFFFFu);
    write32(BCR2, 0);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000401u);
    for (uint32_t q = d >> 5; q; q--) sled(31);
    sled(d);
    const uint16_t a1 = read16(T2_COUNT);
    const uint32_t c = read32(CHCR2), di = read32(DICR);
    for (int i = 0; i < 400 && (read32(CHCR2) & 0x01000000u); i++) {}
    E r; r.t = (uint16_t)(a1 - a0); r.busy = (c & 0x01000000u) != 0; r.flag = (di & 0x04000000u) != 0;
    return r;
}

struct S { uint16_t t; bool ran, ok; };
static __attribute__((noinline, noclone)) S start(uint32_t n, uint32_t d) {
    quiet();
    build(n);
    for (int i = 0; i < 16; i++) blk[i] = 0;
    write32(MADR2, (uint32_t)list & 0x1FFFFFu);
    write32(BCR2, 0);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000401u);
    for (uint32_t q = d >> 5; q; q--) sled(31);
    sled(d);
    write32(MADR2, (uint32_t)blk & 0x1FFFFFu);
    write32(BCR2, (1u << 16) | 16u);
    write32(CHCR2, 0x01000201u);
    uint16_t a2 = a0; unsigned turns = 0;
    for (;;) { a2 = read16(T2_COUNT); if (!(read32(CHCR2) & 0x01000000u)) break; if (++turns > 400) break; }
    const uint32_t ma = read32(MADR2) & 0x1FFFFFu;
    S r; r.t = (uint16_t)(a2 - a0); r.ran = ma == (((uint32_t)blk & 0x1FFFFFu) + 64u); r.ok = turns <= 400;
    return r;
}

static const uint32_t NS[2] = { 4, 32 };

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-ll-start\n");
    printf("A one-node GPU list of N words: D single cycles after CHCR2's store, the time, Busy and DICR's flag (E); a 16-word block started at D (S). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint32_t dicr0 = read32(DICR);
    write32(DPCR, read32(DPCR) | 0x800u);
    write32(GP1, 0x04000002u);
    probe(4, 0); probe(4, 40); start(4, 0);   /* cache it */
    for (int k = 0; k < 2; k++) {
        const uint32_t n = NS[k];
        for (uint32_t d = 0; d < 96; d++) {
            unsigned tries = 0, busy = 0, flag = 0, lo = 0xFFFF; uint32_t sum = 0;
            for (int i = 0; i < 8; i++) {
                waitCycles(300 + (uint32_t)(i * 37 % 256));
                const E r = probe(n, d);
                tries++; sum += r.t; if (r.t < lo) lo = r.t; if (r.busy) busy++; if (r.flag) flag++;
            }
            printf("E %2u %2u %u %4u %4u.%u %u %u\n", (unsigned)n, (unsigned)d, tries, lo, (unsigned)(sum / tries), (unsigned)(sum * 10 / tries % 10), busy, flag);
        }
#ifdef LLSTART
        /* only once the E rows have put the console's node end before D: a MADR2 write the walk can still meet sends RAM to GP0 as
         * commands (the console's DMA works from MADR, this core keeps the address it started with) - and a stray GP0(03h) is what
         * held the first 5501 in its slow boot */
        for (uint32_t d = LLSTART; d < 96; d += 2) {
            unsigned tries = 0, ran = 0, bad = 0; uint32_t sum = 0;
            for (int i = 0; i < 8; i++) {
                waitCycles(300 + (uint32_t)(i * 37 % 256));
                const S r = start(n, d);
                if (!r.ok) { bad++; continue; }
                tries++; sum += r.t; if (r.ran) ran++;
            }
            printf("S %2u %2u %u %u %4u stuck %u\n", (unsigned)n, (unsigned)d, tries, ran, tries ? (unsigned)(sum / tries) : 0, bad);
        }
#endif
    }
    quiet();
    write32(DICR, dicr0 & 0x00FFFFFFu);
    write32(GP1, 0x04000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-ll-start: Done.\n");
    for (;;) {}   /* park after Done. */
}
