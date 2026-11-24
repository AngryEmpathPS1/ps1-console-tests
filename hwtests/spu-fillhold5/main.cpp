/*
 * spu/spu-fillhold5 - the type-0 hold with voices sounding: every halfword of sound RAM it changes (2026-11-24, the SPU session)
 *
 * WHY. hwtests/spu-fillhold4 on the SCPH-1000 and SCPH-1001 (alike): with voices 1 and 3 sounding from 8000h, the first hold of
 * 1F801DACh = 0000h put voice 3's capture values (4FADh ...) at the capture counter's 11-bit address in 2000h-3FFFh - so the
 * write is the capture's, misplaced - and those values fell away inside the hold (4429h, 0DD9h: the interpolator's tail) and
 * both voices read 0000h for good afterwards: their sample at 8000h, or their way through it, was changed by the hold. hwtests/
 * spu-fillhold3 saw 1000h-3FFFh only with every voice idle wherever the last program left it. This reads all of sound RAM with
 * the voices at known places.
 *
 * WHAT IT DOES. Interrupts off, the main volume 0. All 512 KB written with a pattern (halfword i = 4000h | (i & 3FFFh)) holding
 * two looping samples: a steady level at 20000h (voices 1 and 3, pitch 1000h, the envelope at its top) and silence at 30000h
 * (the other 22 voices, keyed on and off again so they loop there). SPUSTAT bit 11's falling edge, 100 samples, 1F801DACh =
 * 0000h for the case's samples, 0004h; all of it read back.
 *   X n value : total t : r:count@first=value ...   n samples held at the value (0000h, then 0002h and 000Ch); t halfwords
 *                                               changed from 1000h up; each 1000h bytes r holding any: how many, the first
 *                                               one's halfword index in it and what it reads (first 24)
 *   S n : 20000h w w w w w w w w / 20010h w ...   the sounding sample's 16 halfwords as read back
 *   R n : rings z0 z1 z2 z3 : ENVX e1 e3 ENDX x  how many of each capture ring's 512 read 0000h; voice 1's and voice 3's
 *                                               envelope and ENDX's low half 16 samples after the hold
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
#define SND 0x20000u
#define SIL 0x30000u

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
/* what was written: the pattern, the sounding sample (shift 0, filter 0, every nibble 5; the second block loops to the first)
 * and the silent one */
static uint16_t want(int i) {
    const uint32_t b = (uint32_t)i * 2;
    if (b >= SND && b < SND + 32) { const uint32_t o = b - SND; return (o & 15) == 0 ? (o < 16 ? 0x0400 : 0x0300) : 0x5555; }
    if (b >= SIL && b < SIL + 32) { const uint32_t o = b - SIL; return (o & 15) == 0 ? (o < 16 ? 0x0400 : 0x0300) : 0x0000; }
    return (uint16_t)(0x4000 | (i & 0x3FFF));
}
static uint32_t edge() {
    for (int i = 0; i < 200000 && !(read16(SPU(0x1AE)) & 0x0800); i++) { now(); }
    for (int i = 0; i < 200000; i++) { const uint32_t t = now(); if (!(read16(SPU(0x1AE)) & 0x0800)) return t; }
    return 0;
}

static void allCase(uint32_t n, uint16_t value) {
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(SAMPLE * 64u);
    for (int i = 0; i < ALL; i++) { big[i] = want(i); if ((i & 1023) == 1023) now(); }
    dma(0, big, ALL / 32, true);
    for (int v = 0; v < 24; v++) {
        const uint32_t o = (uint32_t)v * 16;
        write16(SPU(o + 0), 0); write16(SPU(o + 2), 0);
        write16(SPU(o + 4), 0x1000);
        write16(SPU(o + 6), (uint16_t)((v == 1 || v == 3 ? SND : SIL) / 8));
        write16(SPU(o + 8), 0x000F); write16(SPU(o + 10), 0x0000);
    }
    write16(SPU(0x188), 0xFFFF); write16(SPU(0x18A), 0x00FF);
    idle(SAMPLE * 64u);
    write16(SPU(0x18C), 0xFFF5); write16(SPU(0x18E), 0x00FF);        /* all but voices 1 and 3 off: they go on looping */
    idle(SAMPLE * 600u);
    spuMode(0); write16(SPU(0x1A6), 0x2000 / 8);
    const uint32_t e = edge();
    while (now() - e < 100u * SAMPLE) {}
    write16(SPU(0x1AC), value);
    const uint32_t t0 = now();
    while (now() - t0 < n * SAMPLE) {}
    write16(SPU(0x1AC), 0x0004);
    idle(SAMPLE * 16u);
    const uint16_t ex1 = read16(SPU(0x1C)), ex3 = read16(SPU(0x3C)), endx = read16(SPU(0x19C));
    dma(0, big, ALL / 32, false);
    int total = 0;
    static uint16_t count[128], val[128]; static int16_t first[128];
    for (int r = 0; r < 128; r++) { count[r] = 0; first[r] = -1; val[r] = 0; }
    for (int i = 0x800; i < ALL; i++) {
        if ((i & 1023) == 1023) now();
        if (big[i] == want(i)) continue;
        const int r = i >> 11;
        total++; count[r]++; if (first[r] < 0) { first[r] = (int16_t)(i & 2047); val[r] = big[i]; }
    }
    now();
    printf("X %u %04x : total %d :", (unsigned)n, value, total);
    int shown = 0;
    for (int r = 0; r < 128 && shown < 24; r++) if (count[r]) { printf(" %x:%d@%d=%04x", r, count[r], first[r], val[r]); shown++; if ((shown & 7) == 7) now(); }
    printf("\n"); now();
    printf("S %u : 20000h", (unsigned)n);
    for (int i = 0; i < 16; i++) { if (i == 8) printf(" / 20010h"); printf(" %04x", big[SND / 2 + i]); }
    printf("\n"); now();
    int z[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 2048; i++) if (big[i] == 0) z[i >> 9]++;
    printf("R %u : rings %d %d %d %d : ENVX %04x %04x ENDX %04x\n", (unsigned)n, z[0], z[1], z[2], z[3], ex1, ex3, endx);
    now();
}

int main() {
    printf("\nspu/spu-fillhold5\n");
    printf("The type-0 hold with voices sounding: X n : total : region:count@first=value; S the sample read back; R the rings' zero counts. No assertions.\n\n");
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

    allCase(0, 0x0000);
    allCase(1, 0x0000);
    allCase(4, 0x0000);
    allCase(64, 0x0000);
    allCase(64, 0x0000);
    allCase(64, 0x0000);
    allCase(64, 0x0002);
    allCase(64, 0x0002);
    allCase(64, 0x0002);
    allCase(64, 0x000C);

    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fillhold5: Done.\n");
    for (;;) VSync(0);
}
