/*
 * spu/spu-dmaread3 - WHAT A DMA READ OF SOUND RAM DELIVERS at every block size and every value of 1F801014h's bits 24-27
 * (2026-11-24, the SPU session)
 *
 * WHY. Two block sizes (16 and 17 words) at two values of 1F801014h's bits 24-27 (0, the BIOS's "unstable" read, and 2) are
 * measured (hwtests/spu-dma, three consoles); this emulator's rule for the rest - a 32-halfword FIFO from the block's address, a
 * bubble first when unstable, its last entry repeated once it runs dry, the next block 32 halfwords on - is "follows from it,
 * unmeasured" (SPU.dmaReadUnstable, dmaRead's "under 16 nothing is measured"; experiments/proposals/spu-gaps.md row 6).
 *
 * WHAT IT DOES. Interrupts off, sound muted, nothing keyed on. Sound RAM 1000h-11FFh holds a ramp, halfword n = 1000h + n
 * (n = 0-FFh), written with 1F801014h = 220931E1h and left to settle. Then, for each value k of bits 24-27 (0, 1, 2, 3, 4, 6, 8,
 * Ch, Fh: 1F801014h = 2k0931E1h) and each block size (1, 2, 3, 4, 8, 15, 16, 17, 18, 24, 31, 32, 33, 48, 64 words): the transfer
 * address set to 1000h, DMA read mode, DMA4 in blocks of that size for at least 64 words, twice.
 *   D k bs r : t0 t1 ...     the halfwords read, each as the ramp's index n in two hex digits, "--" for FFFFh, or four hex
 *                            digits for anything else (r: the run, 0 or 1); the first 96
 * experiments/spu_dmaread3.js lines a console's rows up against an emulator's. Every wait is bounded. No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-dmaread3 --frames=6000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

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
static bool dma(uint32_t spuAddr, uint32_t* ram, int blocks, int bs, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)blocks << 16) | (uint32_t)bs);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    bool ok = true;
    for (int i = 0; i < 600000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); ok = false; }
    if (toSpu) idle(100000);
    spuMode(0);
    return ok;
}

static uint32_t ramp[128] __attribute__((aligned(16)));
static uint32_t got[160] __attribute__((aligned(16)));
static const uint8_t KS[9] = { 0, 1, 2, 3, 4, 6, 8, 12, 15 };
static const uint8_t BS[15] = { 1, 2, 3, 4, 8, 15, 16, 17, 18, 24, 31, 32, 33, 48, 64 };

int main() {
    printf("\nspu/spu-dmaread3\n");
    printf("A DMA read of sound RAM by block size and 1F801014h's bits 24-27: D k bs run : the ramp's indices read. No assertions.\n\n");
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
    write16(SPU(0x1A2), 0xFFFE);                    /* the reverb's area: the top 16 bytes */
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    uint16_t* r16 = (uint16_t*)ramp;
    for (int n = 0; n < 256; n++) r16[n] = (uint16_t)(0x1000 + n);
    dma(0x1000, ramp, 8, 16, true);

    for (int ki = 0; ki < 9; ki++) for (int bi = 0; bi < 15; bi++) for (int r = 0; r < 2; r++) {
        const int bs = BS[bi], blocks = (64 + bs - 1) / bs;
        for (int i = 0; i < 160; i++) got[i] = 0xEEEEEEEEu;
        write32(0x1F801014, 0x200931E1u | ((uint32_t)KS[ki] << 24));
        const bool ok = dma(0x1000, got, blocks, bs, false);
        write32(0x1F801014, 0x220931E1);
        now();
        printf("D %x %d %d :", KS[ki], bs, r);
        if (!ok) printf(" timeout");
        const uint16_t* g = (const uint16_t*)got;
        for (int i = 0; i < 96; i++) {
            const uint16_t v = g[i];
            if (v == 0xFFFF) printf(" --");
            else if (v >= 0x1000 && v < 0x1100) printf(" %02x", v - 0x1000);
            else printf(" %04x", v);
            if ((i & 15) == 15) now();
        }
        printf("\n");
        now();
    }
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-dmaread3: Done.\n");
    for (;;) VSync(0);
}
