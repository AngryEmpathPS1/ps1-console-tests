/*
 * dma/dma1-drq5 - the MDEC's enable written too close behind its reset is lost: DMA1's "wedge" on the consoles, the gap that
 * keeps it, and with that gap at last DMA1's start timed (DMA_SM1_START, the instrument audit's flag #1).
 *
 * WHY (2026-11-12). On the second SCPH-5501 and the PSone, DMA1 took one MDEC output and then nothing, whatever the try did
 * (dma1-drq2 1/12, dma1-drq3 1/64, dma1-drq4 2/12; all under the runner, DPCR 08009899h, DICR 00840000h), where dma1-drq
 * took all 18. mdec-in-hang (2026-10-08, three consoles) reads the same: the first try done, then every one hung with the
 * data-in request 0 right after MDEC1 60000000h. The tests that hang write MDEC1 80000000h (reset) and 20000000h (the
 * data-out request on) back to back; dma1-drq's case dispatch put a few instructions between them. This core loses a store
 * MDEC_RESET_CYCLES (7) behind the reset (mdec-reset2, STATUS "The MDEC's reset takes 7 cycles"), and its KNOWN-OPEN has
 * the refresh in the write queue moving that window. So the enable is lost, DMA1 gets no request, and its Busy stays.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, DPCR DMA0 and DMA1 on. A try: outBuf filled with A5A5A5A5h,
 * CHCR0 / CHCR1 0, MDEC1 80000000h, a wait of G cycles, MDEC1 20000000h, one 15-bit DC-only macroblock's six words by the
 * CPU, 30,000 cycles, DMA1 (MADR1, BCR1, CHCR1 01000200h), Busy polled up to 200,000 (the start bit cleared if it stays),
 * the MDEC reset (and 200 cycles).
 *   A  16 tries at G 200: they must all move.
 *   B  dma1-start's measurement at G 200: CHCR1 stored from cached code, an N-nop sled, one raw Timer 2 read, t - N at
 *      N = 0, 8, 16, 32, 64, 128, 192, 256, 319 (fastest..slowest of 4), 1 x 32 and 4 x 32 and a CONTROL (CHCR1 = 0).
 *   C  the gap itself: the reset and the data-out enable (20000000h) from asm with K nops between, K = 0..24, right after a
 *      try that left 96 words in the FIFO; then the macroblock and DMA1 as in A; 4 tries a K; how many moved (the enable kept).
 *
 * READ IT LIKE THIS. A all moving: the gap is the wedge. B reads as dma1-start: t - N constant from N = 0 is a CPU stalled
 * through the block, t - N less the CONTROL's the start and the block. C: the smallest K kept every time is the reset's
 * window (this core: K + the stores' spacing >= 7).
 *
 * THE RULE: run it here first: `node hwtests/run.js dma1-drq5`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define DPCR    0x1F8010F0u
#define DICR    0x1F8010F4u
#define CHCR0   0x1F801088u
#define MADR1   0x1F801090u
#define BCR1    0x1F801094u
#define CHCR1   0x1F801098u
#define MDEC0   0x1F801820u
#define MDEC1   0x1F801824u
#define SENT    0xA5A5A5A5u
#define GAP     200

uint32_t outBuf[128] __attribute__((aligned(16)));

static inline uint16_t t2() { return read16(0x1F801120); }
static void wait(uint32_t cycles) { uint32_t s = 0; uint16_t a = t2(); while (s < cycles) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; } }

/* dma1-start's sled, unchanged: pass 0 stores 0, pass 1 `val`; then N nops and one Timer 2 read. */
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

/* MDEC1 reset, K nops, MDEC1 20000000h - from asm, the two stores K instructions apart. */
#define GAPFN(K) static void __attribute__((noinline)) gap##K() { __asm__ volatile(".set push\n.set noreorder\n lui $t9, 0x1f80\n lui $t8, 0x8000\n lui $t7, 0x2000\n sw $t8, 0x1824($t9)\n .rept " #K "\n nop\n .endr\n sw $t7, 0x1824($t9)\n .set pop\n" ::: "t7", "t8", "t9", "memory"); }
GAPFN(0) GAPFN(1) GAPFN(2) GAPFN(3) GAPFN(4) GAPFN(5) GAPFN(6) GAPFN(7) GAPFN(8) GAPFN(9) GAPFN(10) GAPFN(11) GAPFN(12)
GAPFN(13) GAPFN(14) GAPFN(15) GAPFN(16) GAPFN(17) GAPFN(18) GAPFN(19) GAPFN(20) GAPFN(21) GAPFN(22) GAPFN(23) GAPFN(24)
typedef void (*gapf)();
static const gapf GAPS[25] = { gap0, gap1, gap2, gap3, gap4, gap5, gap6, gap7, gap8, gap9, gap10, gap11, gap12, gap13, gap14, gap15,
                               gap16, gap17, gap18, gap19, gap20, gap21, gap22, gap23, gap24 };

static void prepare(uint32_t blocks) {
    for (int i = 0; i < 128; i++) outBuf[i] = SENT;
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    wait(GAP);
    write32(MDEC1, 0x20000000u);
    write32(MDEC0, (1u << 29) | (3u << 27) | 6u);
    for (int b = 0; b < 6; b++) write32(MDEC0, 0xFE000400u | (uint32_t)(b * 16));
    wait(30000);
    write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
    write32(BCR1, (blocks << 16) | 32u);
}
static bool settle(uint32_t* moved) {
    uint32_t s = 0; uint16_t a = t2();
    while ((read32(CHCR1) & 0x01000000u) && s < 200000) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; }
    const bool stuck = (read32(CHCR1) & 0x01000000u) != 0;
    if (stuck) write32(CHCR1, read32(CHCR1) & ~0x01000000u);
    uint32_t m = 0;
    for (int i = 0; i < 128; i++) if (outBuf[i] != SENT) m++;
    if (moved) *moved = m;
    write32(MDEC1, 0x80000000u);
    wait(GAP);
    return !stuck;
}

static const uint32_t NS[] = { 0, 8, 16, 32, 64, 128, 192, 256, 319 };
#define NN 9
static uint16_t lo[3][NN], hi[3][NN], mv[3][NN];
static uint8_t aMoved[16];
static uint8_t kept[25];

int main() {
    printf("\ndma/dma1-drq5\n");
    printf("The MDEC's enable G cycles behind its reset, DMA1 1 x 32 from its output; at G %d the start timed; the gap scanned. No assertions.\n\n", GAP);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    const uint32_t dp0 = read32(DPCR), di0 = read32(DICR);
    write16(T2_MODE, 0);
    write32(DPCR, dp0 | 0x88u);
    // A
    for (int t = 0; t < 16; t++) { prepare(1); write32(CHCR1, 0x01000200u); uint32_t m; settle(&m); aMoved[t] = (uint8_t)m; }
    // B
    for (int v = 0; v < 3; v++)
        for (int j = 0; j < NN; j++) {
            lo[v][j] = 0xFFFF; hi[v][j] = 0; mv[v][j] = 0;
            for (int rep = 0; rep < 4; rep++) {
                prepare(v == 2 ? 4 : 1);
                const uint32_t t = sled(v == 0 ? 0 : 0x01000200u, NS[j]);
                uint32_t m; settle(&m);
                const int e = (int)t - (int)NS[j];
                if (e < lo[v][j]) lo[v][j] = (uint16_t)e;
                if (e > hi[v][j]) hi[v][j] = (uint16_t)e;
                if (m > mv[v][j]) mv[v][j] = (uint16_t)m;
            }
        }
    // C: each try right behind a transfer that left 96 words (A's recipe, the MDEC not reset after it)
    for (int k = 0; k < 25; k++)
        for (int rep = 0; rep < 4; rep++) {
            prepare(1); write32(CHCR1, 0x01000200u);
            uint32_t s = 0; uint16_t a = t2();
            while ((read32(CHCR1) & 0x01000000u) && s < 200000) { const uint16_t b = t2(); s += (uint16_t)(b - a); a = b; }
            if (read32(CHCR1) & 0x01000000u) write32(CHCR1, read32(CHCR1) & ~0x01000000u);
            GAPS[k]();
            for (int i = 0; i < 128; i++) outBuf[i] = SENT;
            write32(MDEC0, (1u << 29) | (3u << 27) | 6u);
            for (int b = 0; b < 6; b++) write32(MDEC0, 0xFE000400u | (uint32_t)(b * 16));
            wait(30000);
            write32(MADR1, (uint32_t)outBuf & 0x1FFFFFu);
            write32(BCR1, (1u << 16) | 32u);
            write32(CHCR1, 0x01000200u);
            if (settle(0)) kept[k]++;
        }
    write32(DPCR, dp0);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("DPCR at entry %08x, DICR %08x\n\n", (unsigned)dp0, (unsigned)di0);
    int na = 0;
    printf("A  16 tries at G %d, words written:", GAP);
    for (int t = 0; t < 16; t++) { printf(" %u", (unsigned)aMoved[t]); if (aMoved[t] == 32) na++; }
    printf("   (%d of 16 moved 32)\n\n", na);
    printf("B  the start timed at G %d: t - N fastest..slowest of 4, and the most words moved\n", GAP);
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
    printf("\nC  MDEC1 reset, K nops, MDEC1 20000000h behind a transfer that left 96 words, then DMA1 1 x 32: moved, of 4\n ");
    for (int k = 0; k < 25; k++) printf(" K%d:%u", k, (unsigned)kept[k]);
    printf("\n\ndma/dma1-drq5: Done.\n");
    for (;;) VSync(0);
}
