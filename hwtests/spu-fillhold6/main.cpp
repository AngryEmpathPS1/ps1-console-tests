/*
 * spu/spu-fillhold6 - where each of the four captures lands in a hold of transfer type 0 and of type 1, value by value
 * (2026-11-24, the SPU session)
 *
 * WHY. hwtests/spu-fillhold5 on the SCPH-1000 and SCPH-1001: in a type-0 hold each sample leaves 0000h in 1000h-1FFFh and the
 * voices' value in 2000h-2FFFh and 3000h-3FFFh at the capture counter's 11-bit place - the four captures (CD left, CD right,
 * voice 1, voice 3), each 1000h bytes on from the last; in a type-1 hold (0002h) 64 samples leave 64 halfwords of the voices'
 * value in 1000h-1FFFh, where four captures 800h apart would leave 128 - and both voices had the same level, so which is which
 * was not seen, nor what lands in the rings themselves (0000h-0FFFh).
 *
 * WHAT IT DOES. Interrupts off, the main volume 0. 1000h-3FFFh written with a pattern (halfword i = 4000h | i); two looping
 * samples of 64 blocks at 20000h and 21000h, voice 1 at one steady level (nibble 5) and voice 3 at another (nibble 3), every
 * other voice off; the two levels as the rings hold them printed as V. For each case: the pattern again, the voices keyed on
 * afresh, SPUSTAT bit 11's falling edge, 100 samples, 1F801DACh = the value for 12 samples, 0004h; 0000h-3FFFh read back.
 *   V v1 v3
 *   P value : a+c/s=v ...     the changed halfwords of 1000h-3FFFh as runs: c of them from halfword address a (hex, of the
 *                             whole area: 800h = byte 1000h), s apart, reading v (the first's value)
 *   Q value : a+c/s=v ...     the ring area (halfwords 0-7FFh) the same way, a halfword counting as changed when it is not its
 *                             ring's own steady value (0000h in the CD rings, v1 in voice 1's, v3 in voice 3's)
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
#define HW 8192

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

static uint16_t big[HW] __attribute__((aligned(16)));
static uint8_t smp[1024] __attribute__((aligned(16)));
static uint16_t v1, v3;
static inline uint16_t pat(int i) { return (uint16_t)(0x4000 | i); }
static uint16_t steady(int i) { return i < 0x400 ? 0 : i < 0x600 ? v1 : i < 0x800 ? v3 : pat(i); }

static uint32_t edge() {
    for (int i = 0; i < 200000 && !(read16(SPU(0x1AE)) & 0x0800); i++) { now(); }
    for (int i = 0; i < 200000; i++) { const uint32_t t = now(); if (!(read16(SPU(0x1AE)) & 0x0800)) return t; }
    return 0;
}
static void sample(uint32_t at, uint8_t nib) {
    for (int b = 0; b < 64; b++) {
        smp[b * 16] = 0x00; smp[b * 16 + 1] = b == 0 ? 0x04 : b == 63 ? 0x03 : 0x00;
        for (int i = 2; i < 16; i++) smp[b * 16 + i] = nib;
    }
    dma(at, smp, 16, true);
}
static void keyOn() {
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(SAMPLE * 32u);
    for (int v = 1; v <= 3; v += 2) {
        const uint32_t o = (uint32_t)v * 16;
        write16(SPU(o + 0), 0); write16(SPU(o + 2), 0);
        write16(SPU(o + 4), 0x1000);
        write16(SPU(o + 6), (uint16_t)((v == 1 ? 0x20000u : 0x21000u) / 8));
        write16(SPU(o + 8), 0x000F); write16(SPU(o + 10), 0x0000);
    }
    write16(SPU(0x188), 0x000A); write16(SPU(0x18A), 0);
}
static void runs(int from, int to) {
    int shown = 0;
    for (int i = from; i < to && shown < 14; ) {
        if (big[i] == steady(i)) { i++; continue; }
        int j = i + 1; while (j < to && big[j] == steady(j)) j++;
        const int s = j < to ? j - i : 0;
        int c = 1, last = i;
        if (s) for (int q = i + s; q < to && big[q] != steady(q); q += s) {
            bool clean = true; for (int z = last + 1; z < q; z++) if (big[z] != steady(z)) { clean = false; break; }
            if (!clean) break;
            c++; last = q;
        }
        printf(" %x+%d/%d=%04x", i, c, c > 1 ? s : 0, big[i]);
        shown++; i = last + 1;
        if ((shown & 3) == 3) now();
    }
}

static void holdCase(uint16_t value) {
    for (int i = 0; i < HW; i++) big[i] = pat(i);
    dma(0x1000, big + 0x800, (HW - 0x800) / 32, true);
    keyOn();
    idle(SAMPLE * 700u);                       /* the rings full of the steady values */
    spuMode(0); write16(SPU(0x1A6), 0x4000 / 8);
    const uint32_t e = edge();
    while (now() - e < 100u * SAMPLE) {}
    write16(SPU(0x1AC), value);
    const uint32_t t0 = now();
    while (now() - t0 < 12u * SAMPLE) {}
    write16(SPU(0x1AC), 0x0004);
    dma(0x0000, big, HW / 32, false);
    now();
    printf("P %04x :", value); runs(0x800, HW); printf("\n"); now();
    printf("Q %04x :", value); runs(0, 0x800); printf("\n"); now();
}

int main() {
    printf("\nspu/spu-fillhold6\n");
    printf("Where each capture lands in a hold of type 0 and of type 1: V v1 v3; P value : runs of 1000h-3FFFh; Q value : runs of the rings. No assertions.\n\n");
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

    sample(0x20000, 0x55);
    sample(0x21000, 0x33);
    keyOn();
    idle(SAMPLE * 700u);
    dma(0x0000, big, 0x800 / 32, false);
    v1 = big[0x400 + 40]; v3 = big[0x600 + 40];
    printf("V %04x %04x\n", v1, v3);
    now();

    holdCase(0x0004);
    holdCase(0x0000);
    holdCase(0x0000);
    holdCase(0x0000);
    holdCase(0x0002);
    holdCase(0x0002);
    holdCase(0x0002);
    holdCase(0x0002);

    write16(SPU(0x18C), 0x000A); write16(SPU(0x18E), 0);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fillhold6: Done.\n");
    for (;;) VSync(0);
}
