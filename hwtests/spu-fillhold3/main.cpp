/*
 * spu/spu-fillhold3 - the type-0 hold's writes: every place in sound RAM, and how the place moves (2026-11-24, the SPU session)
 *
 * WHY. hwtests/spu-fillhold2 on the SCPH-1000 and SCPH-1001 (the second 5501 writes nothing): 1F801DACh with bits 3-1 clear,
 * held 64 samples, leaves 64 halfwords of 0000h, 4 halfwords apart, in 2000h-2FFFh AND the same 64 in 3000h-3FFFh, none in
 * 4000h-5FFFh; the transfer address, the FIFO's contents, SPUCNT's mode and its enable bit change nothing; the first one sits
 * at halfword K + 1 + 512 b past 2000h, K the capture ring's index at the hold's start - one halfword a sample of K where the
 * hold itself goes four a sample - and b (0-3) kept from case to case but for the two holds that crossed the ring's wrap, after
 * which it was 2 and then 1 more. This reads the whole of sound RAM for the set of places, and short holds for the walk.
 *
 * WHAT IT DOES. Interrupts off, sound muted, nothing keyed on, reverb off.
 *  A rows: all 512 KB written with a pattern (halfword i = 4000h | (i & 3FFFh)); SPUSTAT bit 11's falling edge, 100 samples,
 *    1F801DACh = 0000h for 64 samples; all of it read back.
 *      A addr : total n : r:count@first ...     the transfer address written first; n halfwords changed from 1000h up (the
 *                                               capture rings are left out); each 1000h bytes r holding any, how many, and
 *                                               the first one's halfword index in it (the first 40)
 *  B rows: 2000h-3FFFh written with the pattern; the edge, K samples, 0000h for N samples; (two = the same again 200 samples
 *    after the first hold's end, nothing written between); read back.
 *      B K N what : at k+p, changed n : a+c/s ...   the changed halfword indexes of 2000h-2FFFh as runs: c of them from a, s
 *                                               apart; n counts 2000h-3FFFh. p = cycles past the sample (Timer 2)
 * No assertions. Every wait is bounded. THE RULE: run here first.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define SAMPLE 768u
#define ALL 262144

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static void dma(uint32_t spuAddr, void* ram, uint32_t blocks, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, (blocks << 16) | 16u);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 8000000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); printf("T\n"); }
    if (toSpu) idle(100000);
    spuMode(0);
}

static uint16_t big[ALL] __attribute__((aligned(16)));
static inline uint16_t pat(int i) { return (uint16_t)(0x4000 | (i & 0x3FFF)); }
static void fill(int n) { for (int i = 0; i < n; i++) { big[i] = pat(i); if ((i & 1023) == 1023) now(); } }

static uint32_t edge() {
    for (int i = 0; i < 200000 && !(read16(SPU(0x1AE)) & 0x0800); i++) { now(); }
    for (int i = 0; i < 200000; i++) { const uint32_t t = now(); if (!(read16(SPU(0x1AE)) & 0x0800)) return t; }
    return 0;
}
/* 0000h for n samples from k samples past `e`; returns the store's stamp less e */
static uint32_t hold(uint32_t e, uint32_t k, uint32_t n) {
    while (now() - e < k * SAMPLE) {}
    const uint16_t t = read16(T2_COUNT);
    write16(SPU(0x1AC), 0x0000);
    const uint32_t at = stamp(t);
    while (now() - at < n * SAMPLE) {}
    write16(SPU(0x1AC), 0x0004);
    return at - e;
}

static void allCase(uint32_t addr) {
    fill(ALL);
    dma(0, big, ALL / 32, true);
    spuMode(0); write16(SPU(0x1A6), (uint16_t)(addr / 8));
    idle(SAMPLE * 8u);
    hold(edge(), 100, 64);
    idle(SAMPLE * 64u);
    dma(0, big, ALL / 32, false);
    int total = 0;
    static uint16_t count[128]; static int16_t first[128];
    for (int r = 0; r < 128; r++) { count[r] = 0; first[r] = -1; }
    for (int i = 0x800; i < ALL; i++) {
        if ((i & 1023) == 1023) now();
        if (big[i] == pat(i)) continue;
        const int r = i >> 11;
        total++; count[r]++; if (first[r] < 0) first[r] = (int16_t)(i & 2047);
    }
    now();
    printf("A %05x : total %d :", (unsigned)addr, total);
    int shown = 0;
    for (int r = 0; r < 128 && shown < 40; r++) if (count[r]) { printf(" %x:%d@%d", r, count[r], first[r]); shown++; if ((shown & 7) == 7) now(); }
    printf("\n");
    now();
}

static void walkCase(uint32_t k, uint32_t n, bool two) {
    fill(4096);
    dma(0x2000, big, 4096 / 32, true);
    spuMode(0); write16(SPU(0x1A6), 0x2000 / 8);
    idle(SAMPLE * 8u);
    const uint32_t e = edge();
    const uint32_t at = hold(e, k, n);
    if (two) hold(e, k + n + 200, n);
    idle(SAMPLE * 64u);
    dma(0x2000, big, 4096 / 32, false);
    int total = 0;
    for (int i = 0; i < 4096; i++) if (big[i] != pat(i)) total++;
    now();
    printf("B %u %u %s : at %u+%u, changed %d :", (unsigned)k, (unsigned)n, two ? "two" : "one", (unsigned)(at / SAMPLE), (unsigned)(at % SAMPLE), total);
    int runs = 0;
    for (int i = 0; i < 2048 && runs < 12; ) {
        if (big[i] == pat(i)) { i++; continue; }
        int j = i + 1; while (j < 2048 && big[j] == pat(j)) j++;
        const int s = j < 2048 ? j - i : 0;
        int c = 1, last = i;
        if (s) for (int q = i + s; q < 2048 && big[q] != pat(q); q += s) {
            bool clean = true; for (int z = last + 1; z < q; z++) if (big[z] != pat(z)) { clean = false; break; }
            if (!clean) break;
            c++; last = q;
        }
        printf(" %d+%d/%d", i, c, c > 1 ? s : 0);
        runs++; i = last + 1;
        if ((runs & 3) == 3) now();
    }
    printf("\n");
    now();
}

int main() {
    printf("\nspu/spu-fillhold3\n");
    printf("The type-0 hold's writes: every place (A addr : total : region:count@first) and the walk (B K N what : at, changed : a+c/s). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);

    allCase(0x2000);
    allCase(0x8000);
    allCase(0x40000);

    static const uint16_t W[][3] = {
        { 100, 1, 0 }, { 100, 1, 0 }, { 100, 2, 0 }, { 100, 3, 0 }, { 100, 4, 0 }, { 100, 8, 0 }, { 101, 4, 0 }, { 102, 4, 0 }, { 104, 4, 0 },
        { 100, 4, 1 }, { 100, 16, 1 },
        { 500, 8, 0 }, { 100, 2, 0 },        /* short of the ring's wrap, then where a hold at 100 lands */
        { 506, 8, 0 }, { 100, 2, 0 },        /* across it by 2 */
        { 500, 64, 0 }, { 100, 2, 0 },       /* across it by 52 */
        { 448, 63, 0 }, { 100, 2, 0 },       /* up to it */
        { 448, 66, 0 }, { 100, 2, 0 },       /* just across */
        { 20, 600, 0 }, { 100, 2, 0 },       /* a whole turn */
    };
    for (unsigned c = 0; c < sizeof(W) / sizeof(W[0]); c++) walkCase(W[c][0], W[c][1], W[c][2] != 0);

    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fillhold3: Done.\n");
    for (;;) VSync(0);
}
