/*
 * spu/spu-regs - what key-on does to the repeat address, the envelope's rate
 * law over a grid of settings, and what SPUCNT bit 15 stops.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, the SPU rows).
 *   KEY-ON AND THE REPEAT ADDRESS. psx-spx: Key On "copies Voice Start Address
 *     to Voice Repeat Address". This core leaves the repeat address alone at
 *     key-on - FF7 writes it (start + 32) just before every key-on, and copied
 *     over, its loops ran 56 samples long and every note played flat. DuckStation reads it the same way. And a repeat address
 *     written after key-on outranks a later block's loop-start flag here
 *     (DuckStation's ignore_loop_address); nothing measured either.
 *   THE ENVELOPE. The rate law (a step of 7..4 shifted by 11 - shift, or one
 *     tick in 2^(shift - 11) samples), exponential increase slowing at 6000h,
 *     exponential decrease scaled by the level: psx-spx's formulas and
 *     DuckStation's code. Silicon checks one attack (Redux's ring goldens).
 *   SPUCNT BIT 15. psx-spx: while it is 0 "all voices are Keyed Off, writes to
 *     Key On are ignored, and all voices are forced to ADSR volume=0000h". Here
 *     it only silences the output.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Samples are 768
 * cycles (44.1 kHz).
 *   REPEAT: voice 1, whose output after its envelope the SPU captures at 0800h
 *     (psx-spx), plays DC blocks at 1000h, 2000h and 3000h, and the captured
 *     level shows which block it looped to. A: start = a one-shot block (end
 *     and repeat flags, no loop start), the repeat address written to a 3000h
 *     loop before key-on - does the voice go there (this core) or back to its
 *     start (psx-spx)? Also 1F801C0Eh read back after key-on. B: two plain
 *     blocks, a loop-start block and an end block at 2000h, with the repeat
 *     address written to the 3000h loop 4 and 20 samples after key-on: does
 *     the later loop-start flag win? C: B without the write, the control.
 *   ENVELOPE: voice 0 keyed on for each setting, ENVX (1F801C0Ch) read every
 *     256 cycles; the samples from the key-on write at which it first reaches
 *     each of 0800h, 1000h, 2000h, 4000h, 6000h, 7000h, 7FFFh (rising) or falls
 *     to 6000h, 4000h, 2000h, 1000h, 0800h, 0 (falling). Attack linear and
 *     exponential at rates 00h-38h; decay at shifts 0-12 to sustain level 0
 *     (the sustain then a linear decrease at rate 7Fh, which never ticks);
 *     sustain linear up and down and exponential down; release linear and
 *     exponential after a key-off from 7FFFh.
 *   SPUCNT.15: voice 0 held at 7FFFh; bit 15 cleared for 20 ms, ENVX read at
 *     once and after 20 ms, a key-on written meanwhile; bit 15 set again,
 *     ENVX after 5 ms.
 *
 * READ IT LIKE THIS. Every number is this emulator's in its header; a
 * console's that differ are the finding. THE REPEAT ROWS OF CONSOLE DAY (2026-10-05/06) ARE VOID: the upload stopped as
 * DMA4 ended and lost its tail (spuDma's note); rerun since 2026-10-08. The ENVELOPE rows read ENVX, not the data, and
 * stand: on the SCPH-5501, 7501 and 9001 alike ENVX first moves ~5 samples after the key-on write (rate 00h reaches
 * 0800h at 6, 7FFFh at 8), here 8 - every key-on row 2-3 samples later than the consoles', no release row.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js spu-regs --frames=6000
 * --quiet=5000`, 36 s here):
 *   REPEAT   A: level 2FCEh, the written loop, and 1F801C0Eh still reads the
 *            written 0202h after key-on (psx-spx's rule would read 1000h and
 *            0200h). B written 4 samples after key-on: 1FDEh - the loop-start
 *            flag wins, because the key-on itself lands 7 samples after its
 *            write here (M186) and clears the write's protection; written at 20
 *            samples: 2FCEh, the write outranks the later flag. C: 1FDEh.
 *   ENVELOPE ENVX first moves 8 samples after the key-on write. Linear attack
 *            at rate 00h reaches 7FFFh at sample 10, 20h at 593, 30h at 9,369;
 *            exponential 20h at 1,031 (the same as linear to 6000h, then
 *            slower). Decay shift 8 falls to 0800h at 1,371. Release linear
 *            shift 8 reaches 0 at 553, exponential at 2,468. Every sustain row
 *            reads 6000h and 4000h at sample 11-12: with sustain level F
 *            (8000h, already met) the decay still takes one step from 7FFFh to
 *            3FFFh before the sustain phase begins here. A console without that
 *            step reads those columns later.
 *   SPUCNT.15 ENVX 7FFFh throughout, the key-on written meanwhile honoured:
 *            bit 15 only silences the output here.
 * THE CONSOLES (all three, six runs): SPUCNT.15 7FFFh at once, 0000h 20 ms later, 0000h after - the voices turned off,
 * the key-on lost. HERE SINCE v0.377.44: the same row.
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

static uint16_t cnt = 0xC000;
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
    /* An upload's last FIFO-full is still on its way to sound RAM when DMA4 reports done: Stop at once lands 4-5 of 32
     * halfwords (hwtests/spu-dma TAIL on all three consoles, v0.372's model too). Until 2026-10-08 this stopped at once,
     * and the REPEAT blocks past block 3 never arrived - on the consoles, and here since M372 (B and C read 0000) - so
     * every REPEAT row of console day is of whatever sound RAM held there. spu-dma's fix: a settle first. */
    if (toSpu) idle(100000);
    spuMode(0);
}

/* Blocks at 1000h: 0 DC 1000h one-shot (end+repeat), 1 DC 3000h loop (start+end+repeat), 2 and 3 DC 1000h plain,
 * 4 DC 2000h loop start, 5 DC 2000h end+repeat, 6 DC 4000h loop (start+end+repeat) for the envelope. */
uint32_t blocks[32] __attribute__((aligned(16)));
static void block(int n, uint8_t flags, uint8_t nibble) {
    uint8_t* b = (uint8_t*)blocks + 16 * n;
    b[0] = 0x00; b[1] = flags;
    for (int i = 2; i < 16; i++) b[i] = (uint8_t)(nibble | (nibble << 4));
}
#define BLK(n) ((0x1000 + 16 * (n)) / 8)

uint32_t cap[256] __attribute__((aligned(16)));                 /* the voice-1 capture, 800h..BFFh */

static void voiceSetup(int v, uint16_t start, uint16_t adsrLo, uint16_t adsrHi) {
    const uint32_t o = 0x10 * (uint32_t)v;
    write16(SPU(o + 0x0), 0x3FFF); write16(SPU(o + 0x2), 0x3FFF);
    write16(SPU(o + 0x4), 0x1000);
    write16(SPU(o + 0x6), start);
    write16(SPU(o + 0x8), adsrLo); write16(SPU(o + 0xA), adsrHi);
}
static void keyOff(uint32_t mask) { write16(SPU(0x18C), (uint16_t)mask); write16(SPU(0x18E), (uint16_t)(mask >> 16)); }
static void keyOn(uint32_t mask) { write16(SPU(0x188), (uint16_t)mask); write16(SPU(0x18A), (uint16_t)(mask >> 16)); }
static void silence(uint32_t mask) {                            /* key off, and wait out a fast release */
    for (int v = 0; v < 24; v++) if (mask & (1u << v)) write16(SPU(0x10 * v + 0xA), 0x0000);
    keyOff(mask); idle(400000);
}

/* The captured level: the median-ish of 16 samples from the voice-1 capture's first half. */
static int16_t captured() {
    spuDma(0x800, cap, 256, false);
    const int16_t* h = (const int16_t*)cap;
    int16_t v[16];
    for (int i = 0; i < 16; i++) v[i] = h[16 + i * 24];
    for (int i = 1; i < 16; i++) for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { int16_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return v[8];
}

/* ENVELOPE: key on, read ENVX every 256 cycles for up to `limit` cycles; the sample (from the key-on write) at which
 * it first reached each threshold. `falling` looks downward; `offAt` > 0 keys off after that many cycles. */
static const uint16_t UP[7] = { 0x0800, 0x1000, 0x2000, 0x4000, 0x6000, 0x7000, 0x7FFF };
static const uint16_t DOWN[6] = { 0x6000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0000 };
static void envRun(uint16_t lo, uint16_t hi, bool falling, uint32_t offAt, uint32_t limit) {
    silence(1);
    voiceSetup(0, BLK(6), lo, hi);
    const int nt = falling ? 6 : 7;
    const uint16_t* th = falling ? DOWN : UP;
    int32_t at[7]; for (int i = 0; i < 7; i++) at[i] = -1;
    int k = 0;
    bool off = false;
    uint16_t peak = 0;
    const uint32_t t0 = now();
    keyOn(1);
    for (;;) {
        const uint32_t t = now() - t0;
        if (t > limit || k >= nt) break;
        if (offAt && !off && t >= offAt) { keyOff(1); off = true; }
        const uint16_t e = read16(SPU(0x00C));
        if (e > peak) peak = e;
        if (falling ? (off || !offAt) && (int16_t)e <= (int16_t)th[k] && peak >= 0x7000 : e >= th[k]) { at[k] = (int32_t)(t / 768); k++; }
        while (now() - t0 < t + 256) {}
    }
    for (int i = 0; i < nt; i++) { if (at[i] < 0) printf("      -"); else printf(" %6d", (int)at[i]); }
    printf("\n");
}

int main() {
    printf("\nspu/spu-regs\n");
    printf("Key-on and the repeat address, the envelope's rates, SPUCNT bit 15. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    keyOff(0xFFFFFF); idle(200000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x180), 0x3FFF); write16(SPU(0x182), 0x3FFF);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);             /* no FM */
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);             /* no noise */
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);             /* no reverb */
    for (int i = 0; i < 32; i++) blocks[i] = 0;
    block(0, 0x03, 1); block(1, 0x07, 3); block(2, 0x00, 1); block(3, 0x00, 1); block(4, 0x04, 2); block(5, 0x03, 2);
    block(6, 0x07, 4);
    spuDma(0x1000, blocks, 32, true);

    printf("REPEAT (voice 1's captured level: 1000h its start block, 2000h its own loop, 3000h the hand-written loop):\n");
    /* A */
    silence(2);
    voiceSetup(1, BLK(0), 0x000F, 0x0000);
    write16(SPU(0x01E), BLK(1));
    keyOn(2);
    idle(768 * 2);
    const uint16_t readBack = read16(SPU(0x01E));
    idle(768 * 800);
    printf("  A  repeat written before key-on, a one-shot start: level %04x; 1F801C0Eh after key-on %04x (start %04x, written %04x)\n",
           (uint16_t)captured(), readBack, BLK(0), BLK(1));
    /* B: written 4 samples after key-on (inside the key-on's own delay here) and 20 samples after (before the
     * loop-start block, the third, is decoded) */
    static const int BW[2] = { 4, 20 };
    for (int b = 0; b < 2; b++) {
        silence(2);
        voiceSetup(1, BLK(2), 0x000F, 0x0000);
        keyOn(2);
        idle(768 * (uint32_t)BW[b]);
        write16(SPU(0x01E), BLK(1));
        idle(768 * 800);
        printf("  B  repeat written %2d samples after key-on, a loop-start block after it: level %04x, 1F801C0Eh %04x (loop start %04x)\n",
               BW[b], (uint16_t)captured(), read16(SPU(0x01E)), BLK(4));
    }
    /* C */
    silence(2);
    voiceSetup(1, BLK(2), 0x000F, 0x0000);
    keyOn(2);
    idle(768 * 800);
    printf("  C  the same with no write (control): level %04x, 1F801C0Eh %04x\n", (uint16_t)captured(), read16(SPU(0x01E)));
    silence(2);

    printf("\nENVELOPE (voice 0; the sample, from the key-on write, at which ENVX first reaches each level):\n");
    printf("  %-26s   0800   1000   2000   4000   6000   7000   7FFF\n", "attack");
    static const uint8_t AR[] = { 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x2C, 0x30, 0x38 };
    for (int e = 0; e < 2; e++)
        for (unsigned i = 0; i < sizeof(AR); i++) {
            const uint16_t lo = (uint16_t)((e ? 0x8000 : 0) | (AR[i] << 8) | 0x000F);
            printf("  %s rate %02x         ", e ? "exponential" : "linear     ", AR[i]);
            envRun(lo, 0x0000, false, 0, 33868800u / 2);
        }
    printf("  %-26s   6000   4000   2000   1000   0800   0000\n", "decay to sustain level 0");
    for (int d = 0; d <= 12; d += 2) {                        /* sustain: linear down at rate 7Fh, which holds */
        printf("  decay shift %2d            ", d);
        envRun((uint16_t)((d << 4) | 0x0000), 0x5FC0, true, 0, 33868800u / 2);
    }
    printf("  %-26s   6000   4000   2000   1000   0800   0000\n", "sustain (level F, then)");
    static const uint8_t SR[] = { 0x20, 0x30, 0x40 };
    for (unsigned i = 0; i < sizeof(SR); i++) {
        printf("  linear down rate %02x      ", SR[i]);
        envRun(0x000F, (uint16_t)(0x4000 | (SR[i] << 6)), true, 0, 33868800u / 2);
        printf("  exponential down rate %02x ", SR[i]);
        envRun(0x000F, (uint16_t)(0xC000 | (SR[i] << 6)), true, 0, 33868800u / 2);
    }
    printf("  %-26s   6000   4000   2000   1000   0800   0000\n", "release from 7FFF");
    static const uint8_t RR[] = { 0, 4, 8, 12, 16 };
    for (unsigned i = 0; i < sizeof(RR); i++) {
        printf("  linear shift %2d           ", RR[i]);
        envRun(0x000F, (uint16_t)(RR[i]), true, 768 * 40, 33868800u / 2);
        printf("  exponential shift %2d      ", RR[i]);
        envRun(0x000F, (uint16_t)(0x0020 | RR[i]), true, 768 * 40, 33868800u / 2);
    }
    silence(1);

    printf("\nSPUCNT.15 (voice 0 held at the top):\n");
    voiceSetup(0, BLK(6), 0x000F, 0x0000);
    keyOn(1);
    idle(768 * 100);
    const uint16_t before = read16(SPU(0x00C));
    write16(SPU(0x1AA), (uint16_t)(cnt & 0x7FFF));
    const uint16_t at0 = read16(SPU(0x00C));
    idle(768 * 10);
    keyOn(1);                                                   /* ignored while bit 15 is clear, psx-spx says */
    idle(768 * 870);
    const uint16_t at20 = read16(SPU(0x00C));
    write16(SPU(0x1AA), cnt);
    idle(768 * 220);
    const uint16_t after = read16(SPU(0x00C));
    printf("  ENVX %04x before; bit 15 cleared: %04x at once, %04x 20 ms later (a key-on written meanwhile); set again: %04x 5 ms later\n",
           before, at0, at20, after);
    silence(1);

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-regs: Done.\n");
    for (;;) VSync(0);
}
