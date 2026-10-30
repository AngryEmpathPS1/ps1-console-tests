/*
 * dma/concurrent-dma - two DMA channels at once: which one moves, do they
 * share the bus block by block, and does DPCR's priority field decide?
 *
 * WHY (ROAD A8 "DMA priority arbitration", dug 2026-09-27 on all five rungs;
 * ROAD B). Nothing measured it:
 *   Sony: nothing (one unanswered developer-board guess that CD DMA outranks
 *     the CPU);
 *   psx-spx: priority field 0 is the highest, ties go by channel with DMA6
 *     the highest; pre-emption only "probably" (from MADR updates);
 *   MiSTer: the lowest field wins, ties to the HIGHER channel, re-arbitrated
 *     between blocks;
 *   DuckStation: ascending channel order ("good enough", a TODO); Mednafen:
 *     every channel in parallel; Redux: the enable bits only.
 * This emulator runs each block-mode channel on its own device's pacing, so
 * two requesting channels move turn about. Sony's libraries set every field
 * equal (FF7 3B3BBBBBh, libetc 33333333h), so the tie rule is the one games
 * meet - and no program in any corpus here runs two channels at once, which
 * is why nothing ever decided it.
 *
 * WHAT IT DOES. DMA2 (RAM to VRAM, a 64x64 GP0(A0h) upload) and DMA4 (RAM to
 * SPU RAM), each 128 blocks of 16 words in SyncMode 1, armed, then started by
 * two back-to-back CHCR stores. The CPU then samples both MADRs and Timer 2
 * (the system clock / 8) until both CHCR busy bits clear. It runs only when
 * no channel holds the bus, which is when a sample means something. Four
 * arrangements:
 *   EQ 2>4   the fields equal (3 and 3, as Sony's libraries), DMA2 started first
 *   EQ 4>2   the fields equal, DMA4 started first
 *   2 HIGH   DMA2's field 0, DMA4's 7
 *   4 HIGH   DMA2's field 7, DMA4's 0
 * For each: when each channel's first and last block moved (cycles after the
 * first start), how many samples saw one channel move, how many both, and how
 * often the lead changed.
 *
 * Then OTC (DMA6, 16,384 entries, SyncMode 0: a burst that asks for the bus
 * the whole time it runs, the CPU stopped) started straight after DMA4, with
 * the fields equal, DMA6's the higher and DMA4's the higher: when the OTC was
 * done, how far DMA4 had got, and when DMA4 was done.
 *
 * READ IT LIKE THIS. The plain number is how many of one channel's blocks had
 * moved when the other was done. A channel that keeps the bus to the end of
 * its transfer leaves the other at ~0 blocks (or 128, if the other went
 * first) - and which goes first is the rule: the higher channel on a tie
 * (psx-spx, MiSTer), the lower (DuckStation's ascending order), or whichever
 * started first. Blocks shared as they are asked for leave each count where
 * its own device's pace puts it. A priority field that changes a count, or
 * makes the OTC wait behind DMA4, means the fields decide.
 *
 * ON THIS CORE AT v0.258 (`node hwtests/run.js concurrent-dma`): SHARED, and
 * the fields decide nothing. In all four orders DMA2 is done at 3,936-4,200
 * cycles with DMA4 at 4 of 128 blocks (its own pace, ~1,080 cycles a block),
 * and DMA4 is done at ~138,480. The OTC is done at ~16,700 with DMA4 at 2 of
 * 128 whatever the fields say, and DMA4 then ends ~15,400 cycles late
 * (~153,950): the burst took the bus at DMA4's next block.
 */
#include <common.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <gpu.h>
#include <io.h>
#include <timer.h>

#define BLOCKS 128
#define WORDS (BLOCKS * 16)
#define DPCR 0x1F8010F0
#define MADR(ch) (0x1F801080 + (ch) * 0x10)
#define BCR(ch)  (0x1F801084 + (ch) * 0x10)
#define CHCR(ch) (0x1F801088 + (ch) * 0x10)
#define SPU(o) (0x1F801C00 + (o))
#define NS 3000

static uint32_t src2[WORDS], src4[WORDS];
struct Sample { uint16_t t; uint8_t busy; uint32_t m2, m4; };
static Sample s[NS];

static void spuMode(uint16_t mode) {
    const uint16_t cnt = (read16(SPU(0x1AA)) & ~0x30) | (mode << 4);
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { }
}

static void armGpu() {
    writeGP1(1, 0);
    DrawSync(0);
    CPU2VRAM buf;
    setcode(&buf, 0xA0);
    setlen(&buf, 3);
    setXY0(&buf, 512, 256);
    setWH(&buf, 64, WORDS * 2 / 64);
    DrawPrim(&buf);
    writeGP1(4, 2);                                  /* DMA direction: CPU -> VRAM */
    write32(MADR(2), (uint32_t)src2 & 0xFFFFFF);
    write32(BCR(2), (BLOCKS << 16) | 16);
}

static void armSpu() {
    spuMode(0);
    write16(SPU(0x1AC), 0x0004);                     /* transfer type: normal */
    write16(SPU(0x1A6), 0x1000 / 8);                 /* to sound RAM 1000h */
    spuMode(2);                                      /* DMA write */
    write32(MADR(4), (uint32_t)src4 & 0xFFFFFF);
    write32(BCR(4), (BLOCKS << 16) | 16);
}

struct Result { int n; uint32_t first2, last2, done2, first4, last4, done4; int only2, only4, both, leads, at2done4, at4done2; };

static Result run(uint32_t pri2, uint32_t pri4, bool twoFirst) {
    Result r = {};
    const uint32_t keep = read32(DPCR) & ~((0xFu << 8) | (0xFu << 16));
    write32(DPCR, keep | ((8 | pri2) << 8) | ((8 | pri4) << 16));
    armGpu();
    armSpu();
    const uint32_t base2 = (uint32_t)src2 & 0xFFFFFF, base4 = (uint32_t)src4 & 0xFFFFFF;
    const uint32_t start2 = 0x01000201, start4 = 0x01000201;   /* from RAM, SyncMode 1, start */
    resetTimer(2);
    if (twoFirst) { write32(CHCR(2), start2); write32(CHCR(4), start4); }
    else          { write32(CHCR(4), start4); write32(CHCR(2), start2); }
    /* Busy bits first, then the time, then the addresses: a sample whose busy bit is clear has its channel's last
     * address in it. */
    int n = 0;
    while (n < NS) {
        const uint32_t c2 = read32(CHCR(2)), c4 = read32(CHCR(4));
        s[n].t = readTimer(2);
        s[n].m2 = read32(MADR(2)) & 0xFFFFFF;
        s[n].m4 = read32(MADR(4)) & 0xFFFFFF;
        s[n].busy = ((c2 >> 24) & 1) | (((c4 >> 24) & 1) << 1);
        if (!s[n++].busy) break;
    }
    r.n = n;
    uint32_t p2 = base2, p4 = base4;
    int lead = 0;
    for (int i = 0; i < n; i++) {
        const uint32_t t = (uint32_t)s[i].t * 8;
        const bool mv2 = s[i].m2 != p2, mv4 = s[i].m4 != p4;
        if (mv2 && !r.first2) r.first2 = t;
        if (mv4 && !r.first4) r.first4 = t;
        if (mv2) r.last2 = t;
        if (mv4) r.last4 = t;
        /* When one channel is done, how many of the other's blocks have moved: the plainest serial-or-shared number. */
        if (!(s[i].busy & 1) && !r.done2) { r.done2 = t; r.at2done4 = (s[i].m4 - base4) / 64; }
        if (!(s[i].busy & 2) && !r.done4) { r.done4 = t; r.at4done2 = (s[i].m2 - base2) / 64; }
        if (mv2 && mv4) r.both++;
        else if (mv2) { r.only2++; if (lead != 2) { if (lead) r.leads++; lead = 2; } }
        else if (mv4) { r.only4++; if (lead != 4) { if (lead) r.leads++; lead = 4; } }
        p2 = s[i].m2; p4 = s[i].m4;
    }
    spuMode(0);
    return r;
}

/*
 * OTC (DMA6, SyncMode 0: a burst, the CPU stopped until it ends) straight after DMA4 has started. The OTC asks for
 * the bus all the time it runs; the SPU asks for a block whenever its FIFO has room. How far DMA4 had got when the
 * OTC finished, and when each was done.
 */
#define OT 16384
static uint32_t ot[OT];
struct OtcResult { uint32_t otcDone, dma4Done; int dma4AtOtc; };
static OtcResult runOtc(uint32_t pri4, uint32_t pri6) {
    OtcResult r = {};
    const uint32_t keep = read32(DPCR) & ~((0xFu << 16) | (0xFu << 24));
    write32(DPCR, keep | ((8 | pri4) << 16) | ((8 | pri6) << 24));
    armSpu();
    const uint32_t base4 = (uint32_t)src4 & 0xFFFFFF;
    write32(MADR(6), ((uint32_t)&ot[OT - 1]) & 0xFFFFFF);
    write32(BCR(6), OT);
    resetTimer(2);
    write32(CHCR(4), 0x01000201);                    /* DMA4: from RAM, SyncMode 1, start */
    write32(CHCR(6), 0x11000002);                    /* DMA6: OTC, backwards, start and trigger */
    int n = 0;
    while (n < NS) {
        const uint32_t c4 = read32(CHCR(4)), c6 = read32(CHCR(6));
        const uint16_t t = readTimer(2);
        const uint32_t m4 = read32(MADR(4)) & 0xFFFFFF;
        n++;
        if (!(c6 & 0x01000000) && !r.otcDone) { r.otcDone = (uint32_t)t * 8; r.dma4AtOtc = (m4 - base4) / 64; }
        if (!(c4 & 0x01000000)) { r.dma4Done = (uint32_t)t * 8; break; }
    }
    spuMode(0);
    return r;
}

static void report(const char* name, const Result& r) {
    printf("  %-7s DMA2 done %6d with DMA4 at %3d/128 blocks;  DMA4 done %6d with DMA2 at %3d/128\n",
           name, (int)r.done2, r.at2done4, (int)r.done4, r.at4done2);
    printf("          first moves %d / %d, last %d / %d cycles; moving samples: DMA2 alone %d, DMA4 alone %d, both %d; lead changes %d\n",
           (int)r.first2, (int)r.first4, (int)r.last2, (int)r.last4, r.only2, r.only4, r.both, r.leads);
}

int main() {
    initVideo(320, 240);
    printf("\ndma/concurrent-dma\n");
    printf("DMA2 (to VRAM) and DMA4 (to SPU RAM), 128 blocks of 16 words each, started back to back; both MADRs sampled on Timer 2.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    for (int i = 0; i < WORDS; i++) { src2[i] = 0x7FFF0000u | i; src4[i] = 0x12340000u | i; }
    const uint16_t prevT2 = initTimer(2, 2);         /* the system clock / 8 */
    EnterCriticalSection();
    const Result a = run(3, 3, true);
    const Result b = run(3, 3, false);
    const Result c = run(0, 7, true);
    const Result d = run(7, 0, true);
    const OtcResult e = runOtc(3, 3), f = runOtc(7, 0), g = runOtc(0, 7);
    ExitCriticalSection();
    restoreTimer(2, prevT2);
    report("EQ 2>4", a);
    report("EQ 4>2", b);
    report("2 HIGH", c);
    report("4 HIGH", d);
    printf("\n  OTC (DMA6, %d entries, a burst) started straight after DMA4:\n", OT);
    const char* on[3] = { "EQ", "6 HIGH", "4 HIGH" };
    const OtcResult* ov[3] = { &e, &f, &g };
    for (int i = 0; i < 3; i++)
        printf("  %-7s OTC done %6d with DMA4 at %3d/128 blocks;  DMA4 done %6d\n", on[i], (int)ov[i]->otcDone, ov[i]->dma4AtOtc, (int)ov[i]->dma4Done);
    printf("\n  A channel that keeps the bus to its end: the other's blocks wait (the count at 'done' near 0 or 128).\n");
    printf("  Blocks shared as they are asked for: the counts follow each device's own pace, whatever the fields say.\n");
    printf("\ndma/concurrent-dma: Done.\n");
    for (;;) VSync(0);
}
