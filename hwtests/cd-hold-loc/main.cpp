/*
 * cdrom/cd-hold-loc - GetlocP and GetlocL side by side through the hold after a seek (2026-10-18, the seek piece)
 *
 * WHY. cd-getlocl-seek on the SCPH-7001: a second after a SeekL, GetlocL reads a sector 7-13 short of the target -
 * the header last decoded as the head circles its hold - where this core reads the target's. ps1-tests' console reads
 * the target with GetlocL straight after the INT2, and cd-seek-hist's GetlocP the sector BEFORE it: the subchannel and
 * the data headers do not say the same sector there. How the two move through the hold (the kick back, each turn) is
 * what GetlocL in the hold must do - and what the data's lag behind the subchannel is (src/cdservo.js DATA_LAG, whose
 * KNOWN-OPEN is the hold's kick back two sectors early).
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (2 s; CONSOLE RULE 1), Timer 2 at
 * sysclock/8 unwrapped in software. At each speed, a SeekL to LBA 1,000, to 29:01 and to 50:00 (from 10:00): from its
 * INT2, GetlocP then GetlocL every quarter sector for 2.5 turns' worth (40 sectors) - each pair's time, GetlocP's
 * sector and GetlocL's header sector, kept where either changes.
 *
 * READ IT LIKE THIS. Per seek: time(M):P/L pairs, P GetlocP's sector less the target, L GetlocL's (E an error).
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-hold-loc --disc --frames=60000 --quiet=6000`.
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

#define NP 120
static uint32_t pT[NP]; static int32_t pP[NP], pL[NP]; static int np;
static int32_t getlocL() {
    command(0x10);
    if (waitInt(3) != 3) { drain(); ackAll(); return -99999; }
    uint8_t r[8]; int n = 0;
    while (hasResp() && n < 8) r[n++] = resp();
    drain(); ackAll();
    if (n < 8) return -99999;
    return (int32_t)((unbcd(r[0]) * 60 + unbcd(r[1])) * 75 + unbcd(r[2])) - 150;
}

int main() {
    printf("\ncdrom/cd-hold-loc\n");
    printf("GetlocP and GetlocL through the hold after a SeekL: per seek, time(M from the INT2):P/L - GetlocP's sector and\n");
    printf("GetlocL's header less the target (E an error), where either changed. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
    simple(0x0C);
    static const uint32_t TARGETS[3] = { 1000, 130462, 224850 };
    for (int speed = 0; speed < 2; speed++) {
        setmode(speed ? 0x80 : 0x00);
        const uint32_t sector = speed ? 225792u : 451584u;
        for (int tg = 0; tg < 3; tg++) {
            seekTo(44850);
            const uint32_t st = seekTo(TARGETS[tg]);
            const uint32_t t0 = now();
            np = 0; int32_t lp = -1, ll = -1;
            while (now() - t0 < 40u * sector && np < NP) {
                const int32_t P = getlocP(); const int32_t L = getlocL();
                if (P != lp || L != ll) { pT[np] = now() - t0; pP[np] = P; pL[np] = L; np++; lp = P; ll = L; }
                { const uint32_t tw = now(); while (now() - tw < sector / 4) {} }
            }
            printf("%s SeekL %6d (%u.%02uM) :", speed ? "2x" : "1x", (int)TARGETS[tg], (unsigned)(st / 1000000u), (unsigned)((st % 1000000u) / 10000u));
            for (int i = 0; i < np; i++) {
                printf(" %u.%02u:", (unsigned)(pT[i] / 1000000u), (unsigned)((pT[i] % 1000000u) / 10000u));
                if (pP[i] < 0) printf("E"); else printf("%d", (int)(pP[i] - (int32_t)TARGETS[tg]));
                if (pL[i] == -99999) printf("/E"); else printf("/%d", (int)(pL[i] - (int32_t)TARGETS[tg]));
            }
            printf("\n");
        }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-hold-loc: Done.\n");
    for (;;) VSync(0);
}
