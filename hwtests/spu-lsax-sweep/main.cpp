/*
 * spu/spu-lsax-sweep - when a write to a voice's repeat address (LSAX) wins over a loop-start block's flag (2026-10-30).
 *
 * WHY. spu-regs's REPEAT case B, 42 runs on seven models (findings/spu.md 28): a write 4 samples after Key On wins 28 times
 * and the loop-start flag 14, split by the run's key-on phase, and 20 samples after Key On the write always wins. So the
 * cutoff sits near 4 samples and moves with the phase, and two delays cannot say where (finding 28, Open). Three readings
 * of it: psx-spx's dev text (848ed5d, Key On latency) has the voice fetch its first block's header on each of the first
 * three samples after Key On is serviced, a write before the last fetch setting no flag, and Key On serviced on a 2-sample
 * grid. Mednafen clears the flag on every sample of its 4-sample play delay. This core clears it once, at the voice's
 * restart SPU_KON_DELAY = 4 samples after the latch, and reads 1FDEh at 4 samples in both classes.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, unwrapped. spu-regs's REPEAT set-up, its code as it was: blocks
 * at 1000h (0 DC 1000h one-shot, 1 DC 3000h loop, 2 and 3 DC 1000h plain, 4 DC 2000h loop start, 5 DC 2000h end+repeat, 6 DC
 * 4000h loop), voice 1 started at block 2, 800 samples, the level taken from its capture ring (the middle of 16 samples) and
 * 1F801C1Eh read back. What it adds is the time: each trial waits for SPUSTAT bit 11 to fall and 96 cycles more (spu-phase's
 * phase, so the class reads 8 or 9 there), writes Key On, and n samples after that write writes 0202h (block 1, 3000h) to
 * the repeat address.
 *   SWEEP    n = 0 to 24 in one-sample steps, four tries, round-robin so that a drift shows as a pattern. n = 0 is the
 *            instruction after the Key On writes.
 *   CONTROL  the same with no write, four tries: must read 1FDEh and 0208h, or the block-4 flag is not what the sweep races.
 *   CLASS    the run's key-on class, read before the sweep, half way and after, three ways, because the sweep's result is
 *            nothing until it is tied to the class:
 *              regs   spu-regs's own row: rate 00h linear attack, ENVX read every 256 cycles, the sample from the Key On write
 *                     at which it first reaches each level, with spu-regs's unsynchronised Key On. Its 0800h column is what
 *                     finding 28 split on: 6 the write won, 5 or 7 the flag won.
 *              fixed  the same row with the Key On at the sweep's own phase.
 *              onset  voice 1's first sound in its capture ring (spu-phase's reading, the ring zeroed first and read at
 *                     220931E1h, put back at once): 8 is class A, 9 class B at this phase (finding 23).
 * Nothing prints until it is all over. Every wait is bounded by Timer 2 and by a count of turns (rule 1). The transfer
 * control is only ever written 0004h, which moves no class (finding 24).
 *
 * READ IT LIKE THIS. A row is n, each try's cycles from the Key On write to this write, each try's level (2FCEh the write won,
 * the voice looped to the 3000h block; 1FDEh the flag won, it looped to block 4), and each try's 1F801C1Eh (0202h, 0208h: the
 * same answer from a register, which still works on a board whose capture has died: finding 33). CUT names the last n the
 * flag won and the first n the write won every try. The number is that first n against E, the `fixed` row's 0800h column,
 * the sample ENVX first moves at this very phase, with onset beside it. psx-spx has ENVX move four samples after the first
 * fetch, so its last fetch is E - 2 and the write wins from n = E - 2, or E - 1 where the write falls ahead of the fetch in
 * its sample. Mednafen's protection ends a sample later, at E - 1 or E; this core's at E. Over many runs, a cut that follows
 * E says the time is counted from the service, as psx-spx has it; a cut that stays put with the class moving says it is
 * not. CLASS must read alike all three times: rows that differ mean the sweep crossed a flip and is two runs. MIXED rows
 * sit on the cut: the Key On write and this write each have a poll loop's spread (spu-latch: ~30 cycles). The `regs` column
 * is there to reach finding 28, not to read the class: its Key On lands where it lands, and it can move inside a run with
 * onset standing still (on this core it reads 5, then 6). `fixed` and `onset` are the class.
 *
 * THE RULE: run here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js spu-lsax-sweep --raw --frames=4000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define COM_DELAY 0x1F801014u
#define KON_D 96u                                   /* spu-phase's: the Key On's cycles after bit 11 falls */
#define EDGE_LIMIT 2000000u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
/* Until c cycles after t0. A turn is well over a cycle, so the count ends it only if Timer 2 has stopped (rule 1). */
static void until(uint32_t t0, uint32_t c) { for (uint32_t i = 0; i <= c && now() - t0 < c; i++) {} }
static void idle(uint32_t c) { until(now(), c); }

/* From here to captured(), spu-regs's own. */
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
    if (toSpu) idle(100000);                                    /* the settle: spu-dma */
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

/* SPUSTAT bit 11 falling, then KON_D cycles: false if it never falls. */
static bool bit11(uint16_t want, uint32_t t0) {
    for (uint32_t i = 0; i < EDGE_LIMIT; i++) {
        if ((read16(SPU(0x1AE)) & 0x0800) == want) return true;
        if (now() - t0 > EDGE_LIMIT) return false;
    }
    return false;
}
static bool phase() {
    const uint32_t t0 = now();
    if (!bit11(0x0800, t0) || !bit11(0, t0)) return false;
    const uint32_t te = now();
    until(te, KON_D);
    return true;
}

/* spu-regs's envRun for the rate 00h row, rows kept instead of printed; `fixed` puts the Key On at the sweep's phase. */
static const uint16_t UP[7] = { 0x0800, 0x1000, 0x2000, 0x4000, 0x6000, 0x7000, 0x7FFF };
static void envRow(int32_t* at, bool fixed, bool* edged) {
    silence(1);
    voiceSetup(0, BLK(6), 0x000F, 0x0000);
    for (int i = 0; i < 7; i++) at[i] = -1;
    int k = 0;
    if (fixed && !phase()) *edged = false;
    const uint32_t t0 = now();
    keyOn(1);
    for (uint32_t i = 0; i < 33868800u / 2; i++) {
        const uint32_t t = now() - t0;
        if (t > 33868800u / 2 || k >= 7) break;
        const uint16_t e = read16(SPU(0x00C));
        if (e >= UP[k]) { at[k] = (int32_t)(t / 768); k++; }
        until(t0, t + 256);
    }
}
/* spu-phase's reading at the sweep's phase: voice 1's first sound in its ring, the ring zeroed and read stably. */
static int onset(bool* edged) {
    silence(2);
    for (int i = 0; i < 256; i++) cap[i] = 0;
    spuDma(0x0800, cap, 256, true);
    voiceSetup(1, BLK(6), 0x000F, 0x0000);
    if (!phase()) *edged = false;
    keyOn(2);
    idle(150u * 768u);
    const uint32_t d = read32(COM_DELAY);
    write32(COM_DELAY, 0x220931E1);
    spuDma(0x0800, cap, 256, false);
    write32(COM_DELAY, d);
    keyOff(2);
    const uint16_t* h = (const uint16_t*)cap;
    for (int i = 0; i < 256; i++) if (h[i]) return i;
    return -1;
}
struct ClassRow { int32_t regs[7], fixed[7]; int onset; };
#define CLASS_READS 3
static ClassRow classes[CLASS_READS];
static bool edged = true;
static void readClass(ClassRow& C) {
    envRow(C.regs, false, &edged);
    envRow(C.fixed, true, &edged);
    C.onset = onset(&edged);
}

#define TRIES 4
#define STEPS 25                                    /* n = 0 ... 24 samples */
static uint16_t level[STEPS + 1][TRIES], lsax[STEPS + 1][TRIES];
static uint16_t writeAt[STEPS + 1][TRIES];

/* One trial: n samples after the Key On write, the repeat address; n < 0 the control, no write. */
static void trial(int n, int r) {
    const int row = n < 0 ? STEPS : n;
    silence(2);
    voiceSetup(1, BLK(2), 0x000F, 0x0000);
    if (!phase()) edged = false;
    const uint32_t t0 = now();
    keyOn(2);
    if (n >= 0) {
        until(t0, (uint32_t)n * 768);
        writeAt[row][r] = (uint16_t)(now() - t0);
        write16(SPU(0x01E), BLK(1));
    }
    idle(768 * 800);
    level[row][r] = (uint16_t)captured();
    lsax[row][r] = read16(SPU(0x01E));
}

static void printEnv(const char* name, const int32_t* at) {
    printf("  %-22s", name);
    for (int i = 0; i < 7; i++) { if (at[i] < 0) printf("      -"); else printf(" %6d", (int)at[i]); }
    printf("\n");
}

int main() {
    printf("\nspu/spu-lsax-sweep\n");
    printf("When a write to the repeat address wins over a loop-start block's flag: written n samples after the Key On write,\n");
    printf("n = 0 to 24, the Key On at a fixed phase of SPUSTAT bit 11, tied to the run's key-on class. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    /* spu-regs's set-up but for this line, which comes after the idle there. The runner leaves 0004h, so on a console it is
     * a rewrite; anything else held through the idle would move the class before the first reading (finding 24). */
    write16(SPU(0x1AC), 0x0004);
    keyOff(0xFFFFFF); idle(200000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    write16(SPU(0x180), 0x3FFF); write16(SPU(0x182), 0x3FFF);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);             /* no FM */
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);             /* no noise */
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);             /* no reverb */
    for (int i = 0; i < 32; i++) blocks[i] = 0;
    block(0, 0x03, 1); block(1, 0x07, 3); block(2, 0x00, 1); block(3, 0x00, 1); block(4, 0x04, 2); block(5, 0x03, 2);
    block(6, 0x07, 4);
    spuDma(0x1000, blocks, 32, true);
    const uint32_t delay = read32(COM_DELAY);

    readClass(classes[0]);
    for (int r = 0; r < TRIES; r++) {
        if (r == TRIES / 2) readClass(classes[1]);
        trial(-1, r);
        for (int n = 0; n < STEPS; n++) trial(n, r);
    }
    readClass(classes[2]);
    silence(3);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("1F801014h %08x, Key On %d cycles after bit 11 falls\n", (unsigned)delay, (int)KON_D);
    printf("\nCLASS: ENVX, the sample from the Key On write at which it first reached each level (rate 00h), and the ring onset:\n");
    printf("  %-22s   0800   1000   2000   4000   6000   7000   7FFF\n", "");
    static const char* const WHEN[CLASS_READS] = { "before", "half way", "after" };
    for (int c = 0; c < CLASS_READS; c++) {
        printf(" %s the sweep: onset %d\n", WHEN[c], classes[c].onset);
        printEnv("regs (unsynchronised)", classes[c].regs);
        printEnv("fixed (the phase)", classes[c].fixed);
    }
    if (!edged) printf(" SOME TRIAL SAW NO BIT 11 EDGE: its phase is not the one named\n");

    printf("\nCONTROL no write (must read 1fde and 0208):\n ");
    for (int r = 0; r < TRIES; r++) printf(" %04x", level[STEPS][r]);
    printf("   ");
    for (int r = 0; r < TRIES; r++) printf(" %04x", lsax[STEPS][r]);
    printf("\n\nSWEEP n, the write's cycles after the Key On write, levels (2fce write won, 1fde flag won), 1F801C1Eh (0202, 0208):\n");
    int lastFlag = -1, firstWrite = -1, odd = 0;
    for (int n = 0; n < STEPS; n++) {
        int w = 0, f = 0;
        printf("  n %2d:", n);
        for (int r = 0; r < TRIES; r++) printf(" %5d", writeAt[n][r]);
        printf("  ");
        for (int r = 0; r < TRIES; r++) {
            printf(" %04x", level[n][r]);
            if (level[n][r] == 0x2FCE) w++;
            else if (level[n][r] == 0x1FDE) f++;
            else odd++;
        }
        printf("  ");
        for (int r = 0; r < TRIES; r++) printf(" %04x", lsax[n][r]);
        printf("  %s\n", w == TRIES ? "write" : f == TRIES ? "flag" : "MIXED");
        if (f) lastFlag = n;
        if (w == TRIES && firstWrite < 0) firstWrite = n;
    }
    printf("\nCUT: the flag won last at n = %d, the write won every try from n = %d; onset %d %d %d; %d levels neither\n",
           lastFlag, firstWrite, classes[0].onset, classes[1].onset, classes[2].onset, odd);
    printf("\nspu/spu-lsax-sweep: Done.\n");
    for (;;) VSync(0);
}
