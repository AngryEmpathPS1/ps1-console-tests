/*
 * spu/spu-lead - how far ahead of playback does a voice read its ADPCM, as
 * IRQ9 sees it?
 *
 * WHY (M248, ROAD B). Silicon mutes a block that ends without a repeat EIGHT
 * samples before playback leaves it (Redux's adpcm_edge_flag_end_mute golden),
 * so the decoder runs ahead of the interpolator: this core reads a halfword
 * whenever fewer than 12 decoded samples lie ahead (SPU_DECODE_AHEAD), and
 * IRQ9 meets those reads - a block's start by any read of the block, its
 * second half only by the read of the halfword there (hw4, mednafen's rule).
 * The mute pins the lead in SOUND. Nothing pins it in IRQ terms, and games
 * pace streamed music with IRQ9 (M115). nugget's tests/spu/spu-irq.c polls
 * are left UNSET, and its mid-block probe - the IRQ on a block's second half,
 * re-armed after every hit - reads "two hits, then none" on silicon, which
 * nothing explains (M242).
 *
 * WHAT IT DOES. Interrupts off, IRQ9 polled as SPUSTAT bit 6, Timer 2 at the
 * system clock (768 cycles a 44.1 kHz sample) added into 32 bits. Every
 * voice but 1 parked on a silent looping block at 1100h.
 *   LEAD   voice 1 over eight blocks at 1000h, each at its own level (ADPCM
 *          shift 0, filter 0, every nibble equal): four at A, block 4 at B,
 *          then C, looping on the last two. Capture SYNCED as nugget does: the
 *          key-on written just after SPUSTAT bit 11 falls (the capture ring
 *          back at its first half), so ring index and time since the sync are
 *          one scale. IRQ9 at block 4's start (1040h) and at its second half
 *          (1048h), at pitch 0800h, 1000h and 2000h. Printed: the key-on and
 *          IRQ9 in samples since the sync, and from voice 1's capture ring
 *          (800h, read back by DMA) the first sample the voice sounds (onset)
 *          and the first that leaves level A (change) with the samples round it.
 *   MIDBLOCK nugget's probe exactly (tests/spu/spu-irq.c): eight blocks at
 *          2000h, levels (b+1)&7, block 0 flags 06h, block 7 03h (a loop over
 *          all eight, 224 samples), pitch 1000h, IRQ9 at 2008h; acknowledged
 *          by clearing SPUCNT bit 6 and re-armed after every hit, eight tries,
 *          each giving up after 0.5 s. Each hit's time (a loop is 172,032
 *          cycles), and voice 1's ENVX and the ENDX bits, then and at the end.
 *
 * READ IT LIKE THIS. "IRQ leads by" = change - IRQ9: how many output samples
 * before the new level is heard the IRQ fires. The model: 8 SOURCE samples
 * at every pitch, so 16 / 8 / 4 output samples at 0800h / 1000h / 2000h for
 * the block's start; the second half is met 16 source samples into the block
 * (hw4 holds its samples 12-15), 8 after playback enters it. The key-on row
 * is the control: M186's seven samples from the write to the first sound, so
 * a console whose ring sits some samples off the bit-11 edge (nugget: "the
 * bit-11 sync pins the start to a boundary that differs across PS1
 * revisions") shows the same offset in both. MIDBLOCK on silicon: 2 hits.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js spu-lead --quiet=2000`):
 *   pitch  IRQ at  key-on  onset  IRQ9   change  IRQ leads by
 *   0800   1040h     0.1     9   209.9    226     16.1
 *   0800   1048h     0.1     9   234.0    226     -8.0
 *   1000   1040h     0.1     9   109.0    117      8.0
 *   1000   1048h     0.1     9   120.9    117     -3.9
 *   2000   1040h     0.1     9    58.9     62      3.1
 *   2000   1048h     0.1     9    65.0     62     -3.0
 * The key-on first sounds at ring index 9 - nugget's "a key-on onset latency
 * of ~9 samples" on silicon. A block's start fires 16 / 8 / 3 output samples
 * before its level is heard (the model's 8 source samples; 3, not 4, at 2x);
 * its second half 8 / 4 / 3 after. Level A reads 4079: 8192 at ENVX 3FFFh.
 * MIDBLOCK: 8 hits. Three in the first pass within 1,400 cycles (8,248,
 * 8,963, 9,630): the decoder's read of hw4, then a late fire at each re-arm
 * while hw4 is still the decoder's last read (M242, M248). Then five the same
 * way on the next loop (178,723-181,353), ENVX 3FFFh and the voice still
 * playing at the end. Silicon's 2 hits then none: the console's timestamps
 * say which of these it does not do.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SAMPLE 768u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
static void irqOn(bool on) { setCnt(on ? (cnt | 0x40) : (cnt & ~0x40)); }
static bool irqFlag() { return (r16(SPUSTAT) & 0x40) != 0; }
/* nugget's acknowledge: clear the enable, wait for the flag to go. */
static void irqAck() { irqOn(false); const uint32_t t0 = now(); while (irqFlag() && now() - t0 < 100000) {} }

static void manualWrite(uint32_t a, const uint16_t *d, int n) {
    setCnt(cnt & ~0x30);
    w16(0x1A6, a / 8);
    for (int i = 0; i < n; i++) w16(0x1A8, d[i]);
    setCnt((cnt & ~0x30) | 0x10);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {}
    setCnt(cnt & ~0x30);
}

/* nugget's spu_dma_sync, reading: SPU RAM at `a` into `dst` by DMA4. */
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    w16(0x1A6, a / 8);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    while (read32(0x1F8010C8) & 0x01000000) {}
    setCnt(cnt & ~0x30);
}

/* SPUSTAT bit 11 falling: the capture is back at the first half of its ring. */
static void syncFlip() {
    const uint32_t t0 = now();
    while (!(r16(SPUSTAT) & 0x800) && now() - t0 < 1000000) {}
    while ((r16(SPUSTAT) & 0x800) && now() - t0 < 1000000) {}
}

static void keyOffAll() { w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF); waitCycles(40 * SAMPLE); }

static uint16_t blk[8 * 8];
static void makeBlocks(const uint8_t *nib, const uint8_t *flags) {
    for (int b = 0; b < 8; b++) {
        blk[b * 8] = (uint16_t)(flags[b] << 8);                  /* shift 0, filter 0 */
        const uint16_t d = (uint16_t)(nib[b] * 0x1111);
        for (int i = 1; i < 8; i++) blk[b * 8 + i] = d;
    }
}

static void voice1(uint16_t pitch, uint32_t addr) {
    w16(0x10, 0); w16(0x12, 0);                                  /* volume 0: the capture is before it */
    w16(0x14, pitch);
    w16(0x16, addr / 8); w16(0x1E, addr / 8);
    w16(0x18, 0x000F); w16(0x1A, 0x1FC0);                        /* nugget's: at peak from the first sample */
}

static int16_t ring[512];

struct Lead { uint16_t pitch; uint32_t irqA; uint32_t kon, irq; int onset, change; int16_t around[8]; };
static Lead lead[6];

static void runLead(Lead &L) {
    keyOffAll();
    voice1(L.pitch, 0x1000);
    w16(0x1A4, L.irqA / 8);
    irqAck(); irqOn(true);
    syncFlip();
    const uint32_t t0 = now();
    w16(0x188, 0x0002);
    L.kon = now() - t0;
    L.irq = 0;
    while (now() - t0 < 600 * SAMPLE) if (irqFlag()) { L.irq = now() - t0; break; }
    irqAck();
    while (now() - t0 < 440 * SAMPLE) {}
    dmaRead(0x800, ring, 1024);
    L.onset = -1; L.change = -1;
    for (int i = 0; i < 440; i++) if (ring[i]) { L.onset = i; break; }
    if (L.onset >= 0) {
        const int from = L.onset + 20;
        const int a = ring[from];
        for (int i = from; i < 440; i++) if (ring[i] - a > 64 || a - ring[i] > 64) { L.change = i; break; }
        for (int k = 0; k < 8; k++) { const int i = L.change - 2 + k; L.around[k] = (L.change >= 0 && i >= 0 && i < 512) ? ring[i] : 0; }
    }
    w16(0x18C, 0x0002);
}

static uint32_t midT[8], midEnvx[8], midEndx[8]; static int midHits; static uint32_t endEnvx, endEndx;

static void printSamples(uint32_t cycles) {
    const uint32_t tenths = (cycles * 10 + SAMPLE / 2) / SAMPLE;
    printf("%4u.%u", (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

int main() {
    printf("\nspu/spu-lead\n");
    printf("How far ahead of playback a voice reads its ADPCM, as IRQ9 sees it. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */

    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    w16(0x180, 0); w16(0x182, 0); w16(0x184, 0); w16(0x186, 0);
    w16(0x198, 0); w16(0x19A, 0); w16(0x190, 0); w16(0x192, 0); w16(0x194, 0); w16(0x196, 0);
    static uint16_t parked[8] = { 0x0700, 0, 0, 0, 0, 0, 0, 0 };
    manualWrite(0x1100, parked, 8);
    keyOffAll();
    for (int v = 0; v < 24; v++) {
        const uint32_t b = v * 16;
        w16(b + 0, 0); w16(b + 2, 0); w16(b + 4, 0x1000);
        w16(b + 6, 0x1100 / 8); w16(b + 0xE, 0x1100 / 8);
        w16(b + 8, 0x000F); w16(b + 0xA, 0x1FC0);
    }
    w16(0x188, 0xFFFD); w16(0x18A, 0x00FF);                       /* every voice but 1 onto the parked block */
    waitCycles(200000);

    /* LEAD: A A A A B C C C, looping on the last two (block 6 loop start, block 7 end + repeat). */
    static const uint8_t leadNib[8] = { 2, 2, 2, 2, 5, 13, 13, 13 };
    static const uint8_t leadFlags[8] = { 0, 0, 0, 0, 0, 0, 4, 3 };
    makeBlocks(leadNib, leadFlags);
    manualWrite(0x1000, blk, 64);
    static const uint16_t pitches[3] = { 0x0800, 0x1000, 0x2000 };
    for (int p = 0; p < 3; p++)
        for (int h = 0; h < 2; h++) {
            Lead &L = lead[p * 2 + h];
            L.pitch = pitches[p]; L.irqA = h ? 0x1048 : 0x1040;
            runLead(L);
        }

    /* MIDBLOCK: nugget's sample and schedule. */
    static const uint8_t midFlags[8] = { 6, 0, 0, 0, 0, 0, 0, 3 };
    static uint8_t midNib[8];
    for (int b = 0; b < 8; b++) midNib[b] = (uint8_t)((b + 1) & 7);
    makeBlocks(midNib, midFlags);
    manualWrite(0x2000, blk, 64);
    keyOffAll();
    voice1(0x1000, 0x2000);
    irqAck();
    syncFlip();
    w16(0x1A4, 0x2008 / 8); irqOn(true);
    const uint32_t t0 = now();
    w16(0x188, 0x0002);
    midHits = 0;
    for (int attempt = 0; attempt < 8; attempt++) {
        const uint32_t a0 = now();
        bool hit = false;
        while (now() - a0 < 17000000) if (irqFlag()) { hit = true; break; }
        if (!hit) break;
        midT[midHits] = now() - t0;
        midEnvx[midHits] = r16(0x1C);
        midEndx[midHits] = r16(0x19C) | ((uint32_t)r16(0x19E) << 16);
        midHits++;
        irqAck();
        w16(0x1A4, 0x2008 / 8); irqOn(true);
    }
    endEnvx = r16(0x1C);
    endEndx = r16(0x19C) | ((uint32_t)r16(0x19E) << 16);
    irqAck();
    keyOffAll();

    setCnt(0xC000);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("LEAD (samples since the bit-11 sync; the ring's index is the same scale)\n");
    printf("pitch  IRQ at  key-on  onset   IRQ9   change  IRQ leads by   ring round the change (change-2 ..)\n");
    for (int i = 0; i < 6; i++) {
        const Lead &L = lead[i];
        printf("%04x   %04xh  ", L.pitch, (unsigned)L.irqA);
        printSamples(L.kon);
        printf("  %4d  ", L.onset);
        if (L.irq) printSamples(L.irq); else printf("  none");
        printf("  %5d   ", L.change);
        if (L.irq && L.change >= 0) {
            const int lead10 = L.change * 10 - (int)((L.irq * 10 + SAMPLE / 2) / SAMPLE);
            printf("%4d.%d      ", lead10 / 10, (lead10 < 0 ? -lead10 : lead10) % 10);
        } else printf("     -      ");
        for (int k = 0; k < 8; k++) printf(" %6d", L.around[k]);
        printf("\n");
    }
    printf("\nMIDBLOCK (nugget's probe: IRQ9 at 2008h, re-armed after each hit; a loop is 172,032 cycles)\n");
    for (int i = 0; i < midHits; i++)
        printf("  hit %d at %9u cycles (loop %u.%02u)  ENVX %04x  ENDX %06x\n", i + 1, (unsigned)midT[i],
               (unsigned)(midT[i] / 172032), (unsigned)((midT[i] % 172032) * 100 / 172032),
               (unsigned)midEnvx[i], (unsigned)midEndx[i]);
    printf("  %d hits%s; at the end ENVX %04x, ENDX %06x\n", midHits, midHits < 8 ? ", then none for 0.5 s" : "",
           (unsigned)endEnvx, (unsigned)endEndx);
    printf("\nspu/spu-lead: Done.\n");
    for (;;) VSync(0);
}
