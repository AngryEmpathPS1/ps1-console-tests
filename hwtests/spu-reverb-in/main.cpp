/*
 * spu/spu-reverb-in - the reverb's INPUT stage alone: what reaches the work area from a voice, the decimation FIR's taps
 * one by one (2026-10-06).
 *
 * WHY. spu-reverb, fixed, read Studio Small's work area after an impulse on all three consoles: the SCPH-5501 and 7501
 * byte for byte alike (1,033 non-zero halfwords, 533f44a7), the 9001 the same values a reverb step later. This core
 * differs in VALUE: its peaks sit on one 22,050 Hz sample where the consoles' split across two (1309h / 1873h against
 * 1EAAh / 0964h), "as if the impulse reached the reverb half a reverb sample later". Studio Small runs the whole unit
 * (the input FIR, the IIR reflections, the comb, two all-passes), so that dump cannot say which stage, or whether the
 * stages are right and only the 44.1 kHz sample on which each half of the unit runs differs (M234: left on one cycle,
 * right on the next). This test turns everything off but the input: vWALL, the combs and the all-passes 0, vIIR 7FFFh
 * (the reflection passes its input through), vLIN = vRIN = 8000h (the presets' value: -input). Then each step writes
 * (about) minus the FIR's output into the same-side and different-side lines, and the work area is a RECORDING of the
 * input stage: an impulse plays back the FIR's taps - its centre alone (4000h, the 22,050 Hz sample that falls on the
 * impulse) or the other half's ringing (2806h either side, -0B90h, ...: two equal peaks) - depending on which 44.1 kHz
 * sample the half that took it runs on.
 *   The impulse is on VOICE 1, whose output the SPU also records into sound RAM (0800h-0BFFh, a 512-sample ring
 * written in step with SPUSTAT bit 11), so one run shows both clocks: where in the ring the voice played the impulse,
 * and on which reverb step the FIR's response lies.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock; stable sound RAM reads (220931E1h) and uploads settled
 * 100,000 cycles (spu-dma). A 16 KB work area at 7C000h; the four reflection lines 2,048 halfwords apart (mLSAME 0100h,
 * mRSAME 0300h, mLDIFF 0500h, mRDIFF 0700h), the all-pass lines between them (they write 0), every read-only tap at
 * 0040h. Voice 1 into the reverb (EON bit 1), both volumes 3FFFh, 44.1 kHz, linear attack at the fastest rate. The CPU
 * waits for SPUSTAT bit 11 to change (a sample boundary it can see, ring position 0 or 256), writes mBASE (which also
 * sets the buffer address) and KON at once, and turns the master enable off 200.5 reverb steps later (1,536 cycles a
 * step); then voice 1's ring is read out first (before the voice writes over it), then the area.
 *   IMPULSE 0  block 0 silence, block 1 = 7000h at its sample 0 (the voice's sample 28), block 2 silence looping.
 *   IMPULSE 1  the same with the impulse at block 1's sample 1, one 44.1 kHz sample later: each half of the unit should
 *              swap what it shows.
 *   DC         one block of 4000h looping from KON: the step response at both phases and the value each line settles on.
 *
 * READ IT LIKE THIS. Per run: the ring's non-zero samples with their positions (the voice's own output, and which half
 * bit 11 had at the sync), and each line's non-zero values with their step numbers. On a console that matches this core
 * value for value, the input stage is this core's and spu-reverb's difference lies after it; one whose LEFT and RIGHT
 * responses are swapped, or whose response sits a step over, says which 44.1 kHz sample each half takes; one with other
 * values says the FIR (or vLIN's multiply) is not this one.
 *
 * THE RULE: run here first: `node hwtests/run.js spu-reverb-in --frames=2000`.
 * HERE AT v0.369: the voice plays the impulse as four samples (ffff 10a5 4e7b 106d - the Gaussian interpolation), 32
 * samples after KON (33 for IMPULSE 1). IMPULSE 0: the LEFT lines take it on one 22,050 Hz sample (ce6c at step 27,
 * fc49 / fc5e beside it, the FIR's centre on the middle of the four) and the RIGHT lines split it (df25 / df40 at steps
 * 26 / 27, the other half of the taps); IMPULSE 1 swaps them exactly (left df25 / df40 at 27 / 28, right ce6c at 27). The
 * same side and the other side alike. DC: the voice's first samples 1be4 37c8 3fbf, then 1fdf 3bc3 and 3fbf on - the
 * attack's three steps, ONE DECAY STEP to half before the sustain level (max) is checked, the sustain's rise back - and
 * every line settles on c042h (-16,318) by step 26. Nothing written outside the four lines.
 * THE CONSOLES (2026-10-06): the 9001's voice at this core's place, the 7501's and 5501's a sample earlier; their
 * lines the other way round (left the split response, right the single peak), values often one higher (df26 / df41,
 * ce6d), DC settling at c049h. AT v0.374 (M374, SPU_REVERB_RULES): every line value and step of the SCPH-9001's
 * run 2, the DC step response included (c049h); the 7501's and 5501's the same values a step earlier.
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
/* DMA4 between RAM and sound RAM, `words` words in 16-word blocks; an upload settled 100,000 cycles before the stop
 * (spu-dma: the FIFO's rest is lost otherwise). */
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

/* The reverb registers (1F801DC0h-1F801DFEh, psx-spx's order): the input stage alone. */
static const uint16_t INPUT_ONLY[32] = {
    0x0040, 0x0040,                                 /* dAPF1, dAPF2: the all-passes read where nothing writes */
    0x7FFF,                                         /* vIIR: the reflection passes its input */
    0x0000, 0x0000, 0x0000, 0x0000,                 /* vCOMB1-4 */
    0x0000,                                         /* vWALL */
    0x0000, 0x0000,                                 /* vAPF1, vAPF2 */
    0x0100, 0x0300,                                 /* mLSAME, mRSAME */
    0x0040, 0x0040, 0x0040, 0x0040,                 /* mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2 */
    0x0040, 0x0040,                                 /* dLSAME, dRSAME */
    0x0500, 0x0700,                                 /* mLDIFF, mRDIFF */
    0x0040, 0x0040, 0x0040, 0x0040,                 /* mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4 */
    0x0040, 0x0040,                                 /* dLDIFF, dRDIFF */
    0x0080, 0x0280, 0x0480, 0x0680,                 /* mLAPF1, mRAPF1, mLAPF2, mRAPF2 */
    0x8000, 0x8000,                                 /* vLIN, vRIN: -input, as the presets */
};
#define WA_SIZE 0x4000u
#define WA_BASE (0x80000u - WA_SIZE)                /* 7C000h */
#define WA_WORDS (WA_SIZE / 4)
#define STEPS 200

static uint32_t area[WA_WORDS] __attribute__((aligned(16)));
static uint32_t blocks[16] __attribute__((aligned(16)));
static uint16_t ring[512] __attribute__((aligned(16)));
static int syncHalf;

static void run() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);   /* KOFF every voice */
    idle(200000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);                     /* SPU on, unmuted, reverb master off */
    write16(SPU(0x1AC), 0x0004);
    for (int i = 0; i < (int)WA_WORDS; i++) area[i] = 0;
    spuDma(WA_BASE, area, WA_WORDS, true);                      /* the work area zeroed */
    for (int i = 0; i < 256; i++) area[i] = 0;
    spuDma(0x0800, area, 256, true);                            /* and voice 1's ring */
    spuDma(0x1000, blocks, 16, true);                           /* the sample */
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * i), INPUT_ONLY[i]);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);             /* main volume 0: nothing heard */
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);             /* reverb output volume 0 */
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);             /* no FM */
    write16(SPU(0x198), 0x0002); write16(SPU(0x19A), 0);        /* EON: voice 1 into the reverb */
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);   /* voice 1: volume */
    write16(SPU(0x014), 0x1000);                                /* 44.1 kHz */
    write16(SPU(0x016), 0x1000 / 8);                            /* start */
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);   /* linear attack at the fastest rate; sustain held */
    cnt |= 0x0080; write16(SPU(0x1AA), cnt);                    /* reverb master enable */
    const uint16_t b0 = read16(SPU(0x1AE)) & 0x0800;
    while ((read16(SPU(0x1AE)) & 0x0800) == b0) now();
    const uint32_t t0 = now();
    write16(SPU(0x1A2), (uint16_t)(WA_BASE / 8));               /* mBASE, and the buffer address */
    write16(SPU(0x188), 0x0002);                                /* KON voice 1 */
    syncHalf = b0 ? 0 : 1;                                      /* bit 11 now: the ring half being written */
    while (now() - t0 < STEPS * 1536u + 768u) {}
    cnt &= (uint16_t)~0x0080; write16(SPU(0x1AA), cnt);          /* no more reverb writes */
    spuDma(0x0800, (uint32_t*)ring, 256, false);                /* the voice's ring first: it is still being written */
    write16(SPU(0x18C), 0x0002);                                /* KOFF */
    spuDma(WA_BASE, area, WA_WORDS, false);
}

/* Prints the non-zero entries of v[0..n) as "position:value", a run of three or more equal values as "first-last:value". */
static void runs(const uint16_t* v, int n, bool hex3) {
    int shown = 0;
    for (int i = 0; i < n; ) {
        if (!v[i]) { i++; continue; }
        int j = i; while (j + 1 < n && v[j + 1] == v[i]) j++;
        if (!(shown % 8)) printf("\n   ");
        if (j - i >= 2) { if (hex3) printf(" %03x-%03x:%04x", i, j, v[i]); else printf(" %3d-%3d:%04x", i, j, v[i]); i = j + 1; }
        else { if (hex3) printf(" %03x:%04x", i, v[i]); else printf(" %3d:%04x", i, v[i]); i++; }
        shown++;
    }
    if (!shown) printf(" none");
    printf("\n");
}
static uint16_t lineBuf[STEPS + 8];
static void report(const char* name) {
    printf("\n%s (sync: bit 11 turned %d, the ring writing %s):\n", name, syncHalf, syncHalf ? "100h-1FFh" : "000h-0FFh");
    printf("  voice 1's ring (halfword:value):");
    runs(ring, 512, true);
    static const char* const LINE[4] = { "mLSAME (left, same side)", "mRSAME (right, same side)", "mLDIFF (left, other side)", "mRDIFF (right, other side)" };
    static const uint16_t OFF[4] = { 0x0100, 0x0300, 0x0500, 0x0700 };
    const uint16_t* h = (const uint16_t*)area;
    for (int l = 0; l < 4; l++) {
        const int base = OFF[l] * 4;                            /* halfwords */
        for (int k = 0; k < STEPS + 8; k++) lineBuf[k] = h[(base + k) & (WA_SIZE / 2 - 1)];
        printf("  %s (step:value):", LINE[l]);
        runs(lineBuf, STEPS + 8, false);
    }
    /* Anything written anywhere else (the all-pass lines write 0 here): its count and the first few. */
    int other = 0;
    for (int i = 0; i < (int)(WA_SIZE / 2); i++) {
        bool inLine = false;
        for (int l = 0; l < 4; l++) { const int base = OFF[l] * 4; if (i >= base && i < base + STEPS + 8) inLine = true; }
        if (!inLine && h[i]) { if (other < 8) printf("  ELSEWHERE %05x: %04x\n", (unsigned)(WA_BASE + i * 2), h[i]); other++; }
    }
    printf("  non-zero halfwords outside the four lines: %d\n", other);
}

int main() {
    printf("\nspu/spu-reverb-in\n");
    printf("The reverb's input stage alone (vIIR 7FFFh, vLIN = vRIN = 8000h, everything else off): an impulse at two phases\n");
    printf("and a DC step, recorded in the work area, with voice 1's own output ring. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);                  /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);                            /* stable sound RAM reads (spu-dma) */

    uint8_t* b = (uint8_t*)blocks;
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < 64; i++) b[i] = 0;
        b[18 + p / 2] = (p & 1) ? 0x70 : 0x07;                  /* block 1, sample p: nibble 7 = 7000h */
        b[33] = 0x07;                                           /* block 2: silence looping */
        run();
        report(p ? "IMPULSE 1 (block 1, sample 1)" : "IMPULSE 0 (block 1, sample 0)");
    }
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;                   /* 4000h, looping on itself */
    run();
    report("DC 4000h from KON");

    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("\nspu/spu-reverb-in: Done.\n");
    for (;;) VSync(0);
}
