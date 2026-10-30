/*
 * spu/spu-reverb - the reverb unit's arithmetic, read back out of sound RAM.
 *
 * WHY (docs/REFERENCE-ONLY-AUDIT.md, the SPU reverb rows). The reverb here is
 * DuckStation's ProcessReverb, which is mednafen's: the input decimated to
 * 22.05 kHz through a 39-tap FIR, the same-side and different-side IIR
 * reflections, a four-tap comb and two all-pass stages, the result
 * interpolated back up, every multiply shifted by 14 or 15 and clamped at
 * particular points. psx-spx gives the same block diagram in "reverse-
 * engineered" pseudocode with different rounding, and the shifts and clamps
 * exist only in the emulators' sources. Nothing any console measured grades
 * them: Redux's SPU ring captures (tools/spugolden.js) never turn reverb on.
 * Every game with reverb on - most of them - plays through this code.
 *
 * WHAT IT DOES. The delay lines live in sound RAM above mBASE, so what the
 * unit computed can be read back by DMA. Studio Small (psx-spx's preset, 1F40h
 * bytes at 7E0C0h), voice 0 into it (EON), SPUCNT with reverb's master enable.
 * Interrupts off, Timer 2 at the system clock.
 *   IMPULSE  voice 0 plays a block of silence, then one whose first sample is
 *            7000h and the rest silence, then loops a block of silence, its
 *            envelope at the top by then (linear attack at the fastest rate).
 *            The impulse waits a block because a voice's first samples are
 *            played before its envelope opens (here: the envelope starts 8
 *            samples after KON, the samples at once), which is spu-regs'
 *            question, not this one's. The CPU waits for
 *            SPUSTAT bit 11 to change (the capture buffers' half, every 256
 *            samples - a sample boundary it can see), then writes mBASE (which
 *            also sets the buffer address, psx-spx) and KON at once, and turns
 *            the master enable off a fixed 1,000.5 reverb steps later (1,536
 *            cycles a step) so no later write lands. The work area is DMA'd out
 *            and every non-zero halfword printed, with a checksum.
 *   DC       the same with a looping block of 4000h for 3 seconds (the ring
 *            goes round about 16 times): each writer settles on a fixed value,
 *            wherever the ring stands when it stops, so the distinct values and
 *            their counts are printed - the fixed points of this arithmetic,
 *            whatever the phase.
 *
 * READ IT LIKE THIS. The same program on this emulator prints this core's
 * dump; a console's IMPULSE dump that matches it halfword for halfword
 * confirms the arithmetic. One that matches only shifted by a step says the
 * sync was a step off, not that the arithmetic is (compare the DC values,
 * which cannot shift). One that differs in the values is the finding: its
 * first differing address names the stage.
 *
 * ON THIS CORE AT v0.267 (`node hwtests/run.js spu-reverb --frames=2000`, 4 s
 * here): IMPULSE 1,015 non-zero halfwords, checksum 0804acb9, in 79 non-zero
 * rows - the decimation FIR's ringing around each tap (00c3 fc58 fbd5 0050 ...)
 * and long runs of FFFFh behind the impulse, where a negative product shifted
 * right rounds down to -1 and the recursion holds it there (a rule that
 * truncated toward zero would leave 0000h). DC: 3 values, dc37h x3,264,
 * fcf6h x320, fd04h x416.
 * CONSOLE DAY (2026-10-05): the SCPH-5501 read 4,000 non-zero (690cac6e) on its
 * first run and 1,682 (100600a6) on its second, the SCPH-7501 1,682 (f1ce840b) -
 * the two consoles region for region alike, with LARGE values (a run of AD80h)
 * where this core's tail decays to zero, and DC 64 distinct values + 1,352
 * beyond. Since then each run reads the area back right after its zeroing DMA
 * and says how much is left non-zero (0 here at v0.362): old data that the
 * zeroing missed, or the reverb itself writing that much?
 * AND WHAT THE VOICE PLAYED (the same night): the consoles' areas look like a
 * SUSTAINED input - smooth rises to a ~6300h plateau, decays of ~0.88 a step
 * (vIIR), a flat 18E0h - which neither an impulse nor an impulse train makes
 * here (looped back to block 0 it reads 2,306 halfwords of FIR ringing, peaks
 * near 1800h). So each run now reads the sample back after its upload and,
 * before its KOFF, voice 0's repeat address, envelope and ENDX; and a VOICE
 * section plays the blocks on voice 1 and reads its capture ring (0800h). Here
 * (v0.365): the sample back exactly, repeat 0204h (IMPULSE) / 0200h (DC),
 * envelope 7FFFh, ENDX 1; VOICE 4 samples, ffff 10a5 4e7b 106d at 0ABh-0AEh.
 * THE THREE CONSOLES (2026-10-06, the fixed runs): IMPULSE 1,033 non-zero (533f44a7) on the 5501 and 7501 alike, the
 * 9001 the same values a reverb step later. AT v0.374 (M374, src/psx.js SPU_REVERB_RULES): 1,031 non-zero, checksum
 * b0a72ea3 - the SCPH-9001's area halfword for halfword, DC section too; the 7501's a step apart (5 edge halfwords).
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
/* DMA4 between RAM and sound RAM, `words` words in 16-word blocks. */
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    /* SPUSTAT bit 10, "Data Transfer Busy" (2026-10-06): the DMA finishes when its last word is in the SPU's 32-halfword
     * FIFO, not in sound RAM. Stopped at once, the FIFO's rest is lost: on the SCPH-5501 a 64-byte sample landed its
     * first 8 bytes and a DC block its first 4 halfwords (the BIOS's own samples stayed under the rest - what console
     * day's voices played), and an 8,000-byte zeroing left 27 halfwords of the old area. Waiting on the busy flag
     * alone changed nothing on the 5501 (the same 8 bytes, 25 halfwords): it does not hold while the last block is
     * written. So a fixed wait too, 100,000 cycles (~3 ms) - far more than 32 halfwords take. */
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x0400); i++) {}
    if (toSpu) idle(100000);
    spuMode(0);
}

static const uint16_t STUDIO_SMALL[32] = {
    0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
    0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
    0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
    0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000,
};
#define WA_SIZE 0x1F40u
#define WA_BASE (0x80000u - WA_SIZE)                            /* 7E0C0h */
#define WA_WORDS (WA_SIZE / 4)                                  /* 2,000 */

uint32_t area[WA_WORDS] __attribute__((aligned(16)));
uint32_t blocks[16] __attribute__((aligned(16)));               /* 4 ADPCM blocks of 16 bytes */

/* One run: the blocks at 1000h, reverb on, sync, mBASE and KON, `cycles` later the master enable off; the area out. */
/* What the work area read back right after the zeroing DMA, each run (console day, 2026-10-05): the SCPH-5501 and
 * SCPH-7501 left 1,682 halfwords non-zero after the impulse where this core leaves 1,015, region for region the same
 * on both consoles, with large values (a run of AD80h) where this core has a decaying tail - old data, or the reverb? */
static int zeroLeft;
/* And what voice 0 played (2026-10-05, after console day): both consoles' work areas look like a SUSTAINED input -
 * runs of 63xxh, a flat 18E0h - not an impulse's tail, and their DC run 64+ distinct values where a clean 4000h
 * settles on 3. psx-spx says block 2 (loop start and end both set) repeats in silence, as this core plays it. So
 * each run reads the sample back after its upload (sampleDiff: bytes differing from what was sent) and, before its
 * KOFF, voice 0's repeat address (1F801C0Eh: 0204h if block 2's loop start took), its envelope volume (1F801C0Ch)
 * and ENDX (1F801D9Ch bit 0, a loop end reached). */
static int sampleDiff;
static uint16_t repAfter, envAfter, endxAfter;
uint32_t sampleBack[16] __attribute__((aligned(16)));
static void run(uint32_t cycles) {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);             /* no KON pending */
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);   /* KOFF every voice */
    idle(200000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);                     /* SPU on, unmuted, reverb master off */
    write16(SPU(0x1AC), 0x0004);                                /* transfer control (and bit 11 works) */
    for (int i = 0; i < (int)WA_WORDS; i++) area[i] = 0;
    spuDma(WA_BASE, area, WA_WORDS, true);                      /* the work area zeroed */
    spuDma(WA_BASE, area, WA_WORDS, false);                     /* ...and read back (console day) */
    { int left = 0; const uint16_t* z = (const uint16_t*)area; for (int i = 0; i < (int)(WA_SIZE / 2); i++) if (z[i]) left++; zeroLeft = left; }
    spuDma(0x1000, blocks, 16, true);                           /* the sample */
    for (int i = 0; i < 16; i++) sampleBack[i] = 0xA5A5A5A5u;
    spuDma(0x1000, sampleBack, 16, false);                      /* ...and read back */
    { const uint8_t* x = (const uint8_t*)blocks; const uint8_t* y = (const uint8_t*)sampleBack; sampleDiff = 0;
      for (int i = 0; i < 64; i++) if (x[i] != y[i]) sampleDiff++; }
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * i), STUDIO_SMALL[i]);
    write16(SPU(0x180), 0x3FFF); write16(SPU(0x182), 0x3FFF);   /* main volume */
    write16(SPU(0x184), 0x4000); write16(SPU(0x186), 0x4000);   /* reverb output volume */
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);             /* no FM */
    write16(SPU(0x198), 0x0001); write16(SPU(0x19A), 0);        /* EON: voice 0 into the reverb */
    write16(SPU(0x000), 0x3FFF); write16(SPU(0x002), 0x3FFF);   /* voice 0: volume */
    write16(SPU(0x004), 0x1000);                                /* 44.1 kHz */
    write16(SPU(0x006), 0x1000 / 8);                            /* start */
    write16(SPU(0x008), 0x000F);                                /* linear attack at the fastest rate, sustain level max */
    write16(SPU(0x00A), 0x0000);                                /* sustain: linear, no change */
    cnt |= 0x0080; write16(SPU(0x1AA), cnt);                    /* reverb master enable */
    /* The sync: SPUSTAT bit 11 changes at a sample boundary. */
    const uint16_t b0 = read16(SPU(0x1AE)) & 0x0800;
    while ((read16(SPU(0x1AE)) & 0x0800) == b0) now();
    const uint32_t t0 = now();
    write16(SPU(0x1A2), (uint16_t)(WA_BASE / 8));               /* mBASE, and the buffer address */
    write16(SPU(0x188), 0x0001);                                /* KON voice 0 */
    while (now() - t0 < cycles) {}
    cnt &= (uint16_t)~0x0080; write16(SPU(0x1AA), cnt);          /* no more reverb writes */
    idle(20000);
    repAfter = read16(SPU(0x00E)); envAfter = read16(SPU(0x00C)); endxAfter = read16(SPU(0x19C));
    spuDma(WA_BASE, area, WA_WORDS, false);
    write16(SPU(0x18C), 0x0001);                                /* KOFF */
}
static void voiceReport() {
    printf("(voice 0: the sample read back %s; after the run its repeat address %04x, envelope %04x, ENDX bit 0 %d)\n",
           sampleDiff ? "DIFFERENT" : "exactly", repAfter, envAfter, endxAfter & 1);
    if (sampleDiff) {
        const uint8_t* y = (const uint8_t*)sampleBack;
        printf("  read back:");
        for (int i = 0; i < 64; i++) printf("%s%02x", (i % 16) ? " " : "\n   ", y[i]);
        printf("\n");
    }
}

int main() {
    printf("\nspu/spu-reverb\n");
    printf("Studio Small's work area after an impulse and after a DC input, read back by DMA. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);                  /* DMA4 on */
    /* STABLE SOUND RAM READS (2026-10-06): psx-spx, "SPU RAM DMA-Read (stable reading)": "Be sure that [1F801014h] is
     * set to 220931E1h (bit24-27 MUST be nonzero)". The BIOS leaves 200931E1h, and then each 32-halfword block of a DMA
     * read begins with an inserted FFFFh, loses its last halfword, and has "unstable halfwords ORed by FFFFh (or ORed by
     * other garbage values)". Until this day the test never set it: every read-back the SCPH-5501, 7501 and 9001
     * printed (the work area, the sample read back "DIFFERENT", the voice ring with FFFF at 000h, 020h, 040h...) went
     * through that, and console day's "sustained input" was the reads. ps1-tests' spu/memory-transfer sets it and
     * reads exactly on the consoles. Restored at the end. */
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);

    /* IMPULSE: block 0 silence; block 1 = sample 0 at 7000h (shift 0, filter 0, nibble 7), then silence; block 2 =
     * silence looping on itself (flags 07h: loop start, end, repeat). */
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[18] = 0x07;                                               /* block 1, sample 0 */
    b[33] = 0x07;                                               /* block 2 */
    run(1000u * 1536u + 768u);
    uint32_t sum = 0; int nz = 0;
    const uint16_t* h = (const uint16_t*)area;
    for (int i = 0; i < (int)(WA_SIZE / 2); i++) { sum = sum * 31u + h[i]; if (h[i]) nz++; }
    printf("(after the zeroing DMA the area read back %d non-zero halfwords)\n", zeroLeft);
    voiceReport();
    printf("IMPULSE, 1,000.5 steps: %d non-zero halfwords, checksum %08x. Rows of 16 from 7E0C0h, non-zero rows only:\n", nz, sum);
    for (int r = 0; r < (int)(WA_SIZE / 32); r++) {
        bool any = false;
        for (int k = 0; k < 16; k++) if (h[r * 16 + k]) any = true;
        if (!any) continue;
        printf("  %05x:", (unsigned)(WA_BASE + r * 32));
        for (int k = 0; k < 16; k++) printf(" %04x", h[r * 16 + k]);
        printf("\n");
    }

    /* DC: one block of 4000h (nibble 4) looping on itself. */
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[0] = 0x00; b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    run(3u * 33868800u);
    static uint16_t val[64]; static int count[64]; int nv = 0, other = 0;
    for (int i = 0; i < (int)(WA_SIZE / 2); i++) {
        int k = 0;
        while (k < nv && val[k] != h[i]) k++;
        if (k == nv) { if (nv < 64) { val[nv] = h[i]; count[nv++] = 1; } else other++; }
        else count[k]++;
    }
    printf("\n(after the zeroing DMA the area read back %d non-zero halfwords)\n", zeroLeft);
    voiceReport();
    printf("\nDC 4000h, 3 s: %d distinct values (and %d halfwords beyond the first 64), value x count:\n", nv, other);
    for (int k = 0; k < nv; k++) { printf("  %04x x%-4d", val[k], count[k]); if (k % 6 == 5) printf("\n"); }
    printf("\n");

    /* VOICE (2026-10-05): what a voice plays from IMPULSE's blocks, seen directly - voice 1's output is captured
     * into sound RAM at 0800h-0BFFh, a ring of 512 samples (psx-spx "Capture Buffers"). Voice 1, the same blocks at
     * 1000h, no reverb; ~2 ms after KON (88 samples, the impulse due ~28 in) the ring is read out and its non-zero
     * samples printed with their offsets. A console's voice that plays the impulse and silence shows one short burst;
     * one that plays something else shows it. */
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[18] = 0x07; b[33] = 0x07;
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(200000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 16, true);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);             /* no reverb input */
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);   /* voice 1: volume */
    write16(SPU(0x014), 0x1000);                                /* 44.1 kHz */
    write16(SPU(0x016), 0x1000 / 8);                            /* start */
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);   /* as voice 0 */
    write16(SPU(0x188), 0x0002);                                /* KON voice 1 */
    idle(67738);
    static uint16_t cap[512];
    spuDma(0x0800, (uint32_t*)cap, 256, false);
    const int half = (read16(SPU(0x1AE)) >> 11) & 1;
    write16(SPU(0x18C), 0x0002);
    int ncap = 0;
    for (int i = 0; i < 512; i++) if (cap[i]) ncap++;
    printf("\nVOICE 1 on the same blocks, 2 ms after KON: %d non-zero samples in its capture ring (written half now %d):\n", ncap, half);
    for (int i = 0, shown = 0; i < 512 && shown < 48; i++) if (cap[i]) { printf("  %03x:%04x", i, cap[i]); if (++shown % 8 == 0) printf("\n"); }
    printf("\n");

    write16(T2_MODE, m2);
    write32(0x1F801014, spuDelay);
    ExitCriticalSection();
    printf("\nspu/spu-reverb: Done.\n");
    for (;;) VSync(0);
}
