/*
 * spu/spu-latch - when, inside a 44.1 kHz sample, does the SPU take a register write? (2026-10-07)
 *
 * WHY (STATUS M374, KNOWN-OPEN). The three consoles agree on the reverb's arithmetic but not on its place: against the
 * tests' sync (the CPU sees SPUSTAT bit 11 change), the SCPH-7501's and 5501's voice rings a sample EARLIER than the
 * 9001's in spu-reverb-in (KON written 70-74 cycles after the edge), and their reverb areas a step earlier; while in
 * spu-lead (KON 72-143 cycles after the falling edge) the 9001's onset is the earlier one (ring 8 against 9). One latch
 * point per board for register writes - a moment in each sample before which a write counts in that sample - would give
 * both, and neither test can place it: each writes at one delay. This core keeps the 9001's alignment.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, unwrapped; stable sound RAM reads (220931E1h, spu-dma).
 * Voice 1 plays a looping DC block (4000h) at 44.1 kHz with the fastest linear attack, so the first non-zero sample of
 * its capture ring (0800h-0BFFh, 512 samples written in step with SPUSTAT bit 11) is its onset.
 *   KON    the CPU waits for bit 11 to change (to 0: the ring back at its first half; to 1: at its second), then D
 *          cycles more, then writes KON voice 1. D = 0..768 in steps of 16 (a sample is 768), each twice. Printed per D:
 *          the cycles from the edge to the write as measured, and the onset's ring index counted from the half the edge
 *          began.
 *   REVERB the input stage alone (as spu-reverb-in: vIIR 7FFFh, vLIN = vRIN = 8000h, everything else 0) with voice 1
 *          into it, keyed on long before. At the falling edge mBASE is written (which also restarts the buffer address),
 *          then D cycles later the master enable; 60 reverb steps later it is turned off and the work area read: per D
 *          the step of the first write in each of the four lines (mLSAME, mRSAME, mLDIFF, mRDIFF).
 *
 * READ IT LIKE THIS. A write taken at a fixed point inside each sample moves its result by exactly one sample where D
 * crosses that point: the KON rows' onset steps up by 1 at one D (per edge direction), and that D is the board's latch
 * point against the edge the CPU saw. A board whose step sits at another D, or whose rising and falling edges disagree,
 * says where its bit 11 changes inside the sample. The REVERB rows step by one reverb step where the enable's latch
 * meets a step boundary. Every row printed twice: they must repeat (the poll loop's own spread is ~30 cycles).
 *
 * ON THIS CORE AT v0.377.14 (2026-10-07): KON - the write lands 104 cycles after the edge at the soonest (the poll and
 * the store), then in steps of ~30 (the wait loop); the onset reads ring index 9 up to a write 678 cycles after the edge
 * and 10 from 708, the same for both edge directions and both runs: this core takes a KON in the sample it is written in
 * up to ~690 cycles after the CPU sees bit 11 change (its sample boundary ~78 cycles before that). REVERB - the left
 * lines' first step 1 at every D, the right lines' 0 up to an enable written 649 cycles after the falling edge and 1 from
 * 679. A console's steps elsewhere place its latch against the same edge; both edges and both runs must agree.
 * THE RULE: run here first: `node hwtests/run.js spu-latch --frames=6000 --quiet=3000`.
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

/* The reverb registers (1F801DC0h-1F801DFEh): the input stage alone, as spu-reverb-in. */
static const uint16_t INPUT_ONLY[32] = {
    0x0040, 0x0040, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0100, 0x0300, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0500, 0x0700,
    0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0080, 0x0280, 0x0480, 0x0680,
    0x8000, 0x8000,
};
#define WA_SIZE 0x4000u
#define WA_BASE (0x80000u - WA_SIZE)                /* 7C000h */
#define WA_WORDS (WA_SIZE / 4)
#define REV_STEPS 60

static uint32_t area[WA_WORDS] __attribute__((aligned(16)));
static uint32_t blocks[16] __attribute__((aligned(16)));
static uint16_t ring[512] __attribute__((aligned(16)));

static void voiceSetup() {
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);             /* main volume 0: nothing heard */
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);             /* reverb output volume 0 */
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);             /* no FM */
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);   /* voice 1: volume */
    write16(SPU(0x014), 0x1000);                                /* 44.1 kHz */
    write16(SPU(0x016), 0x1000 / 8);                            /* start */
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);   /* linear attack at the fastest rate; sustain held */
}
static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);   /* KOFF every voice */
    idle(100000);
}
/* SPUSTAT bit 11 becoming `want`: returns the time it was seen. */
static uint32_t edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    while ((read16(SPU(0x1AE)) & 0x0800) == w) now();          /* first leave it, so the change is fresh */
    while ((read16(SPU(0x1AE)) & 0x0800) != w) now();
    return now();
}

#define KON_D 49                                    /* D = 0, 16 ... 768 */
static uint16_t konOnset[2][2][KON_D]; static uint16_t konAt[2][2][KON_D];

static void konRun(int dir, int rep, int k) {
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);                     /* SPU on, unmuted, reverb master off */
    for (int i = 0; i < 256; i++) ((uint32_t*)ring)[i] = 0;
    spuDma(0x0800, (uint32_t*)ring, 256, true);                 /* voice 1's ring zeroed */
    voiceSetup();
    const uint32_t D = (uint32_t)k * 16;
    const uint32_t t0 = edge(dir);
    while (now() - t0 < D) {}
    const uint32_t tw = now();
    write16(SPU(0x188), 0x0002);                                /* KON voice 1 */
    konAt[dir][rep][k] = (uint16_t)(tw - t0);
    idle(150u * 768u);                                          /* the onset well inside the half after the edge */
    spuDma(0x0800, (uint32_t*)ring, 256, false);
    write16(SPU(0x18C), 0x0002);                                /* KOFF */
    const int base = dir ? 256 : 0;                             /* the half the edge began */
    int onset = 0xFFFF;
    for (int i = 0; i < 256; i++) if (ring[(base + i) & 511]) { onset = i; break; }
    konOnset[dir][rep][k] = (uint16_t)onset;
}

#define REV_D 49
static int16_t revFirst[2][REV_D][4]; static uint16_t revAt[2][REV_D];

static void revRun(int rep, int k) {
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    for (int i = 0; i < (int)WA_WORDS; i++) area[i] = 0;
    spuDma(WA_BASE, area, WA_WORDS, true);                      /* the work area zeroed */
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * i), INPUT_ONLY[i]);
    voiceSetup();
    write16(SPU(0x198), 0x0002); write16(SPU(0x19A), 0);        /* EON: voice 1 into the reverb */
    write16(SPU(0x188), 0x0002);                                /* KON voice 1, long before */
    idle(40u * 768u);
    const uint32_t D = (uint32_t)k * 16;
    const uint32_t t0 = edge(0);
    write16(SPU(0x1A2), (uint16_t)(WA_BASE / 8));               /* mBASE, and the buffer address */
    while (now() - t0 < D) {}
    const uint32_t tw = now();
    cnt |= 0x0080; write16(SPU(0x1AA), cnt);                    /* the reverb master enable */
    revAt[rep][k] = (uint16_t)(tw - t0);
    idle(REV_STEPS * 1536u);
    cnt &= (uint16_t)~0x0080; write16(SPU(0x1AA), cnt);
    write16(SPU(0x18C), 0x0002);
    spuDma(WA_BASE, area, WA_WORDS, false);
    static const uint16_t OFF[4] = { 0x0100, 0x0300, 0x0500, 0x0700 };
    const uint16_t* h = (const uint16_t*)area;
    for (int l = 0; l < 4; l++) {
        int first = -1;
        for (int s = 0; s < REV_STEPS + 8; s++) if (h[(OFF[l] * 4 + s) & (WA_SIZE / 2 - 1)]) { first = s; break; }
        revFirst[rep][k][l] = (int16_t)first;
    }
}

int main() {
    printf("\nspu/spu-latch\n");
    printf("When inside a 44.1 kHz sample does the SPU take a register write? KON and the reverb master enable written D\n");
    printf("cycles after SPUSTAT bit 11 changes (a sample is 768 cycles). No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);                  /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                            /* stable sound RAM reads (spu-dma) */

    write16(SPU(0x1AC), 0x0004);                                /* transfer type Normal: SPUSTAT bit 11 works only
                                                                   with 1F801DACh bit 2 or 3 set (psx-spx SPUSTAT) */
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;                                                /* flags: loop start + repeat on itself */
    for (int i = 2; i < 16; i++) b[i] = 0x44;                   /* 4000h every sample */
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 16, true);

    for (int rep = 0; rep < 2; rep++)
        for (int dir = 0; dir < 2; dir++)
            for (int k = 0; k < KON_D; k++) konRun(dir, rep, k);
    for (int rep = 0; rep < 2; rep++)
        for (int k = 0; k < REV_D; k++) revRun(rep, k);
    allOff();

    printf("\nKON: D (the delay asked) / the write's measured cycles after the edge / voice 1's onset (ring index from\n");
    printf("the half the edge began), two runs:\n");
    for (int dir = 1; dir >= 0; dir--) {
        printf(" bit 11 -> %d:\n", dir);
        for (int k = 0; k < KON_D; k++)
            printf("  D %3d: %3d %3d   onset %3d %3d\n", k * 16, konAt[dir][0][k], konAt[dir][1][k],
                   konOnset[dir][0][k], konOnset[dir][1][k]);
    }
    printf("\nREVERB: D / the enable's measured cycles after the falling edge (mBASE written at it) / each line's first\n");
    printf("written step (mLSAME mRSAME mLDIFF mRDIFF), two runs:\n");
    for (int k = 0; k < REV_D; k++)
        printf("  D %3d: %3d %3d   %3d %3d %3d %3d   %3d %3d %3d %3d\n", k * 16, revAt[0][k], revAt[1][k],
               revFirst[0][k][0], revFirst[0][k][1], revFirst[0][k][2], revFirst[0][k][3],
               revFirst[1][k][0], revFirst[1][k][1], revFirst[1][k][2], revFirst[1][k][3]);

    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("\nspu/spu-latch: Done.\n");
    for (;;) VSync(0);
}
