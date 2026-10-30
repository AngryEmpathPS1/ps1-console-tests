/*
 * spu/spu-phase - what moves the SPU's two-sample phase against its capture ring?
 *
 * WHY (STATUS v0.377.205). hwtests/spu-latch's runs fall in two classes on every console - voice 1's onset in its capture
 * ring 8 (stepping to 10 later in the sample) with the reverb's first writes at step 0, or 9 with them at step 1 - and the
 * 5501, 7501, 1001, 9001 and 5000 each show BOTH, sometimes alternating run to run in one runner session. Inside one run it
 * never changes, though the run rewrites SPUCNT (bit 15 kept on) and the DMA mode dozens of times. So the counter behind
 * SPUSTAT bit 11 (the capture position) and the SPU's two-sample cycle (the key-on grid, the reverb's left / right) are two
 * counters, and something between runs moves one against the other. The runner's restore (hwtests/runner restoreKernel)
 * does, every test: KOFF all, the voice and FM / noise / EON registers zeroed, sound RAM zeroed by DMA4 (512 KB), the
 * transfer control 0004h, SPUCNT 8000h then the snapshot's - never bit 15 off.
 *
 * WHAT IT DOES. The class, read as spu-latch reads its KON rows at one delay: voice 1 on a looping block of 4000h, its ring
 * (0800h-0BFFh) zeroed, KON written D = 96 cycles after SPUSTAT bit 11 falls, and the onset counted from the half the edge
 * began (8: the first class, 9: the second, at this D). Then each ACTION, six times, each followed by a reading:
 *   none        nothing (the control: a reading is the class it found)
 *   off-on      SPUCNT 0000h for 20,000 cycles (the SPU off), then C000h
 *   dma-512k    sound RAM zeroed by DMA4 as the runner does (512 x 1 KB), then the voice's block again
 *   dma-1k      one 1 KB DMA4 write
 *   xfer        the transfer control 0000h, then 0004h
 *   mute        SPUCNT 8000h (muted) for 20,000 cycles, then C000h
 *   reverb      SPUCNT C080h (the reverb's master enable) for 20,000 cycles, then C000h
 *   idle-odd    768 x 2 x 1,000 + 768 cycles of nothing (a sample over an even count: a counter that pauses would show)
 * Interrupts off; Timer 2 at the system clock. The SPU's main volume 0 throughout: nothing is heard.
 *
 * READ IT LIKE THIS. An action that never changes the class leaves its six readings the control's; one that moves the two
 * counters apart flips them, at random or every time. A console that reads one class all through has its phase set before
 * the test - by the runner, between runs.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-phase --frames=6000 --quiet=6000`.
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
static uint32_t zeros[256] __attribute__((aligned(16)));

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
static void putBlock() { spuDma(0x1000, blocks, 16, true); }
static void action(int a) {
    switch (a) {
        case 0: break;
        case 1: cnt = 0x0000; write16(SPU(0x1AA), cnt); idle(20000); cnt = 0xC000; write16(SPU(0x1AA), cnt); idle(20000); break;
        case 2: for (int n = 0; n < 512; n++) spuDma(n * 1024u, zeros, 256, true); putBlock(); break;
        case 3: spuDma(0x2000, zeros, 256, true); break;
        case 4: write16(SPU(0x1AC), 0x0000); idle(1000); write16(SPU(0x1AC), 0x0004); idle(1000); break;
        case 5: cnt = 0x8000; write16(SPU(0x1AA), cnt); idle(20000); cnt = 0xC000; write16(SPU(0x1AA), cnt); idle(20000); break;
        case 6: cnt = 0xC080; write16(SPU(0x1AA), cnt); idle(20000); cnt = 0xC000; write16(SPU(0x1AA), cnt); idle(20000); break;
        case 7: idle(768u * 2000u + 768u); break;
    }
}
static const char* const NAMES[8] = { "none", "off-on", "dma-512k", "dma-1k", "xfer", "mute", "reverb", "idle-odd" };
static uint16_t first, res[8][6];

int main() {
    printf("\nspu/spu-phase\n");
    printf("Voice 1's onset in its capture ring, KON 96 cycles after SPUSTAT bit 11 falls (8 or 9: spu-latch's two classes),\n");
    printf("before and after each action, six times each. No assertions - compare against your emulator.\n\n");
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
    for (int i = 0; i < 256; i++) zeros[i] = 0;
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    putBlock();
    first = reading();
    for (int a = 0; a < 8; a++)
        for (int r = 0; r < 6; r++) { action(a); res[a][r] = reading(); }
    allOff();
    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("at the start: onset %u\n", (unsigned)first);
    for (int a = 0; a < 8; a++) {
        printf("  %-9s", NAMES[a]);
        for (int r = 0; r < 6; r++) printf(" %3u", (unsigned)res[a][r]);
        printf("\n");
    }
    printf("\nspu/spu-phase: Done.\n");
    for (;;) VSync(0);
}
