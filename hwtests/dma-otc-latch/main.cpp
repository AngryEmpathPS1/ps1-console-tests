/*
 * dma-otc-latch: WHEN DOES A DMA6 (OTC) START TAKE ITS MADR AND ITS BCR, AND WHAT BECOMES OF A SECOND START?
 *
 * WHY. The console fuzzer's finding 2902-7 and its probe (experiments/fuzz-all/otc2902.js, STATUS "A second DMA6 (OTC)
 * start: the console runs it, this core drops it"), the second SCPH-5501 and the SCPH-7502 alike: two OTC starts k nops
 * apart - at k 0-2 only the SECOND list is written (the first start ran with the rewritten MADR and BCR), from k 3 both.
 * This core writes the table at the store to CHCR with the registers as they stand, holds the channel to the transfer's
 * end and drops a start written meanwhile. The probe's second start stores MADR, then BCR two instructions later, then
 * CHCR: its rows cannot say when each register is taken (MADR 5 instructions behind the start was too late, BCR 6 behind
 * was not - two latches, or one and something else).
 *
 * WHAT. A start (MADR A, BCR 36h, CHCR 11000002h), then K nops, then from registers loaded beforehand:
 *   M  one store: MADR = B                      - which table is written says whether the start had taken MADR
 *   C  one store: BCR = 21h                     - the list's length says whether it had taken BCR
 *   S  three stores back to back: MADR = B, BCR = 21h, CHCR = 11000002h   - the second start
 *   X  one store: CHCR = 11000002h again, MADR / BCR untouched             - a second start of the same list
 * K = 0 .. 12. The code is in the I-cache (each row runs first on RAM words). The table is 256 words of a sentinel before
 * each row. Per row: the words written downward from A's start and from B's start (contiguous, OTC's own values), all the
 * words changed, whether B's list was written after A's (A's top word still a link, not overwritten), MADR, BCR and CHCR
 * after. Three passes. No assertions.
 *
 * READ IT. A alone writes 54 from A; B's registers taken writes from B. M: the last K with "from B 54" is where MADR is
 * still taken. C: the last K with "from A 33" is where BCR is. S: both lists (54 from A, 33 from B) or one. X: 54 either
 * way - its CHCR after and the time say whether the second start ran.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js dma-otc-latch --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR 0x1F8010F0u
#define DMA6 0x1F8010E0u
#define SENT 0x5A000000u
#define IA 0xB5
#define IB 0xF8
#define NA 0x36
#define NB 0x21

static uint32_t tbl[256] __attribute__((aligned(16)));
static volatile uint32_t dummy[4];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }

#define N1 "nop\n"
#define N2 N1 N1
#define N4 N2 N2
#define N8 N4 N4
#define HEAD ".set push\n.set noreorder\nsw %0, 8(%1)\n"
#define FOOT N4 ".set pop\n"
#define ARGS : : "r"(chcr), "r"(b), "r"(m), "r"(n) : "memory"
#define VM(NOPS) __asm__ volatile(HEAD NOPS "sw %2, 0(%1)\n" FOOT ARGS)
#define VC(NOPS) __asm__ volatile(HEAD NOPS "sw %3, 4(%1)\n" FOOT ARGS)
#define VS(NOPS) __asm__ volatile(HEAD NOPS "sw %2, 0(%1)\nsw %3, 4(%1)\nsw %0, 8(%1)\n" FOOT ARGS)
#define VX(NOPS) __asm__ volatile(HEAD NOPS "sw %0, 8(%1)\n" FOOT ARGS)
#define CASES(V) switch (k) { \
    case 0: V(""); break; case 1: V(N1); break; case 2: V(N2); break; case 3: V(N2 N1); break; \
    case 4: V(N4); break; case 5: V(N4 N1); break; case 6: V(N4 N2); break; case 7: V(N4 N2 N1); break; \
    case 8: V(N8); break; case 9: V(N8 N1); break; case 10: V(N8 N2); break; case 11: V(N8 N2 N1); break; \
    default: V(N8 N4); break; }
static void __attribute__((noinline)) fireM(volatile uint32_t* b, uint32_t chcr, uint32_t m, uint32_t n, int k) { CASES(VM) }
static void __attribute__((noinline)) fireC(volatile uint32_t* b, uint32_t chcr, uint32_t m, uint32_t n, int k) { CASES(VC) }
static void __attribute__((noinline)) fireS(volatile uint32_t* b, uint32_t chcr, uint32_t m, uint32_t n, int k) { CASES(VS) }
static void __attribute__((noinline)) fireX(volatile uint32_t* b, uint32_t chcr, uint32_t m, uint32_t n, int k) { CASES(VX) }
static void fire(int mode, volatile uint32_t* b, uint32_t chcr, uint32_t m, uint32_t n, int k) {
    if (mode == 0) fireM(b, chcr, m, n, k); else if (mode == 1) fireC(b, chcr, m, n, k); else if (mode == 2) fireS(b, chcr, m, n, k); else fireX(b, chcr, m, n, k);
}

static int runDown(int from) {   /* words written downward from tbl[from]: a link to the word below, or the terminator */
    int c = 0;
    for (int i = from; i >= 0; i--) {
        const uint32_t v = tbl[i];
        if (v == 0x00FFFFFFu) { c++; break; }
        if (v != (((uint32_t)&tbl[i] - 4u) & 0x00FFFFFFu)) break;
        c++;
    }
    return c;
}
static void one(int p, int mode, int k) {
    const uint32_t chcr = 0x11000002u, ma = (uint32_t)&tbl[IA] & 0x00FFFFFCu, mb = (uint32_t)&tbl[IB] & 0x00FFFFFCu;
    for (int i = 0; i < 256; i++) tbl[i] = SENT | (uint32_t)i;
    fire(mode, &dummy[0], chcr, mb, NB, k);                      /* the same instructions on RAM words: their cache lines are in */
    write32(DMA6 + 0, ma);
    write32(DMA6 + 4, NA);
    waitT2(2000);
    const uint16_t t0 = read16(T2_COUNT);
    fire(mode, (volatile uint32_t*)DMA6, chcr, mb, NB, k);
    for (int i = 0; i < 200000 && (read32(DMA6 + 8) & 0x01000000u); i++) {}   /* bounded: a transfer that never ends is a row, not a hang */
    const uint16_t t1 = read16(T2_COUNT);
    const uint32_t c = read32(DMA6 + 8), madr = read32(DMA6 + 0), bcr = read32(DMA6 + 4);
    if (c & 0x01000000u) write32(DMA6 + 8, 0);
    waitT2(3000);
    int all = 0;
    for (int i = 0; i < 256; i++) if (tbl[i] != (SENT | (uint32_t)i)) all++;
    static const char MN[4] = { 'M', 'C', 'S', 'X' };
    printf("R %d %c %2d  from A %2d  from B %2d  all %3d  madr %06x bcr %x chcr %08x  t %u\n", p, MN[mode], k, runDown(IA), runDown(IB), all,
           (unsigned)((madr - ((uint32_t)tbl & 0x00FFFFFFu)) & 0x00FFFFFFu), (unsigned)bcr, (unsigned)c, (unsigned)(uint16_t)(t1 - t0));
}

int main() {
    printf("\ndma/dma-otc-latch\n");
    printf("R pass mode K: a DMA6 start (MADR A = table+2D4h, BCR 36h), K nops, then M a store of MADR = B (table+3E0h), C a store of\n");
    printf("BCR = 21h, S the second start (MADR B, BCR 21h, CHCR back to back), X CHCR alone again. Words written downward from A's start\n");
    printf("and from B's, all words changed; MADR (less the table's address), BCR and CHCR after; cycles to CHCR's busy bit clear. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t dp = read32(DPCR);
    write32(DPCR, dp | 0x08000000u);
    for (int p = 0; p < 3; p++) for (int mode = 0; mode < 4; mode++) for (int k = 0; k <= 12; k++) one(p, mode, k);
    write32(DPCR, dp);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ndma/dma-otc-latch: Done.\n");
    for (;;) VSync(0);
}
