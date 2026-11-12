/*
 * dma/dma1-drq2 - why dma1-start's DMA1 hung on the consoles when dma1-drq's took the same output, and then the start's cost.
 *
 * WHY (2026-11-12). On the second SCPH-5501 and the PSone, dma1-drq case A (reset, MDEC1 20000000h, six words by the CPU, a
 * 30,000-cycle wait, DMA1 1 x 32 from C) moved its 32 words before the first poll, every try, both consoles. dma1-start, meant
 * to be the same setup, hung on every try: Busy for 32,767 polls, the store costing nothing. Yet its outBuf held the console's
 * own MDEC output (4a314a31 on the 5501, 7db27db2 on the PSone - dma1-drq's first words), so some words did move. Two things
 * differ: dma1-start turns on only DMA1 in DPCR (80h; dma1-drq 88h, DMA0 too), and it stores CHCR1 from an asm sled - pass 0
 * stores 0, pass 1 the start, 330 cycles apart - where dma1-drq stores it once from C. (The words differ console to console
 * because neither test loads the MDEC's quant and scale tables: each decodes with the tables it was left. Not a timing matter.)
 *
 * WHAT IT DOES. As dma1-drq (interrupts off, Timer 2 at the system clock, outBuf filled with A5A5A5A5h, one 15-bit DC-only
 * macroblock of six words by the CPU, 30,000 cycles, DMA1 1 x 32), four ways, three tries each:
 *   A   DPCR 88h, CHCR1 stored from C                      (dma1-drq A)
 *   G   DPCR 80h, CHCR1 stored from C                      (DMA0 off)
 *   H   DPCR 88h, CHCR1 stored by dma1-start's sled, N = 0  (the two-store asm)
 *   I   DPCR 80h, by the sled                              (dma1-start exactly)
 * Per try: the cycles until Busy cleared (or STUCK after 200,000: the start bit then cleared), MADR1's end, the words written
 * and the first. Then THE TIMING, with whichever of H and I moved (H if both): dma1-start's measurement - CHCR1 stored, an
 * N-nop sled, one raw Timer 2 read; t - N fastest..slowest of four tries - at N = 0, 8, 16, 32, 64, 128, 192, 256, 319, for
 * 1 x 32 and for 4 x 32, a CONTROL (CHCR1 = 0) beside them; and the words each moved.
 *
 * READ IT LIKE THIS. G and I stuck where A and H move: DMA1 needs DMA0's enable in DPCR (no reference says so). H and I stuck
 * where A and G move: the store from the sled. A partial count (wrote < 32) is a transfer that began and waited. The timing rows
 * read as dma1-start's: t - N constant from N = 0 is a CPU stalled through the block.
 *
 * THE RULE: run it here first: `node hwtests/run.js dma1-drq2`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define CHCR0   0x1F801088u
#define MADR1   0x1F801090u
#define BCR1    0x1F801094u
#define CHCR1   0x1F801098u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define SENT    0xA5A5A5A5u

uint32_t outBuf[128] __attribute__((aligned(16)));

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { uint32_t s = 0; uint16_t a = t2(); while (s < cycles) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } }

/* dma1-start's sled, unchanged: two passes in one block, pass 0 stores 0, pass 1 `val`; then N nops and one Timer 2 read. */
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

struct Try { uint32_t took, madr, moved, first; bool stuck; };

static void prepare(uint32_t dpcr, uint32_t blocks) {
    for (int i = 0; i < 128; i++) outBuf[i] = SENT;
    write32(DPCR, dpcr);
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    write32(MDEC1, 0x20000000u);
    write32(MDEC0, (1u << 29) | (3u << 27) | 6u);
    for (int b = 0; b < 6; b++) write32(MDEC0, 0xFE000400u | (uint32_t)(b * 16));   /* q 1 + a DC, then the end code */
    wait(30000);
    write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
    write32(BCR1, (blocks << 16) | 32u);
}
static void settle(Try* r) {
    uint32_t s = 0; uint16_t a = t2();
    while ((read32(CHCR1) & 0x01000000u) && s < 200000) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; }
    r->stuck = (read32(CHCR1) & 0x01000000u) != 0;
    r->took = s;
    if (r->stuck) write32(CHCR1, read32(CHCR1) & ~0x01000000u);
    r->madr = read32(MADR1) - ((uint32_t)outBuf & 0x1FFFFFu);
    r->moved = 0;
    for (int i = 0; i < 128; i++) if (outBuf[i] != SENT) r->moved++;
    r->first = outBuf[0];
    write32(MDEC1, 0x80000000u);
}

static const char CASES[] = "AGHI";
static Try res[4][3];
static const uint32_t NS[] = { 0, 8, 16, 32, 64, 128, 192, 256, 319 };
#define NN 9
static uint16_t lo[3][NN], hi[3][NN], mv[3][NN];

int main() {
    printf("\ndma/dma1-drq2\n");
    printf("dma1-drq's case A against dma1-start's DPCR (80h) and its sled store, then the start timed where it moves. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR);
    write16(T2_MODE, 0);
    const uint32_t d88 = dp0 | 0x88u, d80 = (dp0 | 0x80u) & ~0x08u;
    for (int k = 0; k < 4; k++)
        for (int t = 0; t < 3; t++) {
            const char c = CASES[k];
            prepare((c == 'A' || c == 'H') ? d88 : d80, 1);
            if (c == 'A' || c == 'G') write32(CHCR1, 0x01000200u); else (void)sled(0x01000200u, 0);
            settle(&res[k][t]);
        }
    const bool hMoves = !res[2][0].stuck && !res[2][1].stuck && !res[2][2].stuck;
    const bool iMoves = !res[3][0].stuck && !res[3][1].stuck && !res[3][2].stuck;
    const uint32_t dT = hMoves ? d88 : d80;
    const bool timed = hMoves || iMoves;
    if (timed)
        for (int v = 0; v < 3; v++)
            for (int j = 0; j < NN; j++) {
                lo[v][j] = 0xFFFF; hi[v][j] = 0; mv[v][j] = 0;
                for (int rep = 0; rep < 4; rep++) {
                    prepare(dT, v == 2 ? 4 : 1);
                    const uint32_t t = sled(v == 0 ? 0 : 0x01000200u, NS[j]);
                    Try r; settle(&r);
                    const int e = (int)t - (int)NS[j];
                    if (e < lo[v][j]) lo[v][j] = (uint16_t)e;
                    if (e > hi[v][j]) hi[v][j] = (uint16_t)e;
                    if (r.moved > mv[v][j]) mv[v][j] = (uint16_t)r.moved;
                }
            }
    write32(DPCR, dp0);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("DPCR at entry %08x\n", (unsigned)dp0);
    for (int k = 0; k < 4; k++)
        for (int t = 0; t < 3; t++) {
            const Try& r = res[k][t];
            printf("  %c%d  ", CASES[k], t + 1);
            if (r.stuck) printf("Busy STUCK      "); else printf("Busy cleared %6u", (unsigned)r.took);
            printf("  MADR1 +%4u  wrote %3u words, first %08x\n", (unsigned)r.madr, (unsigned)r.moved, (unsigned)r.first);
        }
    if (!timed) printf("\nneither H nor I moved on every try: no timing rows\n");
    else {
        printf("\nthe start timed (case %c's DPCR, the sled): t - N fastest..slowest of 4, and the most words moved\n", hMoves ? 'H' : 'I');
        const char* names[3] = { "CONTROL", "1 x 32 ", "4 x 32 " };
        for (int v = 0; v < 3; v++) {
            printf("  %s", names[v]);
            for (int j = 0; j < NN; j++) {
                if (lo[v][j] == hi[v][j]) printf("  N%u:%u", (unsigned)NS[j], (unsigned)lo[v][j]);
                else printf("  N%u:%u..%u", (unsigned)NS[j], (unsigned)lo[v][j], (unsigned)hi[v][j]);
                if (v) printf("/%u", (unsigned)mv[v][j]);
            }
            printf("\n");
        }
    }
    printf("\ndma/dma1-drq2: Done.\n");
    for (;;) VSync(0);
}
