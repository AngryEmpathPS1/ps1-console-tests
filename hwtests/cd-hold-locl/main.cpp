/*
 * cdrom/cd-hold-locl - the hold after a SeekL, its data header (GetlocL) beside its subchannel position (GetlocP) (2026-10-23)
 *
 * WHY. cd-hold-after on the SCPH-1001 (three runs, 2026-10-19): after its first turn the hold kicks back when GetlocP reads 0
 * and lands at -8 (at LBA 1,000), where every later console and this emulator reach +2 and land at -6. The drive's program
 * (SC430916, as the 1001's stand-in; experiments/scratch_holdc1.js) kicks on the DATA header target+1, so on the 1001 the header
 * must run a sector or two ahead of the Q after a kick - and with it the head - where here the Q leads the header by one
 * (STATUS "The SCPH-1001's hold kick asked of its program"). GetlocL answers the last data header the decoder took, GetlocP the
 * last good Q: polled in turn through the hold, the two show which runs ahead, and by how much, before and after a kick.
 *
 * WHAT IT DOES. Init, Demute; at 1x and 2x, origins 1,000 and 130,462 (00:13:25 / 29:01:37 area), from a far seek and from 6
 * sectors before: a SeekL to the origin, then for 24 sectors GetlocL and GetlocP in turn every quarter sector, each one's
 * value printed (less the origin) where it changed, with its time from the INT2 in M cycles. E = an error (GetlocL answers
 * INT5 when no header is held).
 *
 * READ IT LIKE THIS. Per row: speed, origin, the seek's start, then "P t:v" and "L t:v" in time order.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 0x1F; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }
static uint8_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }

static const uint32_t LIMIT = 67737600u;   /* 2 seconds */
static int timeouts;
/* The interrupt wanted (or 5), 0 if none came within LIMIT. A first read that is neither is read again before it is
 * taken for another interrupt (psx-spx "Unstable IRQ Flag polling"; CONSOLE RULE 1). */
static uint8_t waitInt(uint8_t want) {
    const uint32_t t0 = now();
    for (;;) {
        if (now() - t0 > LIMIT) { timeouts++; return 0; }
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f == want || f == 5) return f;
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setmode(uint8_t m) { param(m); simple(0x0E); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}
/* SeekL to lba and wait for it to finish; command write -> INT2 in cycles, 0 on an error or a wait that ran out. */
static uint32_t seekTo(uint32_t lba) {
    setloc(lba);
    const uint32_t t0 = now();
    command(0x15);
    const uint8_t a = waitInt(3);
    if (a != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    const uint8_t r = waitInt(2); const uint32_t t = now() - t0; drain(); ackAll();
    return r == 2 ? t : 0;
}
/* GetlocP's absolute sector, or -1. */
static int32_t getlocP() {
    command(0x11);
    if (waitInt(3) != 3) { drain(); ackAll(); return -1; }
    uint8_t b[8]; int n = 0;
    while (hasResp() && n < 8) b[n++] = resp();
    drain(); ackAll();
    if (n < 8) return -1;
    return (int32_t)((unbcd(b[5]) * 60 + unbcd(b[6])) * 75 + unbcd(b[7])) - 150;
}

/* GetlocL's header sector (absolute), or -1 on an error. */
static int32_t getlocL() {
    command(0x10);
    const uint8_t f = waitInt(3);
    uint8_t b[8]; int n = 0;
    while (hasResp() && n < 8) b[n++] = resp();
    drain(); ackAll();
    if (f != 3 || n < 3) return -1;
    return (int32_t)((unbcd(b[0]) * 60 + unbcd(b[1])) * 75 + unbcd(b[2])) - 150;
}

#define NP 96
static uint32_t pT[NP]; static int32_t pV[NP]; static char pK[NP]; static int np;

int main() {
    printf("\ncdrom/cd-hold-locl\n");
    printf("The hold after a SeekL: GetlocL (the data header) and GetlocP (the Q) in turn, each where it changed: P|L\n");
    printf("time(M from the INT2):value less the origin (E an error). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
    simple(0x0C);
    static const int32_t D[2] = { 99999, -6 };
    static const uint32_t ORG[2] = { 1000, 130462 };
    for (int speed = 0; speed < 2; speed++) {
        setmode(speed ? 0x80 : 0x00);
        const uint32_t sector = speed ? 225792u : 451584u;
        for (int o = 0; o < 2; o++)
            for (int d = 0; d < 2; d++) {
                seekTo(D[d] == 99999 ? (o ? 224850u : 44850u) : (uint32_t)((int32_t)ORG[o] + D[d]));
                const uint32_t st = seekTo(ORG[o]);
                const uint32_t t0 = now();
                np = 0; int32_t lastP = -99999, lastL = -99999;
                while (now() - t0 < 24u * sector && np < NP - 1) {
                    const int32_t l = getlocL();
                    if (l != lastL) { pT[np] = now() - t0; pV[np] = l; pK[np] = 'L'; np++; lastL = l; }
                    const int32_t q = getlocP();
                    if (q != lastP && np < NP) { pT[np] = now() - t0; pV[np] = q; pK[np] = 'P'; np++; lastP = q; }
                    { const uint32_t tw = now(); while (now() - tw < sector / 4) {} }
                }
                printf("%s %6d %c%-5d (%u.%02uM) :", speed ? "2x" : "1x", (int)ORG[o], D[d] == 99999 ? 'F' : D[d] < 0 ? '-' : '+', D[d] == 99999 ? 0 : (int)(D[d] < 0 ? -D[d] : D[d]), (unsigned)(st / 1000000u), (unsigned)((st % 1000000u) / 10000u));
                for (int i = 0; i < np; i++) {
                    printf(" %c%u.%02u:", pK[i], (unsigned)(pT[i] / 1000000u), (unsigned)((pT[i] % 1000000u) / 10000u));
                    if (pV[i] < 0) printf("E"); else printf("%d", (int)(pV[i] - (int32_t)ORG[o]));
                }
                printf("\n");
            }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-hold-locl: Done.\n");
    for (;;) VSync(0);
}
