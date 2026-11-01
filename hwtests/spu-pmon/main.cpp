/*
 * spu/spu-pmon - pitch modulation, timed by how long a sample takes to play (2026-11-01)
 *
 * WHY. No test here had touched PMON on a console, and games use it (FF7's New Game menu, 6 of ~1,800 key-ons). psx-spx gives the step:
 * (pitch signed x (previous voice's output + 8000h)) SAR 15, AND FFFFh, and above 3FFFh it becomes 4000h. This core clips to 3FFFh. Neither is
 * measured here, nor which output of the previous voice it means, nor what becomes of pitches of 8000h and up, which psx-spx calls glitches.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. No disc. The carrier plays N silent ADPCM blocks up to a block with the end flag, at
 * 7FFF0h. N is chosen for about 20,000 samples, so the time from KON to its ENDX bit, polled, gives the step to about 1 part in 20,000:
 *     step = N x 28 x 4096 x 768 / cycles.
 * The modulator is the voice before it, a DC block looping at a chosen level, volume 0.
 *   PLAIN  no PMON, pitches 0400h..FFFFh: the clip with no modulation.
 *   GRID   PMON on, modulator levels -8000h..+7000h by pitches 0400h..FFFFh.
 *   ENV    the modulator at sustain level 7, envelope 4000h: is it the output after the envelope?
 *   VOLUME the modulator's volume 3FFFh against 0: does its volume reach the step?
 *   SILENT the modulator never keyed.
 *   NOISE  the modulator in noise mode at the fastest rate.
 *   BIT0   PMON bit 0 on voice 0 with voice 23 at +4000h: psx-spx says voice 0 has no modulator.
 *
 * Each row prints the level, the pitch, N and the cycles, twice. "never" is 60,000 samples with no ENDX.
 *
 * READ IT LIKE THIS. Step from the formula above. PLAIN's 4000h row says the clip's value directly, a 3FFFh against 4000h is 1 part in 16,384.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-pmon --frames=30000 --quiet=3000`.
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
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool settle) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(2);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, 0x01000201u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (settle) idle(100000);
    spuMode(0);
}

#define LEVELS 8
/* level = nibble << (12 - shift), signed, filter 0 */
struct Lv { int16_t level; uint8_t nib, sh; };
static const Lv LV[LEVELS] = { { -0x8000, 8, 0 }, { -0x4000, 12, 0 }, { -0x2000, 14, 0 }, { -0x1000, 15, 0 }, { 0, 0, 0 }, { 0x1000, 1, 0 }, { 0x4000, 4, 0 }, { 0x7000, 7, 0 } };
static const uint16_t PITCH[11] = { 0x0400, 0x0800, 0x1000, 0x2000, 0x3FFF, 0x4000, 0x6000, 0x7FFF, 0x8000, 0xC000, 0xFFFF };

static uint32_t buf[1024] __attribute__((aligned(16)));
#define END_ADDR 0x7FFF0u
#define DC_ADDR(i) (0x1000u + (uint32_t)(i) * 16u)
#define SIL_LOW 0x2000u

static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);              /* PMON */
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);              /* NON */
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);              /* EON */
    idle(100000);
}
static void voice(int n, uint16_t pitch, uint32_t start, uint16_t vol, uint16_t adsr1) {
    const uint32_t b = (uint32_t)n * 0x10;
    write16(SPU(b + 0x0), vol); write16(SPU(b + 0x2), vol);
    write16(SPU(b + 0x4), pitch);
    write16(SPU(b + 0x6), (uint16_t)(start / 8));
    write16(SPU(b + 0x8), adsr1); write16(SPU(b + 0xA), 0x1FC0);  /* sustain held */
}
#define HOLD_A 0x000F

/* The step the formula expects, only to choose N. */
static uint32_t expectStep(int level, uint16_t pitch, bool pmon) {
    int32_t st = (int16_t)pitch;
    uint32_t s = pitch;
    if (pmon) s = (uint32_t)((st * (level + 0x8000)) >> 15) & 0xFFFFu;
    if (s > 0x3FFF) s = 0x4000;
    return s;
}
static int chooseN(uint32_t step) {
    uint32_t n = (step * 20000u / 4096u) / 28u;                 /* about 20,000 samples */
    if (n < 16) n = 16;
    const uint32_t most = (END_ADDR - SIL_LOW) / 16;
    if (n > most) n = most;
    return (int)n;
}
/* KON (mask) to the carrier's ENDX bit: cycles, or 0 for never. */
static uint32_t timed(int carrier, uint32_t konMask) {
    const uint32_t bit = 1u << carrier, cap = 60000u * 768u;
    write16(SPU(0x188), (uint16_t)konMask); write16(SPU(0x18A), (uint16_t)(konMask >> 16));
    const uint32_t t0 = now();
    idle(4000);                                                  /* ENDX clears at the key-on's sample */
    while (now() - t0 < cap) {
        const uint32_t e = (uint32_t)read16(SPU(0x19C)) | ((uint32_t)read16(SPU(0x19E)) << 16);
        if (e & bit) return now() - t0;
    }
    return 0;
}
static uint32_t run(int level, int lvIdx, uint16_t pitch, bool pmon, int* nOut, uint16_t modVol, uint16_t modA) {
    allOff();
    const uint32_t step = expectStep(level, pitch, pmon);
    const int n = chooseN(step ? step : 0x100);
    *nOut = n;
    const uint32_t start = END_ADDR - (uint32_t)n * 16u;
    voice(0, 0x1000, DC_ADDR(lvIdx), modVol, modA);
    voice(1, pitch, start, 0, HOLD_A);
    if (pmon) write16(SPU(0x190), 0x0002);
    return timed(1, 0x0003);
}

int main() {
    printf("\nspu/spu-pmon\n");
    printf("Pitch modulation, by the cycles a chain of N silent blocks takes. Step = N x 28 x 4096 x 768 / cycles. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);                   /* DMA4 on */
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);              /* main volume 0: nothing heard */
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    cnt = 0xFF00; write16(SPU(0x1AA), cnt);                      /* the fastest noise, for NOISE */
    write16(SPU(0x1AC), 0x0004);

    uint8_t* b = (uint8_t*)buf;
    for (int i = 0; i < LEVELS; i++) {
        uint8_t* p = b + i * 16;
        p[0] = LV[i].sh; p[1] = 0x07;                            /* filter 0; loop start + repeat on itself */
        for (int k = 2; k < 16; k++) p[k] = (uint8_t)(LV[i].nib | (LV[i].nib << 4));
    }
    allOff();
    spuDma(0x1000, buf, 32, true);
    /* silence from 2000h to the end block */
    for (int i = 0; i < 1024; i++) buf[i] = 0;
    for (uint32_t a = SIL_LOW; a < END_ADDR; a += 4096u) spuDma(a, buf, a + 4096u <= 0x80000u ? 1024 : 512, false);
    idle(100000);
    b[0] = 0; b[1] = 0x01;                                       /* the end flag, no repeat */
    for (int i = 2; i < 64; i++) b[i] = 0;
    spuDma(END_ADDR & ~0x3Fu, buf, 16, true);
    for (int i = 0; i < 16; i++) b[i] = 0;
    /* the end block sits at 7FFF0h: rewrite the 64 bytes around it so only it carries the flag */
    b[48] = 0; b[49] = 0x01;
    b[1] = 0;
    spuDma(END_ADDR & ~0x3Fu, buf, 16, true);

    printf("\nPLAIN: pitch N cycles, twice:\n");
    for (int p = 0; p < 11; p++) {
        int n = 0; uint32_t t[2];
        for (int r = 0; r < 2; r++) {
            allOff();
            n = chooseN(expectStep(0, PITCH[p], false));
            voice(1, PITCH[p], END_ADDR - (uint32_t)n * 16u, 0, HOLD_A);
            t[r] = timed(1, 0x0002);
        }
        printf(" pitch %04x N %5d: %u %u\n", PITCH[p], n, t[0], t[1]);
    }

    printf("\nGRID: level pitch N cycles, twice:\n");
    for (int l = 0; l < LEVELS; l++)
        for (int p = 0; p < 11; p++) {
            int n = 0; uint32_t t[2];
            for (int r = 0; r < 2; r++) t[r] = run(LV[l].level, l, PITCH[p], true, &n, 0, HOLD_A);
            printf(" level %6d pitch %04x N %5d: %u %u\n", LV[l].level, PITCH[p], n, t[0], t[1]);
        }

    printf("\nENV: the modulator at +7000h, sustain level 7 (envelope 4000h) against F, pitch 1000h:\n");
    for (int k = 0; k < 2; k++) {
        int n = 0; uint32_t t[2];
        for (int r = 0; r < 2; r++) t[r] = run(0x7000, 7, 0x1000, true, &n, 0, k ? 0x0007 : HOLD_A);
        printf(" level %s N %5d: %u %u\n", k ? "7" : "F", n, t[0], t[1]);
    }
    printf("\nVOLUME: the modulator's volume at +4000h, pitch 1000h:\n");
    for (int k = 0; k < 2; k++) {
        int n = 0; uint32_t t[2];
        for (int r = 0; r < 2; r++) t[r] = run(0x4000, 6, 0x1000, true, &n, k ? 0x3FFF : 0, HOLD_A);
        printf(" volume %04x N %5d: %u %u\n", k ? 0x3FFF : 0, n, t[0], t[1]);
    }
    {
        int n = chooseN(0x1000); uint32_t t[2];
        for (int r = 0; r < 2; r++) {
            allOff();
            voice(1, 0x1000, END_ADDR - (uint32_t)n * 16u, 0, HOLD_A);
            write16(SPU(0x190), 0x0002);
            t[r] = timed(1, 0x0002);
        }
        printf("\nSILENT: the modulator never keyed, pitch 1000h, N %d: %u %u\n", n, t[0], t[1]);
    }
    {
        int n = chooseN(0x1000); uint32_t t[2];
        for (int r = 0; r < 2; r++) {
            allOff();
            voice(0, 0x1000, DC_ADDR(6), 0, HOLD_A);
            voice(1, 0x1000, END_ADDR - (uint32_t)n * 16u, 0, HOLD_A);
            write16(SPU(0x190), 0x0002);
            write16(SPU(0x194), 0x0001);
            t[r] = timed(1, 0x0003);
        }
        printf("\nNOISE: the modulator in noise mode, pitch 1000h, N %d: %u %u\n", n, t[0], t[1]);
    }
    {
        int n = chooseN(0x1000); uint32_t t[2];
        for (int r = 0; r < 2; r++) {
            allOff();
            voice(23, 0x1000, DC_ADDR(6), 0, HOLD_A);
            voice(0, 0x1000, END_ADDR - (uint32_t)n * 16u, 0, HOLD_A);
            write16(SPU(0x190), 0x0001);
            t[r] = timed(0, 0x00800001u);
        }
        printf("\nBIT0: PMON bit 0 on voice 0, voice 23 at +4000h, pitch 1000h, N %d: %u %u\n", n, t[0], t[1]);
    }
    allOff();

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-pmon: Done.\n");
    for (;;) VSync(0);
}
