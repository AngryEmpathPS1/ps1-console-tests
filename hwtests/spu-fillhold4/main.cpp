/*
 * spu/spu-fillhold4 - is the type-0 hold's write the CAPTURE's? Voices 1 and 3 sounding through the hold (2026-11-24, the SPU
 * session)
 *
 * WHY. hwtests/spu-fillhold3 and spu-capstep on the SCPH-1000 and SCPH-1001: while 1F801DACh's bits 3-1 are clear the capture
 * ring's counter steps 4 a sample as an 11-bit address, and a 0000h lands at that address in each of 1000h-1FFFh, 2000h-2FFFh
 * and 3000h-3FFFh. Its top two bits pick one of the four capture rings' places (CD left, CD right, voice 1, voice 3) - and with
 * nothing sounding every capture is 0000h. If the write is the capture's, voice 1 sounding makes it voice 1's value where the
 * counter's top bits say 2; if it is a fill, it stays 0000h.
 *
 * WHAT IT DOES. Interrupts off, the main volume 0. A two-block looping sample of a steady level at 8000h; voices 1 and 3 keyed
 * on at pitch 1000h, the envelope straight to its top (their captures then read one steady value, printed as V). For each case:
 * 2000h-3FFFh written with a pattern, SPUSTAT bit 11's falling edge, 100 samples, 1F801DACh = V for 64 samples; 2000h-3FFFh
 * and the four rings (0000h-0FFFh) read back.
 *   V v1 v3                                       voice 1's and voice 3's ring values before any hold (index 8)
 *   H value : first i (b), changed n, values ... : rings z0 z1 z2 z3
 *       the first changed halfword of 2000h-2FFFh and b = its 512; how many of 2000h-3FFFh changed; the distinct values
 *       written (4 at most); and for each ring how many of its 512 read 0000h after the hold
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

static uint16_t big[4096] __attribute__((aligned(16)));
static uint16_t ring[2048] __attribute__((aligned(16)));
static uint8_t smp[64] __attribute__((aligned(16)));
static inline uint16_t pat(int i) { return (uint16_t)(0x4000 | (i & 0x3FFF)); }

static uint32_t edge() {
    for (int i = 0; i < 200000 && !(read16(SPU(0x1AE)) & 0x0800); i++) { now(); }
    for (int i = 0; i < 200000; i++) { const uint32_t t = now(); if (!(read16(SPU(0x1AE)) & 0x0800)) return t; }
    return 0;
}

static void holdCase(uint16_t value, uint32_t before) {
    for (int i = 0; i < 4096; i++) big[i] = pat(i);
    dma(0x2000, big, 4096 / 32, true);
    spuMode(0); write16(SPU(0x1A6), 0x2000 / 8);
    idle(SAMPLE * (8u + before));
    const uint32_t e = edge();
    while (now() - e < 100u * SAMPLE) {}
    write16(SPU(0x1AC), value);
    const uint32_t t0 = now();
    while (now() - t0 < 64u * SAMPLE) {}
    write16(SPU(0x1AC), 0x0004);
    idle(SAMPLE * 16u);
    dma(0x0000, ring, 2048 / 32, false);
    dma(0x2000, big, 4096 / 32, false);
    int total = 0, first = -1, nv = 0; uint16_t vals[4];
    for (int i = 0; i < 4096; i++) {
        if (big[i] == pat(i)) continue;
        total++; if (first < 0) first = i;
        int j = 0; while (j < nv && vals[j] != big[i]) j++;
        if (j == nv && nv < 4) vals[nv++] = big[i];
    }
    int z[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 2048; i++) if (ring[i] == 0) z[i >> 9]++;
    now();
    printf("H %04x : first %d (%d), changed %d, values", value, first, first < 0 ? -1 : (first >> 9) & 3, total);
    for (int j = 0; j < nv; j++) printf(" %04x", vals[j]);
    printf(" : rings %d %d %d %d\n", z[0], z[1], z[2], z[3]);
    now();
}

int main() {
    printf("\nspu/spu-fillhold4\n");
    printf("Is the type-0 hold's write the capture's? V v1 v3; H value : first (b), changed, values : rings' zero counts. No assertions.\n\n");
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

    /* the sample: two blocks of a steady level (shift 0, filter 0, every nibble 5), the second looping to the first */
    for (int i = 0; i < 64; i++) smp[i] = 0;
    smp[0] = 0x00; smp[1] = 0x04; for (int i = 2; i < 16; i++) smp[i] = 0x55;
    smp[16] = 0x00; smp[17] = 0x03; for (int i = 18; i < 32; i++) smp[i] = 0x55;
    dma(0x8000, smp, 1, true);
    for (int v = 1; v <= 3; v += 2) {
        const uint32_t o = (uint32_t)v * 16;
        write16(SPU(o + 0), 0); write16(SPU(o + 2), 0);
        write16(SPU(o + 4), 0x1000);
        write16(SPU(o + 6), 0x8000 / 8);
        write16(SPU(o + 8), 0x000F); write16(SPU(o + 10), 0x0000);
    }
    write16(SPU(0x188), 0x000A); write16(SPU(0x18A), 0);
    idle(SAMPLE * 1200u);
    dma(0x0000, ring, 2048 / 32, false);
    printf("V %04x %04x\n", ring[1024 + 8], ring[1536 + 8]);
    now();

    static const uint16_t B[] = { 0, 150, 300, 450, 600, 750, 900, 1050 };
    for (unsigned i = 0; i < 8; i++) holdCase(0x0000, B[i]);
    for (unsigned i = 0; i < 4; i++) holdCase(0x0002, B[i] * 2);
    holdCase(0x0004, 0);

    write16(SPU(0x18C), 0x000A); write16(SPU(0x18E), 0);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fillhold4: Done.\n");
    for (;;) VSync(0);
}
