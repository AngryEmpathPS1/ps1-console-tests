/*
 * spu/spu-statx-tail - when SPUSTAT bits 7 and 9 rise after a DMA write (2026-10-30).
 *
 * WHY. psx-spx's dev text (commit fd70a18, under STATX) says that as CHCR clears the last 50-60 bytes are still on their way
 * to sound RAM, SPUSTAT shows the mode bits alone, and bits 7 and 9 rise together 1,400-1,700 cycles later when the last
 * write is done; that bit 10 never moves; and that polling bit 7 before leaving the mode keeps every byte. spu-dma's TAIL
 * polled bit 10 only, so none of our logs can say (findings/spu.md 8, Open). They do give the drain: a Stop k cycles after
 * CHCR clears lands 28-30 of 32 at k = 800 and all 32 by k = 1,600. And spu-xfer has bits 9 and 7 up with the mode bits
 * before any DMA starts (finding 11), so for the text to hold they must FALL during the transfer, which nobody here has
 * watched. This core sets both whenever the mode is DMA write: they never move, and a driver polling bit 7 here leaves at
 * once and loses the tail.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, unwrapped; DMA4 in 16-word blocks; main volume 0. Every trial
 * first lays FFFFh over the area (settled, spu-dma's fix), and afterwards reads it back with the stable 220931E1h
 * (findings 8, 9) to count what landed. The mode is set and shown before the phase wait, so that only the phase moves.
 * The WATCH: the CPU waits for SPUSTAT bit 11 to fall (spu-latch's edge) and D cycles more, writes CHCR, and reads SPUSTAT,
 * Timer 2 and CHCR in a loop with no calls in it, keeping every change of SPUSTAT and the time of it in RAM, until 4,000
 * cycles after CHCR read clear. Nothing prints until every trial is over.
 *   WRITE   32, 64 and 128 halfwords (1, 2 and 4 blocks) at 1F801014h = 200931E1h, the BIOS's and the runner's; D = 0, 96
 *           ... 672 (a sample is 768); three tries, round-robin so that a drift shows as a pattern, not a block.
 *   DELAY   32 halfwords at 220931E1h, the same phases, two tries: most of spu-dma's TAIL ran at that value, because its
 *           first read-back left it there, and DMA4 runs at a different pace (finding 10).
 *   MANUAL  the control. 32 halfwords queued, SPUCNT mode 1 written at D = 0 and 288, watched 4,000 cycles from the write,
 *           three tries. Bit 10 rises and clears there (finding 5), so this row proves the WATCH sees a bit move. A DMA row
 *           with nothing in its change list means nothing moved only because this row has changes in it.
 *   STOP    psx-spx's own recipe: 32 and 128 halfwords, CHCR clear, bit 7 polled, Stop the moment it reads 1 (at most 4,000
 *           cycles on). The eight phases, once each: how many landed.
 *   WARM-UP the first watch of the run, 32 halfwords at D = 0, on a cold cache (spu-dma's first trials read differently):
 *           printed apart, in no summary.
 * Every wait is bounded by Timer 2 and by a count of turns (rule 1), so nothing here can sit waiting for a bit that never
 * comes. A watch stops 200,000 cycles from its start whatever CHCR says, printing clear = -1.
 *
 * READ IT LIKE THIS. Times are cycles from the CHCR write (the SPUCNT write in MANUAL). After a row's | comes every change of
 * SPUSTAT, as time:value, from the value `before`. `rise7` and `rise9` are the LAST time each bit went 0 to 1, as cycles
 * after CHCR cleared (signed; - if it never rose), and `polls n x c` is n turns of c cycles. psx-spx's reading: before 02A0h,
 * a fall to 0020h after the write, a rise to 02A0h with rise7 = rise9 = +1,400..+1,700, bit 10 up in 0. If the bits are the
 * FIFO's request, the 64 and 128 rows fall and rise once a block. SPUSTAT's refresh is once a sample (finding 12), so a rise
 * at a fixed time after the drain reads across D as a window 768 cycles wide. STOP: 32 of 32 and 128 of 128 at every D if the
 * recipe holds. LANDED below the length in WRITE means the 4,000 cycles were too few, and that row says nothing.
 *
 * THE RULE: run here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js spu-statx-tail --raw --frames=4000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define D4_MADR 0x1F8010C0u
#define D4_BCR  0x1F8010C4u
#define D4_CHCR 0x1F8010C8u
#define COM_DELAY 0x1F801014u
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE
#define BIOS_DELAY 0x200931E1u
#define STABLE_DELAY 0x220931E1u
#define AREA 0x2000u                                /* spu-dma's work area */
#define TAIL 4000u                                  /* cycles watched after CHCR clears */
#define LIMIT 200000u                               /* cycles: the longest row ends near 8,000; every watch stops here */
#define EDGE_LIMIT 2000000u                         /* cycles: bit 11 changes every 196,608 (finding 12's convention) */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
/* Until c cycles after t0. A turn is well over a cycle, so the count ends it only if Timer 2 has stopped (rule 1). */
static void until(uint32_t t0, uint32_t c) { for (uint32_t i = 0; i <= c && now() - t0 < c; i++) {} }
static void idle(uint32_t c) { until(now(), c); }

/* The WATCH's reads: no call, nothing but the load. SPUSTAT is 19 cycles a read (finding 14), the rest a few. */
static inline uint16_t statx() { return *(volatile const uint16_t*)SPU(SPUSTAT); }
static inline uint16_t t2() { return *(volatile const uint16_t*)T2_COUNT; }
static inline uint32_t chcr() { return *(volatile const uint32_t*)D4_CHCR; }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(SPUCNT), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(SPUSTAT)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void dmaStart(uint32_t spuAddr, void* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(D4_MADR, (uint32_t)ram & 0x1FFFFF);
    write32(D4_BCR, ((uint32_t)(words / 16) << 16) | 16);
    write32(D4_CHCR, toSpu ? 0x01000201u : 0x01000200u);
}
static void dmaWait() { for (int i = 0; i < 200000 && (read32(D4_CHCR) & 0x01000000u); i++) {} }

/* SPUSTAT bit 11 falling: false if it never does (a console that froze the capture must not hang the test). */
static bool bit11(uint16_t want, uint32_t t0) {
    for (uint32_t i = 0; i < EDGE_LIMIT; i++) {
        if ((read16(SPU(SPUSTAT)) & 0x0800) == want) return true;
        if (now() - t0 > EDGE_LIMIT) return false;
    }
    return false;
}
static bool edge(uint32_t* at) {
    const uint32_t t0 = now();
    if (!bit11(0x0800, t0) || !bit11(0, t0)) return false;
    *at = now();
    return true;
}

static uint16_t fill[128] __attribute__((aligned(16)));
static uint16_t pat[128] __attribute__((aligned(16)));
static uint16_t back[128] __attribute__((aligned(16)));

/* FFFFh over the whole area, landed (a settle before the Stop: spu-dma), so that a read-back counts this trial's halfwords. */
static void prepare() { dmaStart(AREA, fill, 64, true); dmaWait(); idle(100000); spuMode(0); }
/* What landed, read stably (spu-dma: 200931E1h reads FFFFh into every block), with 1F801014h put back. */
static int landed(int halves) {
    const uint32_t d = read32(COM_DELAY);
    write32(COM_DELAY, STABLE_DELAY);
    for (int i = 0; i < 128; i++) back[i] = 0xCCCC;
    dmaStart(AREA, back, 64, false); dmaWait(); spuMode(0);
    write32(COM_DELAY, d);
    int n = 0;
    for (int i = 0; i < halves; i++) if (back[i] == pat[i]) n++;
    return n;
}

#define CHANGES 24                                  /* 128 halfwords: 4 blocks, a fall and a rise each, each bit apart */
struct Trial {
    uint32_t D, startAt, clear, end, polls;         /* cycles from the edge to the start; the rest from the start */
    uint16_t before;                                /* SPUSTAT just before the start */
    uint16_t n, stat[CHANGES];                      /* n counts on past CHANGES */
    uint32_t at[CHANGES];
    int16_t landed;
    bool edged;
};
#define NOT_CLEAR 0xFFFFFFFFu

/* Start the transfer and keep every change of SPUSTAT until TAIL cycles after CHCR clears. A manual transfer has no CHCR:
 * its TAIL runs from the SPUCNT write. Locals only inside the loop: now()'s RAM load and store would cost a poll turn. */
static void watch(Trial& T, bool manual) {
    T.n = 0;
    uint16_t last = statx();
    T.before = last;
    const uint32_t base = now();
    uint16_t lt = lastT;
    uint32_t t = 0, clear = 0, polls = 0;
    bool idleSeen = manual;
    if (manual) {
        cnt = (uint16_t)((cnt & ~0x30) | 0x10);
        *(volatile uint16_t*)SPU(SPUCNT) = cnt;
    } else {
        *(volatile uint32_t*)D4_CHCR = 0x01000201u;
    }
    while (polls < LIMIT) {
        const uint16_t s = statx();
        const uint16_t c = t2();
        t += (uint16_t)(c - lt); lt = c;
        polls++;
        if (s != last) {
            if (T.n < CHANGES) { T.at[T.n] = t; T.stat[T.n] = s; }
            T.n++; last = s;
        }
        if (idleSeen) { if (t - clear >= TAIL) break; }
        else if (!(chcr() & 0x01000000u)) { idleSeen = true; clear = t; }
        if (t >= LIMIT) break;
    }
    T.clear = idleSeen ? clear : NOT_CLEAR;
    T.end = t; T.polls = polls;
    clk = base + t; lastT = lt;
}

static void armDma(Trial& T, int halves, uint32_t D) {
    prepare();
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(AREA / 8));
    spuMode(2);                                     /* shown before the wait: only the phase moves */
    write32(D4_MADR, (uint32_t)pat & 0x1FFFFF);
    write32(D4_BCR, ((uint32_t)(halves / 32) << 16) | 16);
    uint32_t te = 0;
    T.D = D;
    T.edged = edge(&te);
    if (T.edged) until(te, D);
    T.startAt = T.edged ? now() - te : 0;
}
static void dmaTrial(Trial& T, int halves, uint32_t D) {
    armDma(T, halves, D);
    watch(T, false);
    idle(100000);
    spuMode(0);
    T.landed = (int16_t)landed(halves);
}
static void manualTrial(Trial& T, uint32_t D) {
    prepare();
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(AREA / 8));
    for (int i = 0; i < 32; i++) write16(SPU(0x1A8), pat[i]);
    uint32_t te = 0;
    T.D = D;
    T.edged = edge(&te);
    if (T.edged) until(te, D);
    T.startAt = T.edged ? now() - te : 0;
    watch(T, true);
    idle(100000);
    spuMode(0);
    T.landed = (int16_t)landed(32);
}

struct StopRow { uint32_t D, clear; int32_t seen; int16_t landed; bool edged; };
/* psx-spx's recipe: CHCR idle, then bit 7, then the Stop. */
static void stopTrial(StopRow& S, int halves, uint32_t D) {
    Trial T;
    armDma(T, halves, D);
    S.D = D; S.edged = T.edged; S.clear = NOT_CLEAR; S.seen = -1;
    const uint32_t t0 = now();
    *(volatile uint32_t*)D4_CHCR = 0x01000201u;
    for (uint32_t i = 0; i < LIMIT; i++) {
        const uint32_t t = now() - t0;
        if (!(chcr() & 0x01000000u)) { S.clear = t; break; }
        if (t > LIMIT) break;
    }
    const uint32_t t1 = now();
    for (uint32_t i = 0; i < LIMIT; i++) {
        const uint32_t t = now() - t1;
        if (statx() & 0x0080) { S.seen = (int32_t)t; break; }
        if (t > TAIL) break;
    }
    spuMode(0);
    idle(100000);
    S.landed = (int16_t)landed(halves);
}

/* The last 0 to 1 of a bit among a trial's changes, as a time from the start; false if it never rose. */
static bool lastRise(const Trial& T, uint16_t bit, uint32_t* at) {
    uint16_t prev = T.before;
    bool seen = false;
    const int n = T.n < CHANGES ? T.n : CHANGES;
    for (int i = 0; i < n; i++) {
        if ((T.stat[i] & bit) && !(prev & bit)) { *at = T.at[i]; seen = true; }
        prev = T.stat[i];
    }
    return seen;
}
/* The BIOS's printf (A0h:3Fh, on a console and here) prints these; only the conversions the other tests use. */
static void printSigned(int d) { printf("%s%d", d < 0 ? "-" : "+", d < 0 ? -d : d); }
static void printRise(const Trial& T, uint16_t bit) {
    uint32_t at;
    printf(" ");
    if (lastRise(T, bit, &at) && T.clear != NOT_CLEAR) printSigned((int)(at - T.clear));
    else printf("-");
}
static void printTrial(const Trial& T, int len) {
    printf("  D %3d at %3d  before %04x  clear %5d  rise7", (int)T.D, (int)T.startAt, T.before,
           T.clear == NOT_CLEAR ? -1 : (int)T.clear);
    printRise(T, 0x0080);
    printf(" rise9");
    printRise(T, 0x0200);
    printf("  landed %3d of %3d  polls %3d x %2d  |", T.landed, len, (int)T.polls, T.polls ? (int)(T.end / T.polls) : 0);
    const int n = T.n < CHANGES ? T.n : CHANGES;
    for (int i = 0; i < n; i++) printf(" %d:%04x", (int)T.at[i], T.stat[i]);
    if (T.n > CHANGES) printf(" ...%d more", T.n - CHANGES);
    if (!T.edged) printf("  NO BIT 11 EDGE");
    printf("\n");
}
/* A row's range over its trials: rise after CHCR clear, how many rose, what landed. */
static void summary(const Trial* T, int count, int len) {
    int lo7 = 0x7FFFFFFF, hi7 = -0x7FFFFFFF, lo9 = lo7, hi9 = hi7, clrLo = lo7, clrHi = hi7;
    int n7 = 0, n9 = 0, landLo = 32767, landHi = -1, busy = 0;
    for (int i = 0; i < count; i++) {
        uint32_t at;
        bool up = (T[i].before & 0x0400) != 0;
        for (int k = 0; k < T[i].n && k < CHANGES; k++) if (T[i].stat[k] & 0x0400) up = true;
        if (up) busy++;
        if (T[i].landed < landLo) landLo = T[i].landed;
        if (T[i].landed > landHi) landHi = T[i].landed;
        if (T[i].clear == NOT_CLEAR) continue;
        if ((int)T[i].clear < clrLo) clrLo = (int)T[i].clear;
        if ((int)T[i].clear > clrHi) clrHi = (int)T[i].clear;
        if (lastRise(T[i], 0x0080, &at)) { const int d = (int)(at - T[i].clear); if (d < lo7) lo7 = d; if (d > hi7) hi7 = d; n7++; }
        if (lastRise(T[i], 0x0200, &at)) { const int d = (int)(at - T[i].clear); if (d < lo9) lo9 = d; if (d > hi9) hi9 = d; n9++; }
    }
    printf("  SUMMARY of %d: clear %d..%d; rise7", count, clrLo, clrHi);
    if (n7) { printf(" "); printSigned(lo7); printf(".."); printSigned(hi7); printf(" (%d rose)", n7); } else printf(" never");
    printf("; rise9");
    if (n9) { printf(" "); printSigned(lo9); printf(".."); printSigned(hi9); printf(" (%d rose)", n9); } else printf(" never");
    printf("; bit 10 up in %d; landed %d..%d of %d\n", busy, landLo, landHi, len);
}

#define PHASES 8
#define TRIES 3
#define DELAY_TRIES 2
#define MANUAL_D 2
#define MANUAL_TRIES 3
static const int LEN[3] = { 32, 64, 128 };
static Trial warm;
static Trial writes[3][PHASES * TRIES];
static Trial delays[PHASES * DELAY_TRIES];
static Trial manuals[MANUAL_D * MANUAL_TRIES];
static StopRow stops[2][PHASES];

int main() {
    printf("\nspu/spu-statx-tail\n");
    printf("When SPUSTAT bits 7 and 9 rise after a DMA write: every change of SPUSTAT from the CHCR write to 4,000 cycles\n");
    printf("after CHCR clears, CHCR written D cycles after bit 11 falls (a sample is 768). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);            /* DMA4 on */
    const uint32_t spuDelay = read32(COM_DELAY);
    write16(SPU(0x1AC), 0x0004);                          /* transfer control Normal: bit 11 needs bit 2 or 3 of it */
    cnt = 0xC000; write16(SPU(SPUCNT), cnt);              /* SPU on, unmuted */
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);       /* output volume 0 */
    for (int i = 0; i < 128; i++) { fill[i] = 0xFFFF; pat[i] = (uint16_t)((i + 1) * 0x0101); }

    write32(COM_DELAY, BIOS_DELAY);
    dmaTrial(warm, 32, 0);
    for (int r = 0; r < TRIES; r++)
        for (int k = 0; k < PHASES; k++)
            for (int l = 0; l < 3; l++) dmaTrial(writes[l][k * TRIES + r], LEN[l], (uint32_t)k * 96);
    write32(COM_DELAY, STABLE_DELAY);
    for (int r = 0; r < DELAY_TRIES; r++)
        for (int k = 0; k < PHASES; k++) dmaTrial(delays[k * DELAY_TRIES + r], 32, (uint32_t)k * 96);
    write32(COM_DELAY, BIOS_DELAY);
    for (int r = 0; r < MANUAL_TRIES; r++)
        for (int k = 0; k < MANUAL_D; k++) manualTrial(manuals[k * MANUAL_TRIES + r], (uint32_t)k * 288);
    for (int k = 0; k < PHASES; k++) {
        stopTrial(stops[0][k], 32, (uint32_t)k * 96);
        stopTrial(stops[1][k], 128, (uint32_t)k * 96);
    }
    spuMode(0);
    write32(COM_DELAY, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("1F801014h at the start: %08x\n", (unsigned)spuDelay);
    printf("\nWARM-UP, the first watch of the run, a cold cache (32 halfwords, 200931E1h):\n");
    printTrial(warm, 32);
    for (int l = 0; l < 3; l++) {
        printf("\nWRITE %d halfwords, %d block(s) of 16 words, 200931E1h:\n", LEN[l], LEN[l] / 32);
        for (int i = 0; i < PHASES * TRIES; i++) printTrial(writes[l][i], LEN[l]);
        summary(writes[l], PHASES * TRIES, LEN[l]);
    }
    printf("\nDELAY 32 halfwords, 220931E1h:\n");
    for (int i = 0; i < PHASES * DELAY_TRIES; i++) printTrial(delays[i], 32);
    summary(delays, PHASES * DELAY_TRIES, 32);
    printf("\nMANUAL the control, 32 halfwords queued, SPUCNT mode 1 written at the start, no CHCR (clear reads 0):\n");
    for (int i = 0; i < MANUAL_D * MANUAL_TRIES; i++) printTrial(manuals[i], 32);
    summary(manuals, MANUAL_D * MANUAL_TRIES, 32);
    printf("\nSTOP psx-spx's recipe: CHCR clear (cycles from the write), bit 7 read 1 (cycles after that, -1 never), then Stop:\n");
    for (int l = 0; l < 2; l++)
        for (int k = 0; k < PHASES; k++) {
            const StopRow& S = stops[l][k];
            printf("  %3d halfwords D %3d: clear %5d  bit 7 at %5d  landed %3d of %3d%s\n", l ? 128 : 32, (int)S.D,
                   S.clear == NOT_CLEAR ? -1 : (int)S.clear, (int)S.seen, S.landed, l ? 128 : 32, S.edged ? "" : "  NO BIT 11 EDGE");
        }
    printf("\nspu/spu-statx-tail: Done.\n");
    for (;;) VSync(0);
}
