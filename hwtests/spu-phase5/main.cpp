/*
 * spu/spu-phase5 - spu-phase2's class against every transfer TYPE (1F801DACh bits 1-3), eight actions in a row each: Rep4 (0008h)
 * alone, alternating with 0000h, alternating with a Rep4 hold of 100 cycles (no edge inside); Rep2 0006h, Rep8 000Ah, Fill
 * 000Eh, 0005h (Normal + bit 0), 0001h (Fill + bit 0), 0010h (bit 4). spu-phase4 on the 5501: 000Ch (Fill, bit 2 set) flips
 * like 0000h; Rep4 3 actions in 4 (experiments/_mkspuphase5.js).
 * THE RULE: run it here first: `node hwtests/run.js spu-phase5 --frames=6000 --quiet=6000`. spu-phase2's header follows.
 *
 * spu/spu-phase2 - how a transfer-control write moves the SPU's two-sample phase against its capture ring.
 *
 * WHY (2026-10-27 night). hwtests/spu-phase on the SCPH-7501 (six runs): of eight actions only the transfer control's
 * 0000h-then-0004h (1F801DACh, 1,000 cycles at 0) moves the class - every time but the first of each six - and the class
 * carries from one run to the next (each run starts where the last ended). psx-spx: SPUSTAT bit 11 "works only if Bit2
 * and/or Bit3 of Port 1F801DACh are set". If the capture position stands still while bits 2-3 are clear, the class flips
 * when an odd number of samples pass in between: this sweeps how long they stay clear, and which bits are written.
 *
 * WHAT IT DOES. As spu-phase - the class read as voice 1's onset in its capture ring, KON written 96 cycles after SPUSTAT
 * bit 11 falls (8 or 9) - each action four times, each followed by a reading: the control at 0 for 100, 300, 600, 900,
 * 1,200, 1,536, 2,000, 2,304 and 3,072 cycles, then 0004h again; 0004h written again (no change); 0008h for 1,000 cycles
 * then 0004h (bit 3 instead of bit 2); 0002h for 1,000 then 0004h (bit 1 alone: neither 2 nor 3). Interrupts off, Timer 2
 * at the system clock, the SPU's main volume 0.
 *
 * READ IT LIKE THIS. A capture position that stands still while bits 2-3 are clear flips the class when an odd number of
 * sample edges (every 768 cycles) fall inside the hold - at the short holds by where the write lands in the sample, at the
 * long ones by the count: 1,536 (two samples) mostly not, 2,304 (three) mostly yes. A flip at every hold whatever its length
 * says the write itself moves it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-phase2 --frames=6000 --quiet=6000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
/* DMA4 between RAM and sound RAM, `words` words in 16-word blocks (spu-latch's). */
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (toSpu) idle(100000);
    spuMode(0);
}

static uint32_t blocks[16] __attribute__((aligned(16)));
static uint16_t ring[512] __attribute__((aligned(16)));

static void voiceSetup() {
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);             /* main volume 0: nothing heard */
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);   /* voice 1 */
    write16(SPU(0x014), 0x1000);
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);
}
static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
}
/* SPUSTAT bit 11 becoming `want` (bounded: a console that never changes it reads FFFFh, not a hang - rule 1). */
static bool edge(int want, uint32_t* at) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w) if (now() - t0 > 2000000u) return false;
    while ((read16(SPU(0x1AE)) & 0x0800) != w) if (now() - t0 > 2000000u) return false;
    *at = now();
    return true;
}
/* The class: voice 1's onset, KON written 96 cycles after bit 11 falls. */
static uint16_t reading() {
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 256; i++) ((uint32_t*)ring)[i] = 0;
    spuDma(0x0800, (uint32_t*)ring, 256, true);
    voiceSetup();
    uint32_t t0;
    if (!edge(0, &t0)) return 0xFFFF;
    while (now() - t0 < 96) {}
    write16(SPU(0x188), 0x0002);
    idle(150u * 768u);
    spuDma(0x0800, (uint32_t*)ring, 256, false);
    write16(SPU(0x18C), 0x0002);
    for (int i = 0; i < 256; i++) if (ring[i]) return (uint16_t)i;
    return 0xFFFE;
}
#define NA 9
/* each row eight actions: value and hold of the even ones, then of the odd ones */
static const uint16_t VE[NA] = { 0x0008, 0x0008, 0x0008, 0x0006, 0x000A, 0x000E, 0x0005, 0x0001, 0x0010 };
static const uint16_t VO[NA] = { 0x0008, 0x0000, 0x0008, 0x0006, 0x000A, 0x000E, 0x0005, 0x0001, 0x0010 };
static const uint32_t HE[NA] = { 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000 };
static const uint32_t HO[NA] = { 1000, 1000,  100, 1000, 1000, 1000, 1000, 1000, 1000 };
static const char* const NAMES[NA] = { "8 x1000", "8 / 0", "8 / 8x100", "6 x1000", "A x1000", "E x1000", "5 x1000", "1 x1000",
    "10 x1000" };
static void action(uint16_t v, uint32_t h) { write16(SPU(0x1AC), v); idle(h); write16(SPU(0x1AC), 0x0004); idle(1000); }
static uint16_t first, res[NA][8];

int main() {
    printf("\nspu/spu-phase5\n");
    printf("Voice 1's onset in its capture ring, KON 96 cycles after SPUSTAT bit 11 falls (8 or 9: spu-latch's two classes),\n");
    printf("after the transfer control held at each transfer type for 1,000 cycles, then 0004h, eight in a row. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                            /* stable sound RAM reads (spu-dma) */
    write16(SPU(0x1AC), 0x0004);                                /* bit 11 needs 1F801DACh bit 2 or 3 (psx-spx) */
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;                   /* 4000h every sample */
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 16, true);
    first = reading();
    for (int a = 0; a < NA; a++)
        for (int r = 0; r < 8; r++) { if (r & 1) action(VO[a], HO[a]); else action(VE[a], HE[a]); res[a][r] = reading(); }
    allOff();
    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("at the start: onset %u\n", (unsigned)first);
    for (int a = 0; a < NA; a++) {
        printf("  %-10s", NAMES[a]);
        for (int r = 0; r < 8; r++) printf(" %3u", (unsigned)res[a][r]);
        printf("\n");
    }
    printf("\nspu/spu-phase5: Done.\n");
    for (;;) VSync(0);
}
