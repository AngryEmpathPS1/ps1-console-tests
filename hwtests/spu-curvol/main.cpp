/*
 * spu/spu-curvol - the SPU's CURRENT-LEVEL registers and its unknown registers (2026-10-07).
 *
 * WHY. psx-spx ("SPU Volume and ADSR Generator", "SPU Unknown Registers") documents 1F801E00h+voice*4 as voice N's
 * current volume, left and right - the level its volume sweep has reached - and 1F801DB8h/DBAh as the main volume's,
 * both readable. Until this test this core read 1E00h-1E5Fh back as whatever was last written there (0 from power-on).
 * The references agree on the READ (mednafen spu.cpp, DuckStation spu.cpp: the sweep's current level) and part on
 * everything else, and no console has been asked:
 *   - a WRITE to 1E00h+voice*4: mednafen sets the level (a sweep goes on from it; a fixed volume puts its own back at
 *     the next sample); DuckStation ignores it; this core stored it and nothing else.
 *   - a WRITE to 1DB8h/DBAh: mednafen sets the level (back at the next sample when fixed), DuckStation sets it and a
 *     fixed volume KEEPS it; this core ignored it.
 *   - a write to a voice's ENVX (1F801C0Ch+N*10h): all three emulators set the level the envelope steps from; psx-spx
 *     says the envelope "does overwrite the setting (from another internal register) whenever applying a new Step".
 *   - a voice that is OFF (never keyed, or released to the end): DuckStation and this core skip it, sweep and all,
 *     unless the SPU IRQ is enabled (an optimisation: the IRQ needs its decoder's reads), so its volume sweep stands
 *     still; mednafen runs every voice's sweep every sample. The IRQ enable cannot matter on a console.
 *   - a fixed volume written: DuckStation and this core take it at once; mednafen at the voice's next sample.
 * And the unknown registers: 1F801DA0h ("some kind of a read-only status register.. or just garbage"), 1F801DBCh (4
 * bytes) and 1F801E60h (32 bytes), "general-purpose RAM" with a usual cold-boot content, and 1F801E80h-1F801FFFh read
 * FFh. nocash's console, two days; nobody else's.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, unwrapped; stable sound RAM reads (220931E1h, spu-dma);
 * 1F801DACh = 0004h so SPUSTAT bit 11 runs. A sample is 768 cycles; bit 11 changes only every 256 samples (each half of
 * the capture buffers), so its falling edge anchors each sequence and the samples after it are counted in cycles - the
 * test's first version took every change of the bit for a sample, 256 of them (on this core it read a sweep of +56 a
 * sample as +3800h). Main volume 0 throughout: nothing is heard. In order:
 *   UNKNOWN   1DA0h, 1DBCh-1DBEh, 1E00h-1E7Eh as the BIOS and Unirom left them (before this program writes the SPU at
 *             all); 1DA0h 32 times, 1,000 cycles apart; 0000h, FFFFh, 5A5Ah, A5A5h into 1DBCh, 1DBEh, 1E60h-1E7Eh and
 *             1DA0h, each read back (the old values put back); 1E80h-1E86h read, written 1234h, read.
 *   FIXED     voice 5 (off) and voice 1 (playing): VOLL each of 0000h 0001h 1234h 3FFFh 4000h 4001h 7FFFh, VOLR its
 *             complement: 1E14h/1E16h (1E04h/1E06h) read at once (~10 cycles after the store) and two samples later.
 *   SWEEP     a linear increase (8020h: shift 8, step +7 = +56 a sample) from level 0, read once a sample, 300 cycles
 *             into each, for 48: voice 1 PLAYING (the control: every model sweeps it), voice 5 OFF with the SPU IRQ off,
 *             and voice 5 OFF with it on (its address 7FFF8h, which no voice reads here).
 *   VWRITE    voice 1 playing. Fixed (VOLL 2000h, level 4000h): 1234h into 1E04h, read at once and at samples 1, 2, 8.
 *             Sweeping (8020h from 0): 4000h into 1E04h after 20 samples, read at once and once a sample for 16.
 *   MWRITE    every voice off. Fixed (main volume 0): 1234h into 1DB8h, read at once and at samples 1, 2, 8.
 *             Sweeping (8020h from 0): 4000h into 1DB8h after 20 samples, read for 16.
 *   ENVX      voice 1 playing a DC block (4000h every sample, as spu-latch) into its capture ring (0800h-0BFFh: after the
 *             envelope, before the volume). HELD: the fastest attack, sustain level Fh, sustain rate 7Fh (which never
 *             steps): 2000h into 1F801C1Ch 300 cycles into the 48th sample after a falling edge; ENVX read just before,
 *             at once and once a sample; the ring from 8 samples before the write to 40 after. STEPPING: sustain a
 *             linear decrease (shift 10, step -8: -16 a sample) from where the attack left it: 7000h into ENVX in the
 *             96th sample; the same reads. Where HELD stands before the write is a reading too: from 7FFFh with
 *             sustain level Fh (8000h) this core takes one exponential decay step first, to 4000h.
 *   IRQ       (Redux's nugget spu-offvoice: its assertions, asked here) voice 1 on an eight-block loop at 2000h, the IRQ
 *             address on block 4: the cycles to the fire, SPUCNT read right after (does bit 6 clear itself?), SPUCNT.6
 *             written 0 (does the flag go?), 40 samples on re-armed (fires a lap later?); then keyed off, ENVX 0 and 200
 *             samples, armed on block 6 and then block 2 (does a silent voice still read? - a voice standing where it
 *             fell silent could fire one at once by the late rule, never both).
 *
 * READ IT LIKE THIS. SWEEP's playing voice must climb 56 a sample (or its reads mean nothing); the off voice then
 * says whether a console runs an idle voice's sweep - and the IRQ rows must agree, or the IRQ enable matters on a
 * console too. VWRITE/MWRITE: the written value at once and the fixed level the next sample (mednafen), held
 * (DuckStation's main volume), or never seen (DuckStation's voice volume). ENVX: if the ring jumps with the write and
 * then steps on from it, the write is the envelope's level (the emulators); if the ring never moves, the output reads
 * another register - and if ENVX reads 2000h on the held voice for ever but the ring stays at 7FFFh's level, psx-spx's
 * "another internal register" it is.
 *
 * ON THIS CORE AT v0.377.15 (2026-10-07, the read implemented): UNKNOWN all 0000h at entry (the harness sideloads at the
 * shell's entry, before the BIOS intro sets any voice; a console through Unirom shows the intro's volumes - nocash's
 * DIAG list has each voice's 1E00h+N*4 at twice its volume register), every pattern read back (1DA0h too), 1E80h-1E86h
 * FFFFh before and after. FIXED: twice VOLL/VOLR at once and two samples later (0001h -> 0002h, 4000h -> 8000h, 7FFFh
 * -> FFFEh), off or playing. SWEEP: the playing voice 0038h, 0070h ... +56 a sample; voice 5 off with the IRQ off 0000h
 * throughout, with it on +56 a sample (the off-voice skip). VWRITE: 1234h never seen (4000h at once and after), 4000h
 * into a sweep never seen (0460h, then 0498h ... +56). MWRITE the same (0000h; 0460h on). ENVX: HELD 3FFFh before (the
 * decay step), 2000h at once and from then on, the ring 1FDFh -> 0FF0h with the write; STEPPING 140Fh before, 7000h
 * then -16 a sample, the ring 0A05h -> 37C8h and down 8 a sample - the write is the level the envelope steps from.
 * IRQ: fired after 33,943 cycles, SPUCNT then C040h (bit 6 still set - Redux's assertion fails here, as reduxtest says),
 * acknowledged, re-armed fired 140,951 later; keyed off (ENVX 0) blocks 6 and 2 fired after 40,537 and 85,835 cycles (this
 * core runs an off voice whenever the SPU IRQ is enabled).
 * THE RULE: run here first: `node hwtests/run.js spu-curvol --frames=4000 --quiet=3000`.
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
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    w16(0x1AA, cnt);
    for (int i = 0; i < 100000 && (r16(0x1AE) & 0x3F) != (cnt & 0x3F); i++) {}
}
/* DMA4 between RAM and sound RAM, `words` words in 16-word blocks; an upload settled 100,000 cycles (spu-dma). */
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    w16(0x1A6, (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (toSpu) idle(100000);
    spuMode(0);
}
/* SPUSTAT bit 11 becoming `want`: returns the time it was seen. */
static uint32_t edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    while ((r16(0x1AE) & 0x0800) == w) now();
    while ((r16(0x1AE) & 0x0800) != w) now();
    return now();
}
/* A sample is 768 cycles (33.8688 MHz / 44.1 kHz, one crystal). SPUSTAT bit 11 changes only every 256 samples (each half
 * of the capture buffers), so it is an anchor - its falling edge a sample boundary, the capture rings at index 0 - and
 * the samples after it are counted in cycles: `at(t0, n, off)` waits for `off` cycles into sample n after the edge. */
#define SAMPLE 768u
static void at(uint32_t t0, uint32_t n, uint32_t off) { while (now() - t0 < n * SAMPLE + off) {} }

static uint32_t blocks[16] __attribute__((aligned(16)));
static uint16_t ring[512] __attribute__((aligned(16)));

static void allOff() {
    w16(0x188, 0); w16(0x18A, 0);
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);
    idle(100000);
}
static void spuOn(bool irq) {
    cnt = (uint16_t)(0xC000 | (irq ? 0x0040 : 0));
    w16(0x1AA, cnt);
    w16(0x180, 0); w16(0x182, 0);                               /* main volume 0: nothing heard */
    w16(0x184, 0); w16(0x186, 0);
    w16(0x194, 0); w16(0x196, 0); w16(0x198, 0); w16(0x19A, 0); w16(0x190, 0); w16(0x192, 0);
}
/* Voice v set up on the DC block at 44.1 kHz, its volume fixed `vol`, ADSR as given. */
static void voice(int v, uint16_t vol, uint16_t adsrLo, uint16_t adsrHi) {
    const uint32_t b = (uint32_t)v * 16;
    w16(b + 0x0, vol); w16(b + 0x2, vol);
    w16(b + 0x4, 0x1000);
    w16(b + 0x6, 0x1000 / 8);
    w16(b + 0x8, adsrLo); w16(b + 0xA, adsrHi);
}
static void kon(int v) { w16(0x188, (uint16_t)(1 << v)); }

/* ---- results ---- */
static uint16_t unkEntry[2 + 2 + 64], unkDa0[32], unkRw[18][4], unkDa0Rw[4], unk80[4][2];
static const uint16_t FIXV[7] = { 0x0000, 0x0001, 0x1234, 0x3FFF, 0x4000, 0x4001, 0x7FFF };
static uint16_t fixed[2][7][4];                                 /* [off/playing][value][L at once, R at once, L +2, R +2] */
static uint16_t sweep[3][48];
static uint16_t vwFixed[4], vwSweep[18], mwFixed[4], mwSweep[18];
static uint16_t envxBefore[2], envxRead[2][48], envxRing[2][48];

static void unknownRegs() {
    int n = 0;
    unkEntry[n++] = r16(0x1A0); unkEntry[n++] = r16(0x1A0);
    unkEntry[n++] = r16(0x1BC); unkEntry[n++] = r16(0x1BE);
    for (int i = 0; i < 64; i++) unkEntry[n++] = r16(0x200 + 2 * i);
    for (int i = 0; i < 32; i++) { unkDa0[i] = r16(0x1A0); idle(1000); }
    static const uint16_t PAT[4] = { 0x0000, 0xFFFF, 0x5A5A, 0xA5A5 };
    for (int k = 0; k < 18; k++) {
        const uint32_t o = k == 0 ? 0x1BC : k == 1 ? 0x1BE : 0x260 + 2 * (k - 2);
        const uint16_t old = r16(o);
        for (int p = 0; p < 4; p++) { w16(o, PAT[p]); unkRw[k][p] = r16(o); }
        w16(o, old);
    }
    for (int p = 0; p < 4; p++) { w16(0x1A0, PAT[p]); unkDa0Rw[p] = r16(0x1A0); }
    for (int k = 0; k < 4; k++) { unk80[k][0] = r16(0x280 + 2 * k); w16(0x280 + 2 * k, 0x1234); unk80[k][1] = r16(0x280 + 2 * k); }
}

static void fixedVolumes() {
    for (int playing = 0; playing < 2; playing++) {
        allOff(); spuOn(false);
        const int v = playing ? 1 : 5;
        const uint32_t b = (uint32_t)v * 16, cur = 0x200 + (uint32_t)v * 4;
        voice(v, 0, 0x000F, 0x1FC0);
        if (playing) { kon(v); idle(16 * SAMPLE); }
        for (int i = 0; i < 7; i++) {
            const uint32_t t0 = edge(0);
            at(t0, 0, 300);                                     /* mid-sample: no boundary before the read */
            w16(b + 0x0, FIXV[i]); w16(b + 0x2, (uint16_t)(FIXV[i] ^ 0x7FFF));
            fixed[playing][i][0] = r16(cur); fixed[playing][i][1] = r16(cur + 2);
            at(t0, 2, 300);
            fixed[playing][i][2] = r16(cur); fixed[playing][i][3] = r16(cur + 2);
        }
    }
}

static void sweeps() {
    for (int k = 0; k < 3; k++) {
        allOff(); spuOn(k == 2);
        if (k == 2) w16(0x1A4, 0x7FFF8 / 8);                    /* the IRQ address where no voice reads */
        const int v = k == 0 ? 1 : 5;
        const uint32_t b = (uint32_t)v * 16, cur = 0x200 + (uint32_t)v * 4;
        voice(v, 0, 0x000F, 0x1FC0);                            /* level 0 (fixed 0) */
        if (k == 0) kon(v);
        idle(4 * SAMPLE);
        const uint32_t t0 = edge(0);
        at(t0, 0, 300);
        w16(b + 0x0, 0x8020);                                   /* linear increase, shift 8, step +7 */
        for (int i = 0; i < 48; i++) { at(t0, i + 1, 300); sweep[k][i] = r16(cur); }
        if (k == 2) { cnt &= (uint16_t)~0x0040; w16(0x1AA, cnt); }
    }
}

static void voiceWrites() {
    allOff(); spuOn(false);
    voice(1, 0x2000, 0x000F, 0x1FC0); kon(1); idle(16 * SAMPLE);
    uint32_t t0 = edge(0);
    at(t0, 0, 300);
    w16(0x204, 0x1234);
    vwFixed[0] = r16(0x204);
    at(t0, 1, 300); vwFixed[1] = r16(0x204);
    at(t0, 2, 300); vwFixed[2] = r16(0x204);
    at(t0, 8, 300); vwFixed[3] = r16(0x204);

    w16(0x010, 0x0000); idle(4 * SAMPLE);                       /* level 0 */
    t0 = edge(0);
    at(t0, 0, 300);
    w16(0x010, 0x8020);
    at(t0, 20, 300);
    vwSweep[0] = r16(0x204);
    w16(0x204, 0x4000);
    vwSweep[1] = r16(0x204);
    for (int i = 0; i < 16; i++) { at(t0, 21 + i, 300); vwSweep[2 + i] = r16(0x204); }
}

static void mainWrites() {
    allOff(); spuOn(false);
    uint32_t t0 = edge(0);
    at(t0, 0, 300);
    w16(0x1B8, 0x1234);
    mwFixed[0] = r16(0x1B8);
    at(t0, 1, 300); mwFixed[1] = r16(0x1B8);
    at(t0, 2, 300); mwFixed[2] = r16(0x1B8);
    at(t0, 8, 300); mwFixed[3] = r16(0x1B8);

    w16(0x180, 0x0000); idle(4 * SAMPLE);
    t0 = edge(0);
    at(t0, 0, 300);
    w16(0x180, 0x8020);
    at(t0, 20, 300);
    mwSweep[0] = r16(0x1B8);
    w16(0x1B8, 0x4000);
    mwSweep[1] = r16(0x1B8);
    for (int i = 0; i < 16; i++) { at(t0, 21 + i, 300); mwSweep[2 + i] = r16(0x1B8); }
    w16(0x180, 0x0000);                                         /* main volume 0 again */
}

/* ENVX written `after` samples past a falling edge; ring entries from 8 before the write to 40 after. */
static void envxRun(int k, uint16_t adsrHi, int after, uint16_t value) {
    allOff(); spuOn(false);
    for (int i = 0; i < 256; i++) ((uint32_t*)ring)[i] = 0;
    spuDma(0x0800, (uint32_t*)ring, 256, true);
    spuOn(false);
    voice(1, 0x3FFF, 0x000F, adsrHi);
    kon(1);
    idle(600 * SAMPLE);                                         /* attack done, the ring past its first lap */
    const uint32_t t0 = edge(0);                                /* the ring back at index 0 */
    at(t0, after, 300);                                         /* mid-sample `after` */
    envxBefore[k] = r16(0x01C);
    w16(0x01C, value);
    envxRead[k][0] = r16(0x01C);
    for (int i = 1; i < 48; i++) { at(t0, after + i, 300); envxRead[k][i] = r16(0x01C); }
    w16(0x18C, 0x0002);
    spuDma(0x0800, (uint32_t*)ring, 256, false);
    for (int i = 0; i < 48; i++) envxRing[k][i] = ring[(after - 8 + i) & 511];
}

/* IRQ: does the SPU clear SPUCNT.6 itself when its IRQ fires, does writing it 0 acknowledge, does it fire again once
 * re-armed - and does a keyed-off, silent voice still read RAM? Redux's nugget spu-offvoice asserts the self-clear (it
 * reads psx-spx's "IRQ9 Enable (0=Disabled/Acknowledge, 1=Enabled)" so) and the silent reads ("All voices are permanently
 * reading data from SPU RAM ... so even inaudible voices can trigger IRQs", psx-spx); no silicon log of it is in the tree,
 * DuckStation and this core only set the flag (STATUS: its 1 failing in reduxtest). Voice 1 loops an EIGHT-block sample at
 * 2000h with the IRQ address on block 4 (2040h), as spu-offvoice does: a voice whose current block is the IRQ address fires
 * the moment the enable is written (the "late" rule, M242 - spu-irq), so a one-block loop would fire on every enable and
 * say nothing about reading; here a fire needs the voice to come round to block 4, once a lap (224 samples). */
static uint32_t loop8[32] __attribute__((aligned(16)));
static uint32_t irqOn, irqOff, irqAgain, irqOffK[2];
static uint16_t irqCnt, irqStat, irqStatAck, irqEnvx, irqCntWrote;
static int irqAcked;
static uint32_t waitIrq(uint32_t limit) {
    const uint32_t t0 = now();
    while (!(r16(0x1AE) & 0x0040)) if (now() - t0 > limit) return 0xFFFFFFFFu;
    return now() - t0;
}
static void irqRun() {
    allOff(); spuOn(false);
    uint8_t* b8 = (uint8_t*)loop8;
    for (int k = 0; k < 8; k++) {
        uint8_t* p = b8 + k * 16;
        p[0] = 0x00;
        p[1] = k == 0 ? 0x04 : (k == 7 ? 0x03 : 0x00);          /* loop start; loop end + repeat */
        for (int i = 2; i < 16; i++) p[i] = 0x22;
    }
    spuDma(0x2000, loop8, 32, true);
    spuOn(false);
    voice(1, 0x3FFF, 0x000F, 0x0000);                           /* the fastest release: silent soon after KOFF */
    w16(0x016, 0x2000 / 8);
    kon(1); idle(64 * SAMPLE);
    w16(0x1A4, 0x2040 / 8);
    cnt = (uint16_t)(cnt | 0x0040); irqCntWrote = cnt; w16(0x1AA, cnt);
    irqOn = waitIrq(4000000);
    irqCnt = r16(0x1AA); irqStat = r16(0x1AE);                  /* SPUCNT as the console has it after the fire */
    cnt = (uint16_t)(cnt & ~0x0040); w16(0x1AA, cnt);           /* acknowledge */
    irqAcked = 0;
    for (int i = 0; i < 100000; i++) if (!(r16(0x1AE) & 0x0040)) { irqAcked = 1; break; }
    irqStatAck = r16(0x1AE);
    idle(40 * SAMPLE);                                          /* off block 4 before re-arming (the late rule) */
    cnt = (uint16_t)(cnt | 0x0040); w16(0x1AA, cnt);            /* re-armed: the voice loops over the address */
    irqAgain = waitIrq(4000000);
    cnt = (uint16_t)(cnt & ~0x0040); w16(0x1AA, cnt);
    w16(0x18C, 0x0002);                                         /* KOFF: release to silence */
    for (int i = 0; i < 4000000 && r16(0x01C); i++) {}
    idle(200 * SAMPLE);
    irqEnvx = r16(0x01C);
    /* Two blocks in turn, 6 then 2: a voice that stopped where it fell silent sits on one of them at most, so at most
     * one fires at once by the late rule; a voice still reading fires on both, each within a lap. */
    for (int k = 0; k < 2; k++) {
        w16(0x1A4, (uint16_t)((k ? 0x2020 : 0x2060) / 8));
        cnt = (uint16_t)(cnt | 0x0040); w16(0x1AA, cnt);
        irqOffK[k] = waitIrq(8000000);
        cnt = (uint16_t)(cnt & ~0x0040); w16(0x1AA, cnt);
        for (int i = 0; i < 100000 && (r16(0x1AE) & 0x0040); i++) {}
    }
    irqOff = irqOffK[0];
}

int main() {
    printf("\nspu/spu-curvol\n");
    printf("The SPU's current-level registers (1F801E00h+N*4, 1F801DB8h, ENVX) read and written, and its unknown\n");
    printf("registers (1F801DA0h, 1DBCh, 1E60h-1E7Fh, 1E80h). Values in hex; a sample is 768 cycles.\n");
    printf("No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    unknownRegs();                                              /* before anything else writes the SPU */
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    w16(0x1AC, 0x0004);                                         /* SPUSTAT bit 11 needs 1F801DACh bit 2 or 3 */

    uint8_t* bb = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) bb[i] = 0;
    bb[1] = 0x07;                                               /* loop start + repeat on itself */
    for (int i = 2; i < 16; i++) bb[i] = 0x44;                  /* 4000h every sample */
    allOff(); spuOn(false);
    spuDma(0x1000, blocks, 16, true);

    fixedVolumes();
    sweeps();
    voiceWrites();
    mainWrites();
    envxRun(0, 0x1FC0, 48, 0x2000);                             /* held: sustain rate 7Fh */
    envxRun(1, 0x4A00, 96, 0x7000);                             /* stepping: sustain linear decrease, shift 10, -8 */
    irqRun();
    allOff();

    printf("\nUNKNOWN at entry: 1DA0 %04x %04x  1DBC %04x %04x\n", unkEntry[0], unkEntry[1], unkEntry[2], unkEntry[3]);
    for (int r = 0; r < 4; r++) {
        printf("  1E%02x:", r * 0x20);
        for (int i = 0; i < 16; i++) printf(" %04x", unkEntry[4 + r * 16 + i]);
        printf("\n");
    }
    printf("UNKNOWN 1DA0 x32:");
    for (int i = 0; i < 32; i++) printf("%s%04x", i % 16 ? " " : "\n  ", unkDa0[i]);
    printf("\nUNKNOWN read back after 0000 FFFF 5A5A A5A5:\n");
    for (int k = 0; k < 18; k++) {
        const uint32_t o = k == 0 ? 0x1BC : k == 1 ? 0x1BE : 0x260 + 2 * (k - 2);
        printf("  1%03x: %04x %04x %04x %04x\n", 0xC00 + o, unkRw[k][0], unkRw[k][1], unkRw[k][2], unkRw[k][3]);
    }
    printf("  1DA0: %04x %04x %04x %04x\n", unkDa0Rw[0], unkDa0Rw[1], unkDa0Rw[2], unkDa0Rw[3]);
    printf("UNKNOWN 1E80-1E86 before and after 1234:");
    for (int k = 0; k < 4; k++) printf(" %04x/%04x", unk80[k][0], unk80[k][1]);
    printf("\n");

    printf("\nFIXED: VOLL (VOLR = VOLL^7FFF) / current L R at once / L R two samples later\n");
    for (int p = 0; p < 2; p++) {
        printf(" voice %d (%s):\n", p ? 1 : 5, p ? "playing" : "off");
        for (int i = 0; i < 7; i++)
            printf("  %04x: %04x %04x   %04x %04x\n", FIXV[i], fixed[p][i][0], fixed[p][i][1], fixed[p][i][2], fixed[p][i][3]);
    }
    static const char* SW[3] = { "voice 1 playing", "voice 5 off, SPU IRQ off", "voice 5 off, SPU IRQ on" };
    printf("\nSWEEP (8020h from level 0), current L once a sample:\n");
    for (int k = 0; k < 3; k++) {
        printf(" %s:", SW[k]);
        for (int i = 0; i < 48; i++) printf("%s%04x", i % 16 ? " " : "\n  ", sweep[k][i]);
        printf("\n");
    }
    printf("\nVWRITE fixed (VOLL 2000): 1234 into 1E04, read at once, samples 1 2 8: %04x %04x %04x %04x\n",
           vwFixed[0], vwFixed[1], vwFixed[2], vwFixed[3]);
    printf("VWRITE sweeping: before %04x, 4000 into 1E04, at once %04x, then once a sample:", vwSweep[0], vwSweep[1]);
    for (int i = 0; i < 16; i++) printf("%s%04x", i % 8 ? " " : "\n  ", vwSweep[2 + i]);
    printf("\nMWRITE fixed (main 0): 1234 into 1DB8, read at once, samples 1 2 8: %04x %04x %04x %04x\n",
           mwFixed[0], mwFixed[1], mwFixed[2], mwFixed[3]);
    printf("MWRITE sweeping: before %04x, 4000 into 1DB8, at once %04x, then once a sample:", mwSweep[0], mwSweep[1]);
    for (int i = 0; i < 16; i++) printf("%s%04x", i % 8 ? " " : "\n  ", mwSweep[2 + i]);
    printf("\n");
    static const char* EN[2] = { "HELD (sustain rate 7F), 2000 written", "STEPPING (sustain -16 a sample), 7000 written" };
    for (int k = 0; k < 2; k++) {
        printf("\nENVX %s: ENVX before %04x, at once, then once a sample:", EN[k], envxBefore[k]);
        for (int i = 0; i < 48; i++) printf("%s%04x", i % 16 ? " " : "\n  ", envxRead[k][i]);
        printf("\n voice 1's ring from 8 samples before the write:");
        for (int i = 0; i < 48; i++) printf("%s%04x", i % 16 ? " " : "\n  ", envxRing[k][i]);
        printf("\n");
    }
    printf("\nIRQ voice 1 playing over the IRQ address (SPUCNT %04x written): fired after %d cycles; SPUCNT then %04x "
           "(bit 6 %d), SPUSTAT %04x\n", irqCntWrote, (int)irqOn, irqCnt, (irqCnt >> 6) & 1, irqStat);
    printf("  SPUCNT.6 written 0: flag %s (SPUSTAT %04x); re-armed: fired again after %d cycles\n",
           irqAcked ? "cleared" : "STILL SET", irqStatAck, (int)irqAgain);
    printf("  keyed off, ENVX %04x, armed on block 6 then block 2: fired after %d and %d cycles (-1: not in 8,000,000)\n",
           irqEnvx, (int)irqOffK[0], (int)irqOffK[1]);

    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("\nspu/spu-curvol: Done.\n");
    for (;;) VSync(0);
}
