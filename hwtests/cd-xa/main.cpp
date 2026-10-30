/*
 * cdrom/cd-xa - three XA-ADPCM questions: are audio sectors dropped under the
 * filter alone, what does an 18,900 Hz impulse come out as, and how long
 * does the audio go on after its sector?
 *
 * WHY (ROAD A8, dug 2026-09-27; ROAD B "XA").
 *   FILTER. psx-spx: "If XA-ADPCM (and/or XA-Filter) is enabled via Setmode,
 *     then INT1 is generated only for non-ADPCM sectors." With the filter on
 *     and XA off (Setmode 08h) psx-spx and this core drop audio+RT sectors;
 *     DuckStation, mednafen, Redux and MiSTer deliver them as data.
 *   IMPULSE. M246 took the 3/7 phase step for 18,900 Hz (Sony's CXD1199AQ
 *     datasheet: every output at 132.3 kHz; mednafen and DuckStation's
 *     tables), where MiSTer and ares double each sample. No 18,900 Hz capture
 *     exists; audiotest grades this core against itself.
 *   LATENCY. The CD audio FIFO: every reference buffers about one sector,
 *     this core rings 16,384 samples; and ADPBUSY (1F801800h bit 2, "set when
 *     playing XA-ADPCM sound") is not modelled here.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at the system
 * clock added into 32 bits. It needs tools/xatestdisc.js's disc (`node
 * tools/xatestdisc.js --write=<dir>` makes cd-xa.bin/.cue; that disc also
 * boots this program as PSX.EXE). Init, Demute, the CD audio matrix left to
 * left and right to right at 80h; SPU on with CD audio in, output volume 0.
 *   FILTER   LBA 1000-1039, audio+RT (even) and data (odd), file 1 channel
 *            0, each sector's first data bytes a marker 'A'/'D' + LBA. Setfilter
 *            1,0 and ReadN under Setmode 00h, 08h and 48h for 0.9 s: every
 *            INT1's marker, in order.
 *   IMPULSE  one audio sector at 18,900 Hz (LBA 2000) and one at 37,800 Hz
 *            (LBA 3000, read three times - see impVol), each every sample 16 with one sample of +28,672 at
 *            its sample 2,016, followed by data sectors. Setmode 48h, Setfilter
 *            to its channel, ReadN; the CD-left capture ring (sound RAM 0000h)
 *            read by DMA at every flip of SPUSTAT bit 11 from the ReadN to
 *            0.3 s after the first INT1 - a continuous 44.1 kHz recording - and
 *            ADPBUSY at each flip.
 *
 * READ IT LIKE THIS. FILTER: a string of what arrived - under 08h, "D D D ..."
 * is psx-spx, "A D A D ..." the emulators. IMPULSE: times in cycles from the
 * ReadN's INT3 (1 sample = 768), the playback's first and last sample (16s),
 * the peak and the 49 samples round it (the impulse response, 16 included),
 * and ADPBUSY per 256-sample half ('1' set). At 18,900 Hz the response should
 * spread about twice as wide as at 37,800 (psx-spx: "about fifty 44100Hz
 * samples"); a doubled sample reads as pairs.
 *
 * ON THIS CORE AT v0.259 (`node hwtests/run.js cd-xa --disc=xa --quiet=2000`):
 * FILTER: 00h delivers every sector (A D A D ... to 1039, then D; 59 INT1s
 * in 0.9 s); 08h and 48h only the data sectors (43 INT1s from 1001) -
 * psx-spx's rule. IMPULSE at 18,900 Hz: 9,407 samples of playback (4,032 x
 * 44,100 / 18,900 = 9,408), starting 444,704 cycles before the next sector's
 * INT1 (a sector at 1x is 451,584: it plays as it is read); the peak 27,153
 * with 18,793 and 18,786 either side, ringing out to +-24 samples. At 37,800
 * Hz: 4,704 samples, the peak 23,198 with -3,957 before and 13,439 after.
 * ADPBUSY: never set - it is not modelled here.
 * SINCE 2026-10-06 IT UNLOCKS THE DRIVE FIRST (see unlock): its burned disc is refused by a stock drive when the
 * program comes over the cable rather than booting from the disc. At v0.367 the printout is the one before it but
 * for the new "drive region" line and the impulse rows' times, 9-44 cycles on (the unlock's commands move the
 * drive's phase); every count, string and sample the same.
 * THE SCPH-9001 (2026-10-06) read a burned copy of this disc poorly - the copy reads back sector for sector in a PC
 * drive, raw, and the generator's EDC and ECC are a pressed disc's (checked against Ridge Racer's and FF7's): FILTER
 * 00h 22 INT1s (59 here), 08h none, 48h 45 data sectors; the 18,900 Hz sector never came. The 37,800 Hz one played
 * whole (4,705 samples) and showed three things this core does not do:
 *   - its 16s before the impulse read 13, flat, where this core reads 1-4: the resampler SUMS its 29 products and
 *     shifts once (mednafen's GetCDAudio, and this core's own 18,900 Hz path), where this core's 37,800 Hz path
 *     shifts each product (DuckStation's ZigZagInterpolate; psx-spx writes "/8000h" per term). Summed, the console's
 *     peak and both neighbours come out exact (27,152, 2,837, 2,824), and 33 of the 49 samples; the other 16 within
 *     2, the tables' own rounding (psx-spx: "nearly correct results, but with small rounding errors");
 *   - every positive sample one below the sum and every negative one equal: x7FFFh/8000h, the CD input volume this
 *     test sets - the capture ring AFTER that volume, where psx-spx and this core put it before. So the 37,800 Hz
 *     sector is now read three times, at 7FFFh, 4000h and 7FFFh (impVol): after the volume, 4000h halves it;
 *   - ADPBUSY set for 18 halves (4,608 samples, the sector's playback) and clear outside them;
 * and its playback began 88,573 cycles before the next sector's INT1 where this core's begins 460,146 before (~0.8 of
 * a sector later) - to see on a clean read, since the drive was retrying.
 * A READ CHECK runs first since then (the 1st to the 30th data sector from LBA 2001 at single speed, 29 sector times:
 * 13,096,000 cycles), and an INT5 in a FILTER row is printed as a read error. HERE AT v0.367 with the volume rows:
 * READ CHECK 13,102,536; the 37,800 Hz peak 23,198 at 7FFFh, 4000h and 7FFFh alike (the capture before the volume,
 * the resampler's phase carried from read to read). On the SCPH-7501's program (SC430940) the same samples, and FILTER
 * 48h 35 data sectors where the model delivers 43.
 * THE SCPH-7501 (2026-10-06) read this disc perfectly at single speed (READ CHECK 13,095,610): its 37,800 Hz impulse at
 * a phase of its own, the same on both 7FFFh reads (with the 9001's, M369: the resampler sums its products); the first
 * console capture of the 18,900 Hz impulse (peak 27,343, open - STATUS M369); ADPBUSY for 18 halves at 37,800 Hz and
 * 37 at 18,900, each sector's playback. Its 4000h read got no sector, so a read that brings none in 3 s is now made a
 * second time; and its FILTER rows read '?' for a scattering of markers, taken before DRQSTS said the FIFO had them -
 * the marker read now waits for DRQSTS. AT v0.369 (M369's resampler, these two fixes): READ CHECK 13,102,548; the
 * 37,800 Hz peak 18,793 (this core's phase); FILTER and the rest as before.
 * THE SCPH-5501 (2026-10-06) placed the capture ring BEFORE the SPU's CD input volume (its 4000h read = its 7FFFh read),
 * so the 37,800 Hz sector is now read SEVEN ways (impVol, impMat): the drive's volume matrix at 80h, 40h, 7Fh and FFh,
 * plus the 4000h row for the one-chip consoles and an 80h repeat. HERE AT v0.369 the matrix is (x * v) >> 7: peak 18,793
 * at 80h, 9,396 at 40h, 18,646 at 7Fh, 32,767 (saturated) at FFh; 4000h and the repeat as 80h.
 * THE SCPH-7501 AND 9001 then read FFh alike and neither fits it near the peak (STATUS "The SCPH-7501's extra run"), so
 * three rows ABOVE 80h come before the repeat (2026-10-06): 90h, A0h and C0h (see impMat). HERE AT v0.369, at this
 * core's phase: 21,142, 23,491 and 28,189 - (x * v) >> 7 to the sample, no clamp reached (at the consoles' phase, whose
 * peak is 27,152 at 80h, A0h passes 32,767 at the peak only and C0h around it).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t data() { idx(0); return *CD2; }

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    w16(0x1A6, a / 8);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    for (uint32_t k = 0; (read32(0x1F8010C8) & 0x01000000) && k < 4000000; k++) {}   /* rule 1: bounded */
    setCnt(cnt & ~0x30);
}

/* A command and its first response (and, with `second`, its second). */
static uint8_t cmd(uint8_t c, bool second = false) {
    ackAll();
    command(c);
    uint8_t f = 0; const uint32_t t0 = now();
    while (!(f = irqFlag()) && now() - t0 < MS(1000)) {}
    drain(); ackAll();
    if (second && f == 3) { const uint32_t t1 = now(); uint8_t g; while (!(g = irqFlag()) && now() - t1 < MS(5000)) {} drain(); ackAll(); }
    return f;
}
/* A burned disc is locked: a stock drive refuses ReadN on a disc without the SCEx string until the secret unlock
 * (psx-spx "CDROM - Secret Unlock Commands": 50h-56h, with the region Test 19h,22h names - "of America" on a US
 * drive, "(Europe)" on a European one; Japanese drives ignore it). Sent first (2026-10-06), so this runs from Unirom
 * with its own disc in the drive as well as booted from it. Each answers INT5 11h,40h, unlocked or not; this
 * emulator's model answers so and changes nothing. */
static void unlock() {
    ackAll(); param(0x22); command(0x19);
    uint8_t f = 0; const uint32_t t0 = now();
    while (!(f = irqFlag()) && now() - t0 < MS(1000)) {}
    char reg[12] = { 0 }; int n = 0;
    while (hasResp() && n < 11) reg[n++] = (char)resp();
    drain(); ackAll();
    const char *r = (reg[4] == 'U') ? "of America" : (reg[4] == 'E') ? "(Europe)" : 0;
    printf("drive region: %s - %s\n", reg, r ? "unlocking" : "no unlock for it");
    if (!r) return;
    static const char *const S[7] = { 0, "Licensed by", "Sony", "Computer", "Entertainment", 0, 0 };
    for (int k = 0; k < 7; k++) {
        const char *s = (k == 5) ? r : S[k];
        if (s) for (int i = 0; s[i] && i < 16; i++) param((uint8_t)s[i]);
        cmd(0x50 + k);
    }
}
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    const uint32_t m = a / 4500, s = (a / 75) % 60, f = a % 75;
    param((uint8_t)(((m / 10) << 4) | (m % 10))); param((uint8_t)(((s / 10) << 4) | (s % 10))); param((uint8_t)(((f / 10) << 4) | (f % 10)));
    cmd(0x02);
}

#define NMARK 64
static char markKind[3][NMARK]; static uint32_t markLba[3][NMARK]; static int markN[3];
static const uint8_t filterModes[3] = { 0x00, 0x08, 0x48 };

#define HALVES 128
static int16_t cap[HALVES * 256];
static uint32_t flipT[HALVES]; static char adp[HALVES + 1];
struct Imp { uint32_t int1; int halves, first, last, peak; int16_t peakV, around[49]; };
#define NIMP 10                                         /* the impulse reads: see impVol */
static Imp imp[NIMP];

/* Every INT5 the filter runs met, and the first one's bytes: a disc the drive cannot read answers with errors (or
 * nothing at all), and a count of INT1s alone cannot tell that from a filter rule (2026-10-06, the SCPH-9001 and its
 * burned copy of this disc: 22 INT1s under 00h where ~59 were due, none under 08h - the copy reads back sector for
 * sector in a PC drive). */
static int errN[3]; static uint8_t errB[3][2];
static void runFilter(int k) {
    param(filterModes[k]); cmd(0x0E);                   /* Setmode */
    param(1); param(0); cmd(0x0D);                      /* Setfilter 1, 0 */
    setloc(1000);
    cmd(0x06);                                          /* ReadN */
    markN[k] = 0; errN[k] = 0;
    const uint32_t t0 = now();
    while (now() - t0 < MS(900)) {
        const uint8_t f = irqFlag();
        if (!f) continue;
        if (f == 5) {
            if (!errN[k]++) { errB[k][0] = hasResp() ? resp() : 0; errB[k][1] = hasResp() ? resp() : 0; }
            drain(); ackAll();
            continue;
        }
        if (f == 1 && markN[k] < NMARK) {
            drain();
            want(true);
            /* DRQSTS (1F801800h bit 6): wait for the sector's first bytes to be in the data FIFO before reading them, 1 ms
             * at most. The SCPH-7501 (2026-10-06) read '?' for a scattering of markers here: the bytes taken before the
             * FIFO had them. This core fills it at once. */
            { const uint32_t tw = now(); while (!(*CD0 & 0x40) && now() - tw < MS(1)) {} }
            const uint8_t b0 = data(), b1 = data(), b2 = data(), b3 = data();
            want(false);
            markKind[k][markN[k]] = (char)b0;
            markLba[k][markN[k]] = b1 | (b2 << 8) | (b3 << 16);
            markN[k]++;
        } else drain();
        ackAll();
    }
    cmd(0x09, true);                                    /* Pause */
}

/* The impulse runs: the 18,900 Hz sector, then the 37,800 Hz one three times with the SPU's CD input volume (1F801DB0h)
 * at 7FFFh, 4000h and 7FFFh again (2026-10-06): psx-spx puts the capture ring BEFORE that volume, and the SCPH-9001's
 * one good read fits AFTER it (every positive sample one below "sum, then shift", every negative one equal: x7FFFh/8000h).
 * Captured after it, 4000h halves the response; before it, nothing moves. The third read says whether the resampler's
 * phase (where the impulse falls among its seven tables) repeats from one read to the next. */
/* THE DRIVE'S VOLUME MATRIX (2026-10-06, after the SCPH-5501): its 4000h read was its 7FFFh read sample for sample, so
 * the ring IS before the SPU's CD input volume (psx-spx), and the x7FFFh/8000h step every console shows - each positive
 * sample one below the resampler's sum, each negative equal - lies earlier: the drive's own volume matrix
 * (1F801802h.2/3, "80h = normal") most likely. So the 37,800 Hz sector is also read with the matrix's left-to-left and
 * right-to-right at 40h, 7Fh and FFh: how the matrix scales, and whether 80h is the x7FFFh/8000h. The 4000h row stays
 * for the 7501 and 9001, whose SPU and CD decoder are one chip (CXD2938Q) where the 5501's are two. */
/* AND ABOVE 80h (2026-10-06, after the SCPH-7501's and 9001's sweeps): at FFh both consoles fit (s * v) >> 7 then the step
 * far from the peak and not near it (+810 where it gives -90, 19,989 where it gives 26,781), on both alike. 90h puts the
 * impulse at ~30,500 (no clamp anywhere if the formula holds), A0h at ~34,000 (only the peak past 32,767), C0h at ~40,700:
 * whether the matrix is linear above 80h and the FFh rows are where a clamp sits, or something else above 80h. */
static const uint16_t impVol[NIMP] = { 0x7FFF, 0x7FFF, 0x4000, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF };
static const uint8_t impMat[NIMP] = { 0x80, 0x80, 0x80, 0x40, 0x7F, 0xFF, 0x90, 0xA0, 0xC0, 0x80 };
static void setMatrix(uint8_t m) {
    idx(2); *CD2 = m; *CD3 = 0x00;                      /* left to left, left to right */
    idx(3); *CD1 = m; *CD2 = 0x00; *CD3 = 0x20;         /* right to right, right to left; apply */
}
/* A read that brings no sector in 3 s is made once more (the SCPH-7501's 4000h read, 2026-10-06, the only row that
 * could place the capture ring, got none): 'tries' says which one the printout is. */
static int impTries[NIMP];
static void runImpulseOnce(int r);
static void runImpulse(int r) {
    for (impTries[r] = 1; impTries[r] <= 2; impTries[r]++) { runImpulseOnce(r); if (imp[r].int1) break; }
    if (impTries[r] > 2) impTries[r] = 2;
}
static void runImpulseOnce(int r) {
    Imp &I = imp[r];
    const int hi = r > 0;                               /* 0: 18,900 Hz at LBA 2000; 1-3: 37,800 Hz at LBA 3000 */
    w16(0x1B0, impVol[r]); w16(0x1B2, impVol[r]);
    setMatrix(impMat[r]);
    param(0x48); cmd(0x0E);                             /* Setmode: XA on, filter on */
    param(1); param((uint8_t)(hi ? 2 : 1)); cmd(0x0D);
    setloc(hi ? 3000 : 2000);
    cmd(0x06);                                          /* ReadN */
    const uint32_t t0 = now();
    I.int1 = 0; I.halves = 0;
    uint16_t last = r16(SPUSTAT) & 0x800;
    while (I.halves < HALVES) {
        const uint8_t f = irqFlag();
        if (f) { if (f == 1 && !I.int1) I.int1 = now() - t0; drain(); ackAll(); }
        const uint16_t b = r16(SPUSTAT) & 0x800;
        if (b != last) {
            dmaRead(b ? 0x000 : 0x200, &cap[I.halves * 256], 512);   /* the half just finished */
            flipT[I.halves] = now() - t0;
            adp[I.halves] = (*CD0 & 0x04) ? '1' : '0';
            I.halves++;
            last = b;
        }
        if (I.int1 && now() - t0 > I.int1 + MS(300)) break;
        if (now() - t0 > MS(3000)) break;
    }
    adp[I.halves] = 0;
    cmd(0x09, true);                                    /* Pause */
    const int n = I.halves * 256;
    I.first = I.last = I.peak = -1;
    int best = 0;
    for (int i = 0; i < n; i++) {
        const int v = cap[i] < 0 ? -cap[i] : cap[i];
        if (v) { if (I.first < 0) I.first = i; I.last = i; }
        if (v > best) { best = v; I.peak = i; }
    }
    I.peakV = I.peak >= 0 ? cap[I.peak] : 0;
    for (int k = 0; k < 49; k++) { const int i = I.peak - 24 + k; I.around[k] = (I.peak >= 0 && i >= 0 && i < n) ? cap[i] : 0; }
}

/* The time of capture sample i: the half it is in ended at that half's flip. */
static uint32_t sampleTime(int i) { return flipT[i / 256] - (uint32_t)(255 - (i % 256)) * 768; }

static char impAdp[NIMP][HALVES + 1];
static uint32_t impFirstT[NIMP], impLastT[NIMP], impPeakT[NIMP];

int main() {
    printf("\ncdrom/cd-xa\n");
    printf("XA sectors under the filter, an 18.9 kHz impulse, and the audio after its sector. Needs cd-xa's disc.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);    /* DMA4 on */
    idx(1); *CD2 = 0; ackAll(); drain();

    unlock();                                           /* the burned disc (see unlock) */
    cmd(0x0A, true);                                    /* Init */
    cmd(0x0C);                                          /* Demute */
    idx(2); *CD2 = 0x80; *CD3 = 0x00;                   /* left to left, left to right */
    idx(3); *CD1 = 0x80; *CD2 = 0x00; *CD3 = 0x20;      /* right to right, right to left; apply */
    w16(0x1AC, 0x0004);
    setCnt(0xC001);                                     /* SPU on, unmuted, CD audio in */
    w16(0x180, 0); w16(0x182, 0);                       /* output volume 0 */
    w16(0x1B0, 0x7FFF); w16(0x1B2, 0x7FFF);             /* CD input volume (the capture is before it) */

    /* READ CHECK (2026-10-06): data sectors from LBA 2001 at single speed, the time from the first to the 30th - 29
     * sector times, 13,096,000 cycles, on a drive that reads the disc well (a rate: the seek before the first does
     * not count). A drive that cannot read the disc well - an old laser and a CD-R - is slow or errs, and then every row
     * below may be read errors rather than the drive's rules.
     * A check that gets NOTHING - no sector and no error - is made once more after 3 s (2026-10-06): on the SCPH-7501 the
     * test started as the disc went in, the check read 0 sectors and 0 errors while the drive spun up and read the disc's
     * contents, and printed "READS THE DISC POORLY" over rows that then all read perfectly. */
    for (int attempt = 1; attempt <= 2; attempt++) {
        param(0x00); cmd(0x0E);
        setloc(2001); cmd(0x06);
        int n = 0, e = 0; uint32_t first = 0, last = 0; const uint32_t t0 = now();
        while (n < 30 && now() - t0 < MS(3000)) { const uint8_t f = irqFlag(); if (!f) continue; const uint32_t t = now(); if (f == 1) { if (!n) first = t; last = t; n++; } if (f == 5) e++; drain(); ackAll(); }
        cmd(0x09, true);
        const uint32_t span = n > 1 ? last - first : 0;
        if (attempt == 1 && n == 0 && e == 0) {
            printf("READ CHECK: nothing in 3 s, and no error - the drive still starting up? Again in 3 s.\n");
            const uint32_t tw = now(); while (now() - tw < MS(3000)) { if (irqFlag()) { drain(); ackAll(); } }
            continue;
        }
        printf("READ CHECK at single speed: %d of 30 data sectors in 3 s, first to last %u cycles (29 sector times: 13096000), %d errors%s\n",
               n, (unsigned)span, e, (n < 30 || span > 19644000) ? " - THIS DRIVE READS THE DISC POORLY: the rows below may be read errors, not rules" : "");
        break;
    }
    for (int k = 0; k < 3; k++) runFilter(k);
    for (int r = 0; r < NIMP; r++) {
        runImpulse(r);
        for (int h = 0; h <= imp[r].halves; h++) impAdp[r][h] = adp[h];
        impFirstT[r] = imp[r].first >= 0 ? sampleTime(imp[r].first) : 0;
        impLastT[r] = imp[r].last >= 0 ? sampleTime(imp[r].last) : 0;
        impPeakT[r] = imp[r].peak >= 0 ? sampleTime(imp[r].peak) : 0;
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("FILTER (LBA 1000-1039, even audio+RT, odd data; Setfilter 1,0; what each INT1 delivered):\n");
    for (int k = 0; k < 3; k++) {
        printf("  Setmode %02xh: %2d INT1s ", filterModes[k], markN[k]);
        if (markN[k]) printf("from LBA %u: ", (unsigned)markLba[k][0]);
        for (int i = 0; i < markN[k]; i++) printf("%c", markKind[k][i] == 'A' || markKind[k][i] == 'D' ? markKind[k][i] : '?');
        if (errN[k]) printf("  INT5 x%d (first %02x %02x): READ ERRORS - not a filter rule", errN[k], errB[k][0], errB[k][1]);
        printf("\n");
    }
    for (int r = 0; r < NIMP; r++) {
        const Imp &I = imp[r];
        printf("\nIMPULSE at %s Hz (LBA %d), CD input volume %04xh, drive volume %02xh, cycles from the ReadN's INT3:\n",
               r ? "37,800" : "18,900", r ? 3000 : 2000, impVol[r], impMat[r]);
        if (impTries[r] > 1) printf("  (no sector in 3 s on the first try: this is the second)\n");
        printf("  first INT1 (the data sector after it) %u; %d halves recorded\n", (unsigned)I.int1, I.halves);
        printf("  playback: first sample %u, last %u (%u samples); peak %d at %u\n", (unsigned)impFirstT[r], (unsigned)impLastT[r],
               (unsigned)(I.last - I.first + 1), I.peakV, (unsigned)impPeakT[r]);
        printf("  round the peak (peak-24 .. peak+24):\n  ");
        for (int k = 0; k < 49; k++) { printf(" %6d", I.around[k]); if (k % 12 == 11) printf("\n  "); }
        printf("\n  ADPBUSY per half: %s\n", impAdp[r]);
    }
    printf("\ncdrom/cd-xa: Done.\n");
    for (;;) VSync(0);
}
