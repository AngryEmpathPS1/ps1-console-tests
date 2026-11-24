/*
 * spu/spu-fillhold - does a HOLD of a non-Normal transfer type write to sound RAM with no transfer running? (2026-11-24, the SPU
 * session)
 *
 * WHY. hwtests/spu-rulerdump on the SCPH-1001 and SCPH-1000: after 1F801DACh is held at 0000h / 0001h / 0002h for 100-300
 * samples the voice-1 capture ring of a plain linear attack is no staircase any more - steps of no size, steps back, the same
 * on every later key-on, and gone when a new program uploads its sample again - where the second 5501's stays clean. That reads
 * as the voice's own sample at 1000h being written over: the "Fill" types filling sound RAM from the transfer address on the
 * older SPU, with no transfer asked for. This looks.
 *
 * WHAT IT DOES. Interrupts off, sound muted, nothing keyed on. For each case: a ramp (halfword n = 4000h + n, n = 0-7FFh) written
 * to 2000h-2FFFh and settled; the transfer address set to 2000h, SPUCNT's transfer mode 0; 1F801DACh written to the case's
 * value for the case's samples, then 0004h; 2000h-2FFFh read back (1F801014h = 220931E1h, blocks of 16 words).
 *   cases: 0004h 100 (the control); 0000h 16 / 100 / 300; 0001h 100; 0002h 100; 0006h 100; 000Ch 100; and 0000h 100 with the
 *   transfer address left where a DMA read of 2000h-23FFh ended instead of written
 *   F case value samples : changed n, first i, last j : i=v i=v ...     how many of the 2,048 halfwords read back other than
 *                          written, the first and the last such index, and the first 20 (index = value read, hex)
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
static void dma(uint32_t spuAddr, uint32_t* ram, int blocks, bool toSpu, bool setAddr) {
    spuMode(0);
    if (setAddr) write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)blocks << 16) | 16u);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 2000000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); printf("T\n"); }
    if (toSpu) idle(100000);
    spuMode(0);
}

static uint32_t ramp[1024] __attribute__((aligned(16)));
static uint32_t got[1024] __attribute__((aligned(16)));

static void fillCase(int id, uint16_t value, uint32_t samples, bool afterRead) {
    dma(0x2000, ramp, 64, true, true);
    if (afterRead) dma(0x2000, got, 16, false, true);          /* the address left 400h bytes on by a read */
    else { spuMode(0); write16(SPU(0x1A6), 0x2000 / 8); }
    idle(SAMPLE * 8u);
    write16(SPU(0x1AC), value);
    idle(samples * SAMPLE);
    write16(SPU(0x1AC), 0x0004);
    idle(SAMPLE * 64u);
    dma(0x2000, got, 64, false, true);
    const uint16_t* g = (const uint16_t*)got;
    int n = 0, first = -1, last = -1;
    for (int i = 0; i < 2048; i++) if (g[i] != (uint16_t)(0x4000 + i)) { n++; if (first < 0) first = i; last = i; }
    now();
    printf("F %d %04x %u%s : changed %d, first %d, last %d :", id, value, (unsigned)samples, afterRead ? " after-read" : "", n, first, last);
    int shown = 0;
    for (int i = 0; i < 2048 && shown < 20; i++) if (g[i] != (uint16_t)(0x4000 + i)) { printf(" %x=%04x", i, g[i]); shown++; if ((shown & 7) == 7) now(); }
    printf("\n");
    now();
}

int main() {
    printf("\nspu/spu-fillhold\n");
    printf("Does a hold of a non-Normal transfer type write to sound RAM with no transfer running? F case value samples : changed ... No assertions.\n\n");
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
    uint16_t* r16 = (uint16_t*)ramp;
    for (int n = 0; n < 2048; n++) r16[n] = (uint16_t)(0x4000 + n);

    fillCase(0, 0x0004, 100, false);
    fillCase(1, 0x0000, 16, false);
    fillCase(2, 0x0000, 100, false);
    fillCase(3, 0x0000, 300, false);
    fillCase(4, 0x0001, 100, false);
    fillCase(5, 0x0002, 100, false);
    fillCase(6, 0x0006, 100, false);
    fillCase(7, 0x000C, 100, false);
    fillCase(8, 0x0000, 100, true);

    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fillhold: Done.\n");
    for (;;) VSync(0);
}
