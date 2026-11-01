/*
 * spu/spu-noise - the SPU's noise generator, read off the capture ring (2026-11-01)
 *
 * WHY. No test here had ever listened to noise on a console: every SPU test writes NON to turn it off. Games use it (FF7's New Game
 * menu, 13 of ~1,800 key-ons), and two descriptions disagree in form: psx-spx's shift register (a timer stepping 4..7 a sample, a
 * parity of bits 15, 12, 11, 10 clocked in) and this core's table, Dr Hell's waveform by way of pcsx-r and DuckStation. This asks the console.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, stable sound RAM reads. No disc. Voice 1 in noise mode with volume 0, nothing
 * heard: its capture ring (0800h-0BFFh, after the envelope) read a half at a time as SPUSTAT bit 11 passes each, so 256-sample
 * halves run on with nothing lost.
 *   RATE   every shift 0..15 and step 0..3: 1,024 samples; the sign bits of the first 512 in hex, MSB first, the flips in all 1,024, lowest, highest.
 *   SLOW   shifts 0..3 at step 0: the sample number of the first flips, up to 1,200 halves.
 *   LEVEL  sustain level 7 (envelope 4000h), held: lowest and highest, against sustain level F's.
 *   TWO    voices 1 and 3 in noise mode, keyed at different times: the samples of 256 where their rings differ.
 *   ADPCM  a one-block sample with the end flag, no repeat, in noise mode and not: ENDX, the envelope, flips, after 300 samples.
 *   PITCH  voice 1's pitch 0, 1000h and 4000h at one noise rate: flips of 1,024.
 *   NON    voice 1 playing DC 4000h, NON written D cycles after the bit 11 edge, D = 0, 64 ... 704, twice: the ring index of the first sample off DC.
 *   RESET  the sign bits of 256 samples at the fastest rate and its first 16 samples, three times with SPUCNT 0 held for 100,000 cycles before, then twice without.
 *
 * READ IT LIKE THIS. RATE's flips give the period by shift and step. Where a run is a sample long the hex IS the generator's bit stream, so
 * shift 15 tells the register from a table. RESET says whether the stream restarts at the same place, and so what it starts from.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-noise --frames=6000 --quiet=3000`.
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
/* DMA4 between RAM and sound RAM, `words` words in 16-word blocks; an upload settled 100,000 cycles (spu-dma). */
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
/* SPUSTAT bit 11 becoming `want`, bounded: a half is 196,608 cycles. */
static void edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
}

static uint32_t dcBlock[16] __attribute__((aligned(16)));
static uint32_t oneShot[16] __attribute__((aligned(16)));
#define HALF 256
#define MAXH 8
static uint16_t cap[HALF * MAXH] __attribute__((aligned(16)));
static uint16_t cap2[HALF * 2] __attribute__((aligned(16)));

static void voice(int n, uint16_t pitch, uint32_t start, uint16_t adsr1, uint16_t adsr2) {
    const uint32_t b = (uint32_t)n * 0x10;
    write16(SPU(b + 0x0), 0); write16(SPU(b + 0x2), 0);          /* volume 0: nothing heard, and the ring is before it */
    write16(SPU(b + 0x4), pitch);
    write16(SPU(b + 0x6), (uint16_t)(start / 8));
    write16(SPU(b + 0x8), adsr1); write16(SPU(b + 0xA), adsr2);
}
static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);              /* NON */
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);              /* PMON */
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);              /* EON */
    idle(100000);
}
static void setCnt(int shift, int step) {
    cnt = (uint16_t)(0xC000 | (shift << 10) | (step << 8));
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);                                 /* SPUSTAT bit 11 needs a transfer type (spu-latch) */
}
/* `halves` halves of voice `v`'s ring (1 or 3) running on from the next edge. */
static void capture(int v, uint16_t* to, int halves) {
    const uint32_t ring = v == 3 ? 0x0C00u : 0x0800u;
    for (int h = 0; h < halves; h++) {
        const int second = h & 1;                                /* the half just filled: 0 the second half, 1 the first */
        edge(second);
        spuDma(second ? ring : ring + 0x200, (uint32_t*)(to + h * HALF), HALF / 2, false);
    }
}
static void hexBits(const uint16_t* s, int n) {
    uint32_t w = 0; int k = 0, line = 0;
    for (int i = 0; i < n; i++) {
        w = (w << 1) | ((s[i] & 0x8000) ? 1u : 0u);
        if (++k == 32) { printf("%08x%s", w, ++line % 8 == 0 ? "\n" : " "); k = 0; w = 0; }
    }
    if (line % 8) printf("\n");
}
static int flips(const uint16_t* s, int n) { int f = 0; for (int i = 1; i < n; i++) if (s[i] != s[i - 1]) f++; return f; }
static void range(const uint16_t* s, int n, int* lo, int* hi) {
    *lo = 0x7FFF; *hi = -0x8000;
    for (int i = 0; i < n; i++) { const int v = (int16_t)s[i]; if (v < *lo) *lo = v; if (v > *hi) *hi = v; }
}
#define HOLD_A 0x000F                                            /* the fastest attack, sustain level F */
#define HOLD_S 0x1FC0                                            /* sustain held */
#define SETTLE (64u * 768u)

int main() {
    printf("\nspu/spu-noise\n");
    printf("The noise generator off voice 1's capture ring (44.1 kHz samples, 256 a half). No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);                   /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                             /* stable sound RAM reads (spu-dma) */
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);              /* main volume 0 */
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);

    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;                                                 /* loop start + repeat on itself */
    for (int i = 2; i < 16; i++) b[i] = 0x44;                    /* 4000h every sample */
    uint8_t* e = (uint8_t*)oneShot;
    for (int i = 0; i < 64; i++) e[i] = 0;
    e[1] = 0x01;                                                 /* the end flag, no repeat */
    for (int i = 2; i < 16; i++) e[i] = 0x44;
    allOff(); setCnt(15, 3);
    spuDma(0x1000, dcBlock, 16, true);
    spuDma(0x1100, oneShot, 16, true);

    printf("\nRATE: shift step: flips of 1024, lowest, highest, then the sign bits of 512 samples:\n");
    for (int sh = 0; sh < 16; sh++)
        for (int st = 0; st < 4; st++) {
            allOff(); setCnt(sh, st);
            voice(1, 0x1000, 0x1000, HOLD_A, HOLD_S);
            write16(SPU(0x194), 0x0002);
            write16(SPU(0x188), 0x0002);
            idle(SETTLE);
            capture(1, cap, 4);
            int lo, hi; range(cap, HALF * 4, &lo, &hi);
            printf(" shift %2d step %d: flips %4d  lo %6d hi %6d\n", sh, st, flips(cap, HALF * 4), lo, hi);
            hexBits(cap, 512);
        }

    printf("\nSLOW: shift step 0: the sample numbers of the first flips:\n");
    for (int sh = 0; sh < 4; sh++) {
        allOff(); setCnt(sh, 0);
        voice(1, 0x1000, 0x1000, HOLD_A, HOLD_S);
        write16(SPU(0x194), 0x0002);
        write16(SPU(0x188), 0x0002);
        idle(SETTLE);
        printf(" shift %d:", sh);
        int found = 0, last = -1;
        for (int h = 0; h < 1200 && found < 8; h++) {
            capture(1, cap, 1 + (h & 1) * 0);
            for (int i = 0; i < HALF && found < 8; i++) {
                const int s = cap[i];                    /* a change of value: psx-spx has the whole 16-bit register as the output */
                if (last >= 0 && s != last) { printf(" %d", h * HALF + i); found++; }
                last = s;
            }
        }
        printf("\n");
    }

    printf("\nLEVEL: sustain level, envelope, lowest, highest:\n");
    for (int k = 0; k < 2; k++) {
        allOff(); setCnt(15, 3);
        voice(1, 0x1000, 0x1000, k ? 0x0007 : HOLD_A, HOLD_S);
        write16(SPU(0x194), 0x0002);
        write16(SPU(0x188), 0x0002);
        idle(SETTLE * 4);
        const uint16_t env = read16(SPU(0x01C));
        capture(1, cap, 2);
        int lo, hi; range(cap, HALF * 2, &lo, &hi);
        printf(" level %s: env %04x lo %6d hi %6d\n", k ? "7" : "F", env, lo, hi);
    }

    allOff(); setCnt(15, 3);
    voice(1, 0x1000, 0x1000, HOLD_A, HOLD_S); voice(3, 0x0800, 0x1000, HOLD_A, HOLD_S);
    write16(SPU(0x194), 0x000A);
    write16(SPU(0x188), 0x0002); idle(17u * 768u + 100u); write16(SPU(0x188), 0x0008);
    idle(SETTLE);
    edge(1);                                                     /* a half of each, read on the same edge */
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap, HALF / 2, false);
    spuDma(0x0E00, (uint32_t*)cap2, HALF / 2, false);
    { int d = 0; for (int i = 0; i < HALF; i++) if (cap[i] != cap2[i]) d++; printf("\nTWO: voices 1 and 3 differ at %d of 256 samples\n", d); }

    printf("\nADPCM: a one-block sample with the end flag, after 300 samples: ENDX bit 1, envelope, flips of 256:\n");
    for (int k = 0; k < 2; k++) {
        allOff(); setCnt(15, 3);
        voice(1, 0x1000, 0x1100, HOLD_A, HOLD_S);
        if (k) write16(SPU(0x194), 0x0002);
        write16(SPU(0x188), 0x0002);
        idle(300u * 768u);
        const uint16_t endx = read16(SPU(0x19C)), env = read16(SPU(0x01C));
        capture(1, cap, 2);
        printf(" %s: ENDX %d env %04x flips %d\n", k ? "noise" : "ADPCM", (endx >> 1) & 1, env, flips(cap + HALF, HALF));
    }

    printf("\nPITCH: voice 1's pitch: flips of 1024 at shift 12 step 0:\n");
    static const uint16_t PS[3] = { 0x0000, 0x1000, 0x4000 };
    for (int k = 0; k < 3; k++) {
        allOff(); setCnt(12, 0);
        voice(1, PS[k], 0x1000, HOLD_A, HOLD_S);
        write16(SPU(0x194), 0x0002);
        write16(SPU(0x188), 0x0002);
        idle(SETTLE);
        capture(1, cap, 4);
        printf(" pitch %04x: flips %d\n", PS[k], flips(cap, HALF * 4));
    }

    printf("\nNON: D, the write's measured cycles after the edge, the ring index of the first sample off DC, two runs:\n");
    for (int k = 0; k < 12; k++) {
        int at[2], first[2];
        for (int r = 0; r < 2; r++) {
            allOff(); setCnt(15, 3);
            voice(1, 0x1000, 0x1000, HOLD_A, HOLD_S);
            write16(SPU(0x188), 0x0002);
            idle(SETTLE);
            edge(0);
            const uint32_t t0 = now(), D = (uint32_t)k * 64;
            while (now() - t0 < D) {}
            const uint32_t tw = now();
            write16(SPU(0x194), 0x0002);
            at[r] = (int)(tw - t0);
            idle(150u * 768u);
            spuDma(0x0800, (uint32_t*)cap, HALF / 2, false);
            int f = -1;
            for (int i = 0; i < HALF; i++) { const int v = (int16_t)cap[i]; if (v < 0x3000 || v > 0x4800) { f = i; break; } }
            first[r] = f;
        }
        printf(" D %3d: %3d %3d  first %3d %3d\n", k * 64, at[0], at[1], first[0], first[1]);
    }

    printf("\nRESET: the sign bits of 256 samples at shift 15 step 3:\n");
    for (int k = 0; k < 5; k++) {
        allOff();
        if (k < 3) { cnt = 0; write16(SPU(0x1AA), 0); idle(100000); }
        setCnt(15, 3);
        voice(1, 0x1000, 0x1000, HOLD_A, HOLD_S);
        write16(SPU(0x194), 0x0002);
        write16(SPU(0x188), 0x0002);
        idle(SETTLE);
        capture(1, cap, 2);
        printf(" %s:\n", k < 3 ? "after SPUCNT 0" : "without");
        hexBits(cap, 256);
        for (int i = 0; i < 16; i++) printf("%04x%s", cap[i], i == 15 ? "\n" : " ");
    }
    allOff();

    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("\nspu/spu-noise: Done.\n");
    for (;;) VSync(0);
}
