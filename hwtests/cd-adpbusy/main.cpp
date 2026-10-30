/*
 * cdrom/cd-adpbusy - when the drive's ADPBUSY bit rises and falls around an XA sector's playback, to the sample
 * (2026-10-06).
 *
 * WHY. 1F801800h bit 2, ADPBUSY ("XA-ADPCM fifo empty (0=Empty); set when playing XA-ADPCM sound", psx-spx), is not
 * modelled here: it reads 0 always. hwtests/cd-xa on the SCPH-9001, 7501 and 5501 read it once per capture-ring half
 * (256 samples): set for 18 halves around the 37,800 Hz sector's 4,704 samples of playback and 37 around the 18,900 Hz
 * one's 9,408, clear outside. A half is 5.8 ms; a game that polls the bit (to know a sound has finished, or that the
 * decoder has room) sees its edges, and to model it the edges are needed to the sample, beside the playback's own first
 * and last samples.
 *
 * WHAT IT DOES. Needs cd-xa's disc (tools/xatestdisc.js, burned; the drive is unlocked first, as cd-xa does).
 * Interrupts off, Timer 2 at the system clock. The SPU on with CD audio in (output volume 0), the drive's volume 80h.
 * Each row: Setmode 48h (XA on, filter on), Setfilter, Setloc, ReadN, then a tight loop (~100-200 cycles a turn) that
 * logs every change of ADPBUSY with its time and where the capture ring's write pointer was (from SPUSTAT bit 11's last
 * flip: a sample every 768 cycles), the interrupts (the first twelve), and two snapshots of voice... of the CD's capture
 * ring (0000h, left): 200 samples after ADPBUSY rises (where the playback's first sample lies) and 100 after it falls
 * (its last). Pause.
 *   18,900 Hz  the impulse sector at LBA 2000 (file 1, channel 1): every sample 16 and one of +28,672.
 *   37,800 Hz  the same at LBA 3000 (channel 2).
 *   STREAM     the filter run, LBA 1000-1039 (Setfilter 1,0): twenty silent 37,800 Hz mono sectors, every other
 *              sector - each plays for 4 sector times and they come every 2, so the decoder is offered twice what it
 *              can play: whether ADPBUSY ever falls between them, and when it falls after the last.
 *
 * READ IT LIKE THIS. Times are cycles from the ReadN's write; "ring p" is the capture ring's write position then (0-511,
 * a sample = 768 cycles). The first and last playback samples are found in the snapshots (the 16s are non-zero) and
 * given as times on the same clock, so each edge reads as so many samples before or after the playback it brackets.
 *
 * THE RULE: run here first: `node hwtests/run.js cd-adpbusy --disc=xa --quiet=2000 --frames=4000`.
 * HERE AT v0.369: ADPBUSY never set in any row (so no snapshot is taken - they wait for its edges); the data sectors'
 * INT1s from 4.40M (18,900 Hz row) and 4.57M (37,800 Hz) cycles, every ~451,600 after. This core's playback times are
 * cd-xa's (the 37,800 Hz sector's first sample 4,026,458 cycles after the ReadN's INT3).
 * THE THREE CONSOLES (2026-10-06 evening): rise 108,000-114,000 cycles before the next sector's INT1, fall 10,168-10,307
 * before the last sample, and the filter run busy 18,063,311-18,063,377 cycles - its five played sectors exactly
 * (STATUS "...what the three consoles agree on"). "first rise 238,6xx cycles from the first sample" on every row of every
 * console is THIS TEST'S OWN WINDOW, not the bit: the start snapshot locates samples from a write position worked out of
 * when this loop sees SPUSTAT bit 11 flip, the bit lags the ring, and the slot taken for the oldest (511 back, 200 after
 * the rise: 311 samples) is one just written. Counting the ring's non-zero samples instead was tried and dropped: the
 * ring fills during the DMA and the resampler's first outputs include exact zeros. Read the start off cd-xa (its halves)
 * and the rise off the INT1s. (Kept as the consoles ran it, so their logs grade against it.)
 * AT v0.371 (M371: the playback ~482 samples after the hand-over, ADPBUSY leading it by 14 at both ends, no queue): rise
 * 4,283,232 / 4,460,736 (108,383 / 110,093 before the INT1s), fall 10,192 / 10,167 before the last sample, the filter run
 * busy 18,062,504 cycles; "first rise" 238,663 / 238,701 - the window, as on the consoles.
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
/* Sound RAM to RAM, stable (220931E1h: hwtests/spu-dma). */
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
static uint8_t cmd(uint8_t c, bool second = false) {
    ackAll();
    command(c);
    uint8_t f = 0; const uint32_t t0 = now();
    while (!(f = irqFlag()) && now() - t0 < MS(1000)) {}
    drain(); ackAll();
    if (second && f == 3) { const uint32_t t1 = now(); uint8_t g; while (!(g = irqFlag()) && now() - t1 < MS(5000)) {} drain(); ackAll(); }
    return f;
}
/* The secret unlock (psx-spx), as cd-xa: the burned disc is refused over the cable without it. */
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

/* One row's log. */
struct Edge { uint32_t t; uint8_t v; uint16_t pos; };
struct Row {
    Edge e[64]; int ne, more;                       /* ADPBUSY's changes (v the new value), and how many past 64 */
    uint32_t it[12]; uint8_t ity[12]; int ni, intMore;
    uint32_t riseT, fallT;                          /* the first rise, the last fall */
    int firstIdx, lastIdx; uint32_t firstT, lastT;  /* the playback's first and last non-zero samples */
    bool startSnap, endSnap;
};
static Row rows[3];
static uint16_t snap[512] __attribute__((aligned(16)));
static uint32_t flipT; static uint8_t flipV;
/* The capture ring's write position at time t, from the last bit 11 flip (bit 11 = 1: the second half, from 256). */
static uint16_t posAt(uint32_t t) { return (uint16_t)(((flipV ? 256 : 0) + (t - flipT) / 768) & 511); }

static void runRow(Row &R, uint8_t channel, uint32_t lba, bool stream) {
    param(0x48); cmd(0x0E);                         /* Setmode: XA on, filter on, single speed */
    param(1); param(channel); cmd(0x0D);            /* Setfilter */
    setloc(lba);
    R.ne = R.more = R.ni = R.intMore = 0; R.riseT = R.fallT = 0; R.firstIdx = R.lastIdx = -1; R.startSnap = R.endSnap = false;
    uint8_t prevB = (*CD0 >> 2) & 1, prevH = (r16(SPUSTAT) >> 11) & 1;
    flipV = prevH; flipT = now();
    ackAll(); command(0x06);                        /* ReadN */
    const uint32_t t0 = now();
    uint32_t last = t0;
    for (;;) {
        const uint32_t t = now();
        const uint8_t b = (*CD0 >> 2) & 1;
        const uint8_t h = (r16(SPUSTAT) >> 11) & 1;
        if (h != prevH) { prevH = h; flipV = h; flipT = t; }
        if (b != prevB) {
            prevB = b;
            if (R.ne < 64) { R.e[R.ne].t = t - t0; R.e[R.ne].v = b; R.e[R.ne].pos = posAt(t); R.ne++; } else R.more++;
            if (b && !R.riseT) R.riseT = t;
            if (!b) R.fallT = t;
        }
        const uint8_t f = irqFlag();
        if (f) { if (R.ni < 12) { R.it[R.ni] = t - t0; R.ity[R.ni] = f; R.ni++; } else R.intMore++; drain(); ackAll(); }
        /* the snapshots: the playback's start 200 samples after the rise, its end 100 after the (latest) fall */
        if (!stream && R.riseT && !R.startSnap && t - R.riseT > 200 * 768) {
            const uint32_t td = now(); const uint16_t p = posAt(td);
            dmaRead(0x000, snap, 1024);
            R.startSnap = true;
            for (int age = 511; age >= 1; age--) { const int i = (p - age) & 511; if (snap[i]) { R.firstIdx = i; R.firstT = td - t0 - age * 768; break; } }
        }
        if (!stream && R.fallT && !b && !R.endSnap && t - R.fallT > 100 * 768) {
            const uint32_t td = now(); const uint16_t p = posAt(td);
            dmaRead(0x000, snap, 1024);
            R.endSnap = true;
            for (int age = 1; age < 512; age++) { const int i = (p - age) & 511; if (snap[i]) { R.lastIdx = i; R.lastT = td - t0 - age * 768; break; } }
        }
        if (b) last = t;
        if (R.riseT && !b && t - last > MS(stream ? 600 : 300) && (stream || R.endSnap)) break;
        if (t - t0 > MS(stream ? 6000 : 3000)) break;
    }
    cmd(0x09, true);                                /* Pause */
}

static void printRow(const Row &R, const char *name) {
    printf("\n%s:\n", name);
    printf("  ADPBUSY changes (cycles from ReadN, ring position):");
    for (int i = 0; i < R.ne; i++) printf("%s %s%u p%u", (i % 4) ? "," : "\n   ", R.e[i].v ? "rise " : "fall ", (unsigned)R.e[i].t, R.e[i].pos);
    if (!R.ne) printf(" none - never set");
    if (R.more) printf("\n   ... and %d more", R.more);
    printf("\n  interrupts:");
    for (int i = 0; i < R.ni; i++) printf(" INT%d %u", R.ity[i], (unsigned)R.it[i]);
    if (R.intMore) printf(" (+%d)", R.intMore);
    printf("\n");
    if (R.startSnap || R.endSnap) {
        printf("  playback: first sample %s", R.firstIdx >= 0 ? "" : "not found");
        if (R.firstIdx >= 0) printf("%u (ring %d)", (unsigned)R.firstT, R.firstIdx);
        printf(", last sample %s", R.lastIdx >= 0 ? "" : "not found");
        if (R.lastIdx >= 0) printf("%u (ring %d)", (unsigned)R.lastT, R.lastIdx);
        printf("\n");
        if (R.firstIdx >= 0 && R.ne) printf("  first rise %d cycles from the first sample", (int)(R.e[0].t - R.firstT));
        if (R.lastIdx >= 0 && R.fallT) printf(", last fall %d cycles from the last sample", (int)(R.e[R.ne > 0 ? R.ne - 1 : 0].t - R.lastT));
        printf("\n");
    }
}

int main() {
    printf("\ncdrom/cd-adpbusy\n");
    printf("ADPBUSY's edges around an XA sector's playback, to the sample. Needs cd-xa's disc. Cycles at the system clock.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);    /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    idx(1); *CD2 = 0; ackAll(); drain();
    unlock();
    cmd(0x0A, true);                                    /* Init */
    cmd(0x0C);                                          /* Demute */
    idx(2); *CD2 = 0x80; *CD3 = 0x00;                   /* the drive's volume: left to left, left to right */
    idx(3); *CD1 = 0x80; *CD2 = 0x00; *CD3 = 0x20;      /* right to right, right to left; apply */
    w16(0x1AC, 0x0004);
    setCnt(0xC001);                                     /* SPU on, unmuted, CD audio in */
    w16(0x180, 0); w16(0x182, 0);                       /* output volume 0 */
    w16(0x1B0, 0x7FFF); w16(0x1B2, 0x7FFF);
    runRow(rows[0], 1, 2000, false);
    runRow(rows[1], 2, 3000, false);
    runRow(rows[2], 0, 1000, true);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printRow(rows[0], "18,900 Hz (LBA 2000)");
    printRow(rows[1], "37,800 Hz (LBA 3000)");
    printRow(rows[2], "STREAM (LBA 1000-1039, twenty silent 37,800 Hz mono sectors, every other one)");
    printf("\ncdrom/cd-adpbusy: Done.\n");
    for (;;) VSync(0);
}
