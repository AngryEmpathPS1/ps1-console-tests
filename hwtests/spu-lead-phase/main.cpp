/*
 * spu/spu-lead-phase - spu-lead's LEAD rows with the key-on written at every phase of the sample.
 *
 * WHY (v0.377.59). Three tests place a voice's decoder against two different clocks. spu-decode-ahead (the 5501 and
 * 7501) times its reads from the KON write, at whatever phase the write meets the SPU's sample, and fits a decoder that
 * restarts four samples after the latch (v0.377.59). spu-lead times them from the bit-11 edge with the KON written 0.1
 * sample after it, and reads the voice's SOUND in the capture ring in the same trial: there the consoles' onset is this
 * core's (ring index 9; the 9001 and one 5501 run 8) while their IRQ9 comes 2.3 samples later than here at every pitch
 * (0800h, 1000h, 2000h). And spu-regs' four key-on envelope rows read two samples early with SPU_KON_WINDOW. One KON
 * phase per row cannot tell a latch point that moves the decoder from one that moves only the sound.
 *
 * WHAT IT DOES. spu-lead's LEAD setup (voice 1 over eight blocks at 1000h, levels A A A A B C C C, every other voice on a
 * silent parked block, IRQ9 polled as SPUSTAT bit 6, Timer 2 at the system clock), IRQ9 on block 4's start (1040h), at
 * pitches 1000h and 2000h; the KON written D cycles after SPUSTAT bit 11 falls, D = 0, 32 ... 736. Each row: D, the
 * KON's time and IRQ9's in samples since the edge, and from voice 1's capture ring the onset (first sound) and the change
 * (first sample off level A).
 *
 * READ IT LIKE THIS. As D crosses the KON's latch point the onset steps by a sample (spu-latch's step); if the IRQ9 column
 * steps at the same D and by the same sample, the decoder restarts with the sound and the 2.3 is a fixed distance
 * between them; if IRQ9 steps elsewhere, or not at all, the two have latches of their own. "change - IRQ9" is spu-lead's
 * "IRQ leads by" at every phase.
 *
 * ON THIS CORE AT v0.377.59: the onset 9 and IRQ9 108.8 (1000h) / 56.7 (2000h) for D = 0..640, then 10 and 109.8 / 57.8
 *   from D = 672: the decoder and the sound step together, change - IRQ9 8.2-8.3 and 5.2-5.3 at every phase. One row
 *   fired early: 1000h at D = 224, IRQ9 6.7 samples after the edge - a read of 1040h before the restart (the voice keyed
 *   off by the trial before still decoding, the SPU IRQ on). The consoles' spu-lead row (KON 0.1 after the edge) reads
 *   111.0 / 59.0.
 * THE RULE: run it here first: `node hwtests/run.js spu-lead-phase --quiet=2000`.
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

struct Lead { uint16_t pitch; uint32_t irqA, d; uint32_t kon, irq; int onset, change; int16_t around[8]; };
#define PHASES 24
static Lead lead[2 * PHASES];

static void runLead(Lead &L) {
    keyOffAll();
    voice1(L.pitch, 0x1000);
    w16(0x1A4, L.irqA / 8);
    irqAck(); irqOn(true);
    syncFlip();
    const uint32_t t0 = now();
    while (now() - t0 < L.d) {}
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
    printf("\nspu/spu-lead-phase\n");
    printf("spu-lead's LEAD with the key-on at every phase of the sample. No assertions.\n\n");
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
    static const uint16_t pitches[2] = { 0x1000, 0x2000 };
    for (int p = 0; p < 2; p++)
        for (int k = 0; k < PHASES; k++) {
            Lead &L = lead[p * PHASES + k];
            L.pitch = pitches[p]; L.irqA = 0x1040; L.d = (uint32_t)k * 32;
            runLead(L);
        }
    keyOffAll();

    setCnt(0xC000);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("LEAD at every phase (samples since the bit-11 sync; IRQ9 at 1040h, block 4's start)\n");
    printf("pitch     D  key-on  onset   IRQ9   change  change-IRQ9\n");
    for (int i = 0; i < 2 * PHASES; i++) {
        const Lead &L = lead[i];
        printf("%04x   %3u  ", L.pitch, (unsigned)L.d);
        printSamples(L.kon);
        printf("  %4d  ", L.onset);
        if (L.irq) printSamples(L.irq); else printf("  none");
        printf("  %5d   ", L.change);
        if (L.irq && L.change >= 0) {
            const int lead10 = L.change * 10 - (int)((L.irq * 10 + SAMPLE / 2) / SAMPLE);
            printf("%4d.%d", lead10 / 10, (lead10 < 0 ? -lead10 : lead10) % 10);
        } else printf("     -");
        printf("\n");
    }
    printf("\nspu/spu-lead-phase: Done.\n");
    for (;;) VSync(0);
}
