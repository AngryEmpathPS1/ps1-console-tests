/*
 * dma/dma1-start - what a DMA1 (MDEC.OUT) SyncMode 1 start costs the CPU, to the cycle: the start every movie decode makes.
 *
 * WHY (docs/INSTRUMENT-AUDIT.md flag #1, 2026-11-12). src/psx.js DMA_SM1_START 230 - a CPU stall from the CHCR write to the
 * first block - is M39's fit to ps1-tests' dma/chopping when GPU blocks stalled the CPU. The GPU channel has not used it since
 * M161, and every channel measured since overturned it: MDEC.IN 5 (mdec-reset: the core read 300, the consoles 75), SPU read 66
 * (fuzz-dma), SPU write 0 (dma-rates). It is still charged at every SyncMode 1 start the others are not: MDEC.OUT (DMA1, CHCR
 * 01000200h - every movie's decode), CD DMA3 and PIO. Nothing times DMA1's start: fuzz-mdec and mdec-time end on DMA1's last
 * block, which the MDEC's copy-out clock sets, so a stall of 230 at its first block is invisible in their totals.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Each try: the MDEC reset, its data-out request on, one 15-bit
 * macroblock decoded (six DC-only blocks, the command and its six words written by the CPU - no DMA0), and a wait of 30,000
 * cycles, so the whole output (128 words) is ready while DMA1 has not started. Then, from cached code: Timer 2 read, CHCR1
 * written, a jump into a nop sled of N cycles, ONE raw Timer 2 read (an I/O read: it waits for the bus). N = 0..319; four tries
 * each (each at another refresh phase: nothing locks the try to the grid), the fastest and slowest printed as t - N. The same
 * code runs a first pass storing CHCR1 = 0 (nothing starts) right before each timed one, so the sled is in the cache; the
 * CONTROL times that pass alone. Cases:
 *   1 x 32   BCR1 00010020h (one block)
 *   4 x 32   BCR1 00040020h (the macroblock, as a movie player asks)
 * And dma-rates' rows for DMA1: CHCR1 written, K = 0 / 16 ALU ops, CHCR1 read ONCE (Busy, and when), then polled (K a turn)
 * until Busy clears: the first read, the total and the turns.
 *
 * READ IT LIKE THIS. t - N CONSTANT from N = 0 (t grows one a nop): the CPU stopped at the store for all of it - t - N less the
 * CONTROL's is the start stall plus the block. t FLAT while N is small, then t - N constant: the CPU ran the nops while the
 * block moved, and the knee is where the block (and the start before it) ended. A first read BUSY means the CPU ran before the
 * block finished.
 *
 * THE GAMES THAT PIN IT (src/psx.js before DMA_SM1_START, M155): GameShark Lite (the MDEC channel: its second table write 24
 * cycles after the first CHCR must find the first block moved) and BallBlazer Champions (its movie player: no CPU before a
 * transfer's first block). Their constraint is "the CPU does not run before the first block", not the 230.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma1-start`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define MADR1   0x1F801090u
#define BCR1    0x1F801094u
#define CHCR1   0x1F801098u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define NMAX 320

uint32_t outBuf[128] __attribute__((aligned(16)));

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { uint32_t s = 0; uint16_t a = t2(); while (s < cycles) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } }

/* CHCR1 = val, a sled of n nops, one Timer 2 read: the cycles from the read before the store to the read after. Two passes in
 * one block - the first stores 0 (nothing starts) and warms every line the second runs; no other code between them (the virtual
 * console, 2026-11-12: a warm-up CALL before the timed one left sled lines evicted by the caller's own, ~5 cycles a line). */
static uint32_t __attribute__((noinline, aligned(64))) sled(uint32_t val, uint32_t n) {
    uint32_t t;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        " lui $t9, 0x1f80\n"
        " la $t8, 9f\n sll $t7, %[n], 2\n subu $t8, $t8, $t7\n"
        " li $t6, 2\n move $t5, $zero\n"
        "1:\n lhu $t0, 0x1120($t9)\n nop\n"
        " sw $t5, 0x1098($t9)\n"
        " jr $t8\n nop\n"
        " .rept 320\n nop\n .endr\n"
        "9:\n lhu $t1, 0x1120($t9)\n nop\n"
        " addiu $t6, $t6, -1\n"
        " bnez $t6, 1b\n"
        " move $t5, %[v]\n"
        " subu %[t], $t1, $t0\n andi %[t], %[t], 0xffff\n"
        ".set pop\n"
        : [t] "=&r"(t) : [v] "r"(val), [n] "r"(n) : "t0", "t1", "t5", "t6", "t7", "t8", "t9", "memory");
    return t;
}

/* dma-rates' startPoll, for one channel: write CHCR, K ALU ops, read CHCR once, then poll (K a turn) until Busy clears. */
struct Res { uint32_t first, firstAt, total, iters; };
#define WORK ".rept %[k]\n\taddu $t5, $t5, $t4\n\t.endr\n\t"
template <int K> __attribute__((noinline)) void startPoll(uint32_t chcr, uint32_t val, Res* r) {
    uint32_t first, firstAt, total, iters;
    __asm__ volatile(
        ".set push\n\t.set noreorder\n\t.set noat\n\t"
        "lui $t9, 0x1f80\n\t"
        "lhu $t0, 0x1120($t9)\n\t"
        "nop\n\t"
        "sw %[val], 0(%[chcr])\n\t"
        WORK
        "lw %[first], 0(%[chcr])\n\t"
        "lhu $t6, 0x1120($t9)\n\t"
        "nop\n\t"
        "subu %[firstAt], $t6, $t0\n\t"
        "andi %[firstAt], %[firstAt], 0xffff\n\t"
        "move %[total], %[firstAt]\n\t"
        "move $t0, $t6\n\t"
        "li %[iters], 0\n\t"
        "srl $t3, %[first], 24\n\t"
        "andi $t3, $t3, 1\n\t"
        "beqz $t3, 2f\n\t"
        "nop\n"
        "1:\n\t"
        WORK
        "lw $t3, 0(%[chcr])\n\t"
        "lhu $t6, 0x1120($t9)\n\t"
        "addiu %[iters], %[iters], 1\n\t"
        "subu $t7, $t6, $t0\n\t"
        "andi $t7, $t7, 0xffff\n\t"
        "addu %[total], %[total], $t7\n\t"
        "move $t0, $t6\n\t"
        "srl $t3, $t3, 24\n\t"
        "andi $t3, $t3, 1\n\t"
        "beqz $t3, 2f\n\t"
        "sltiu $t3, %[iters], 0x7fff\n\t"
        "bnez $t3, 1b\n\t"
        "nop\n"
        "2:\n\t"
        ".set pop\n\t"
        : [first] "=&r"(first), [firstAt] "=&r"(firstAt), [total] "=&r"(total), [iters] "=&r"(iters)
        : [chcr] "r"(chcr), [val] "r"(val), [k] "i"(K)
        : "t0", "t3", "t4", "t5", "t6", "t7", "t9", "memory");
    r->first = first; r->firstAt = firstAt; r->total = total; r->iters = iters;
    if (iters >= 0x7FFF) write32(chcr, read32(chcr) & ~0x01000000u);   /* given up: the channel stopped */
}

static int stuck;
/* One 15-bit DC-only macroblock decoded, its output ready, DMA1 set to take `blocks` blocks of 32. */
static void prepare(uint32_t blocks) {
    write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);                       /* reset */
    write32(MDEC1, 0x20000000u);                       /* data-out request on (DMA1) */
    write32(MDEC0, (1u << 29) | (3u << 27) | 6u);      /* decode, 15 bpp, 6 words */
    for (int b = 0; b < 6; b++) write32(MDEC0, 0xFE000400u | (uint32_t)(b * 16));   /* a block: q 1 + a DC, then the end code */
    wait(30000);
    if (read32(MDEC1) & 0x80000000u) stuck++;          /* the output FIFO still empty: say so */
    write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
    write32(BCR1, (blocks << 16) | 32u);
}
static void finish() {
    for (int i = 0; i < 200000 && (read32(CHCR1) & 0x01000000u); i++) { }
    write32(CHCR1, read32(CHCR1) & ~0x01000000u);
    write32(MDEC1, 0x80000000u);
}

static uint16_t lo[3][NMAX], hi[3][NMAX];

static void runs(int v, uint32_t blocks, uint32_t val) {
    for (int n = 0; n < NMAX; n++) { lo[v][n] = 0xFFFF; hi[v][n] = 0; }
    for (int rep = 0; rep < 4; rep++)
        for (int n = 0; n < NMAX; n++) {
            prepare(blocks);
            const uint32_t t = sled(val, n);
            finish();
            const int e = (int)t - n;
            if (e < lo[v][n]) lo[v][n] = (uint16_t)e;
            if (e > hi[v][n]) hi[v][n] = (uint16_t)e;
        }
}

/* t - N per N, run-length: a run of N with the same fastest..slowest printed once. */
static void show(const char* name, int v) {
    printf("%s: t - N\n ", name);
    int col = 0, a = 0;
    for (int n = 1; n <= NMAX; n++) {
        if (n < NMAX && lo[v][n] == lo[v][a] && hi[v][n] == hi[v][a]) continue;
        if (n - 1 > a) printf(" N %d-%d:", a, n - 1); else printf(" N %d:", a);
        if (lo[v][a] == hi[v][a]) printf("%d", lo[v][a]); else printf("%d..%d", lo[v][a], hi[v][a]);
        if (++col % 6 == 0) printf("\n ");
        a = n;
    }
    printf("\n");
}

int main() {
    printf("\ndma/dma1-start\n");
    printf("DMA1 (MDEC.OUT, SyncMode 1, CHCR 01000200h) with the output ready: CHCR1 stored, N nops, one raw Timer 2 read.\n");
    printf("Cycles at the system clock, t - N fastest..slowest of 4 tries a N. No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000080u);          /* DMA1 on */
    runs(0, 1, 0);
    runs(1, 1, 0x01000200u);
    runs(2, 4, 0x01000200u);
    Res r0[2], r1[2];
    for (int k = 0; k < 2; k++) {
        for (int w = 0; w < 2; w++) {                    /* twice, the second kept (warm) */
            prepare(1);
            if (k == 0) startPoll<0>(CHCR1, 0x01000200u, &r0[0]); else startPoll<16>(CHCR1, 0x01000200u, &r0[1]);
            finish();
            prepare(4);
            if (k == 0) startPoll<0>(CHCR1, 0x01000200u, &r1[0]); else startPoll<16>(CHCR1, 0x01000200u, &r1[1]);
            finish();
        }
    }
    Res c[2];
    for (int k = 0; k < 2; k++) for (int w = 0; w < 2; w++) {
        prepare(1);
        if (k == 0) startPoll<0>(CHCR1, 0, &c[0]); else startPoll<16>(CHCR1, 0, &c[1]);
        finish();
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    show("CONTROL (CHCR1 = 0)", 0);
    show("1 x 32 words", 1);
    show("4 x 32 words", 2);
    printf("\nfirst read of CHCR1 after the store (K ALU ops), then polled with K a turn:\n");
    const char* names[2] = { "K = 0 ", "K = 16" };
    for (int k = 0; k < 2; k++) {
        printf("  CONTROL %s  first read %s at %4u   total %5u   turns %u\n", names[k], (c[k].first >> 24) & 1 ? "BUSY" : "idle", (unsigned)c[k].firstAt, (unsigned)c[k].total, (unsigned)c[k].iters);
        printf("  1 x 32  %s  first read %s at %4u   total %5u   turns %u\n", names[k], (r0[k].first >> 24) & 1 ? "BUSY" : "idle", (unsigned)r0[k].firstAt, (unsigned)r0[k].total, (unsigned)r0[k].iters);
        printf("  4 x 32  %s  first read %s at %4u   total %5u   turns %u\n", names[k], (r1[k].first >> 24) & 1 ? "BUSY" : "idle", (unsigned)r1[k].firstAt, (unsigned)r1[k].total, (unsigned)r1[k].iters);
    }
    printf("\noutput ready on every try: %s\n", stuck ? "NO" : "yes");
    if (stuck) printf("  (%d tries found the output FIFO empty after the wait)\n", stuck);
    printf("first words out: %08x %08x\n", (unsigned)outBuf[0], (unsigned)outBuf[1]);
    printf("\ndma/dma1-start: Done.\n");
    for (;;) VSync(0);
}
