/*
 * spu/spu-envphase-move2 - WHICH TRANSFER CONTROL VALUES STALL THE CAPTURE RING, AND BY HOW MANY SAMPLES (2026-11-07)
 *
 * THE FIRST VERSION'S ANSWER (spu-envphase-move, the second SCPH-5501, three runs): only transfer control (1F801DACh) 0000h for 100
 * samples moved the ruler, +101 every time (34 -> 135 -> 236 -> 337); SPU off, SPUCNT 0, mute, reverb and DMA never did - the capture
 * position stops while the envelope counter runs (psx-spx: SPUSTAT bit 11 "works only if Bit2 and/or Bit3" of 1DACh are set). This
 * version asks which values stall it - bits 2/3 clear (0000h, 0002h) or the transfer TYPE (Fill 000Ch has bits 2 and 3 set) - and
 * how a stall of n samples counts (1, 10, 37, 100 at 0000h).
 *
 * The first version's notes:
 *
 * WHY. The SPU's envelope steps land on a free-running counter N (v0.377.238: a rate of period P steps when (N & (P-1)) = P/2),
 * and its phase against the capture ring is a console's own (v0.377.277: 799, 547, 803 on three consoles) - psx-spx calls the
 * key-on grid's phase "fixed for a given power-on". But on 2026-11-07 two spu-envring runs IN ONE BOOT disagreed: the SCPH-7502's
 * ruler moved 2 samples (c 48 -> 50) and the SCPH-7501's 140 (c 41 -> 181), with ~200 other tests between and no reset; the
 * SCPH-5501s' back-to-back runs never moved (STATUS "The envelope ruler moves inside a boot"). This core ties N to the capture
 * position, which nothing resets, and so predicts no move at all. Something one of those tests did moves one counter against the
 * other: an SPU stop (SPUCNT bit 15) is the first suspect - SpuInit-style code clears SPUCNT - but which counter stops, or resets,
 * decides how a game's envelopes line up after every sound-driver init.
 *
 * WHAT IT DOES. Interrupts off, no disc, sound out muted. A MEASURE: voice 1 plays a constant 4000h through a linear attack of
 * period P = 8, 16, 32 ... 512 (ADSR1 shift 14 to 20, step +7), keyed on just behind a capture-half edge exactly as spu-envring
 * does, its capture ring read back; each row gives the ring indices of its first two steps (i1, i2), and c = (P/2 - i) mod P is
 * the ruler's phase against the ring (spu-envring's attack rows: the first SCPH-5501 28, the second 32). Then, in turn, each
 * ACTION followed by a measure:
 *   none twice (the control), 2 s of nothing (drift), SPUCNT bit 15 off for 1 / 10 / 100 / 300 / 301 samples, SPUCNT 0000h for
 *   100 samples (all off, as SpuInit), the mute bit (14) off 100 samples, transfer control 0000h for 100 samples, reverb master
 *   (bit 7) on and off, a 1 KB DMA write to sound RAM, and finally SPU off 100 samples twice more (does a repeat move the same?).
 * A line per measure: the action, c from the slowest row and whether every row agrees with it, then each row's i1/i2.
 * READ IT LIKE THIS: if bit 15 off stops one counter, c moves by the off time in samples (+-1, 10, 100, 300, 301 mod 512); a
 * reset puts c at one value whatever the time; no move, this core's model.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-envphase-move2 --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define HALF 256
#define SAMPLE 768u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (toSpu) idle(4000);
    spuMode(0);
}
static void edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
}
static uint32_t dcBlock[16] __attribute__((aligned(16)));
static uint32_t junk[256] __attribute__((aligned(16)));
static uint16_t cap[HALF * 2] __attribute__((aligned(16)));

/* one attack row, spu-envring's run() exactly: the ring indices of the first two changes (-1: none) */
static void row(uint16_t a1, int* i1, int* i2) {
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);
    write16(SPU(0x10 + 0xA), 0x001F);
    idle(SAMPLE * 1100u);
    write16(SPU(0x10 + 0x4), 0x1000);
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x8), a1);
    write16(SPU(0x10 + 0xA), 0x1FC0);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    edge(1);
    write16(SPU(0x188), 0x0002);
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap, HALF / 2, false);
    edge(1);
    spuDma(0x0800, (uint32_t*)(cap + HALF), HALF / 2, false);
    *i1 = -1; *i2 = -1;
    uint16_t last = 0;
    for (int i = 0; i < HALF * 2; i++) if (cap[i] != last) { last = cap[i]; if (*i1 < 0) *i1 = i; else { *i2 = i; break; } }
}

struct Meas { const char* what; int i1[7], i2[7]; };
static Meas meas[16];
static int nm = 0;
static void measure(const char* what) {
    Meas& m = meas[nm++];
    m.what = what;
    for (int k = 0; k < 7; k++) row((uint16_t)(((14 + k) << 10) | 0x000F), &m.i1[k], &m.i2[k]);   /* linear, shift 14+k, step +7 */
}
/* transfer control at v for n samples, then back to 0004h (Normal) */
static void xfer(uint16_t v, uint32_t n) { write16(SPU(0x1AC), v); idle(n * SAMPLE); write16(SPU(0x1AC), 0x0004); idle(SAMPLE * 64u); }
static void spuOff(uint32_t samples, uint16_t to) {
    const uint16_t keep = cnt;
    write16(SPU(0x1AA), to);
    idle(samples * SAMPLE);
    cnt = keep; write16(SPU(0x1AA), cnt);
    idle(SAMPLE * 64u);
}

int main() {
    printf("\nspu/spu-envphase-move2\n");
    printf("The envelope ruler's phase c against the capture ring, measured after each action. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    for (int v = 0; v < 24; v++) { write16(SPU((uint32_t)v * 0x10), 0); write16(SPU((uint32_t)v * 0x10 + 2), 0); }
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    spuDma(0x1000, dcBlock, 16, true);

    measure("start");
    xfer(0x0000, 1);    measure("xfer 0000h 1 sample");
    xfer(0x0000, 10);   measure("xfer 0000h 10 samples");
    xfer(0x0000, 37);   measure("xfer 0000h 37 samples");
    xfer(0x0000, 100);  measure("xfer 0000h 100 samples");
    xfer(0x0002, 100);  measure("xfer 0002h (Fill, bits 2-3 clear) 100");
    xfer(0x0006, 100);  measure("xfer 0006h (Rep2) 100");
    xfer(0x0008, 100);  measure("xfer 0008h (Rep4) 100");
    xfer(0x000C, 100);  measure("xfer 000Ch (Fill, bits 2-3 set) 100");
    xfer(0x000E, 100);  measure("xfer 000Eh (Fill) 100");
    xfer(0x0001, 100);  measure("xfer 0001h (bit 0 only) 100");
    xfer(0x0000, 300);  measure("xfer 0000h 300 samples");

    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int n = 0; n < nm; n++) {
        const Meas& m = meas[n];
        /* c from each row's SECOND step where the ring holds it (a first step can hide before the voice is heard), else its first */
        int c[7]; bool agree = true;
        for (int k = 0; k < 7; k++) {
            const int P = 8 << k;
            const int i = (m.i2[k] >= 0 && m.i2[k] - m.i1[k] == P) ? m.i2[k] : m.i1[k];
            c[k] = i < 0 ? -1 : (((P / 2 - i) % P) + P) % P;
        }
        for (int k = 0; k < 6; k++) if (c[k] < 0 || c[6] < 0 || (c[6] % (8 << k)) != c[k]) agree = false;
        printf("M%-2d c %3d %s  %-34s", n, c[6], agree ? "agree   " : "DISAGREE", m.what);
        for (int k = 0; k < 7; k++) printf(" %d/%d", m.i1[k], m.i2[k]);
        printf("\n");
    }
    printf("\nspu/spu-envphase-move2: Done.\n");
    for (;;) VSync(0);
}
