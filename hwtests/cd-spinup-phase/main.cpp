/*
 * cdrom/cd-spinup-phase - the subchannel against the data headers after each spin-up (2026-10-18, the seek piece)
 *
 * WHY. cd-hold-loc on the SCPH-7001: straight after a SeekL's INT2, its run 1 read GetlocP +1 and GetlocL 0 in some
 * seeks where runs 2-3 read -1 and -2 in all; cd-getlocl-seek's run 1 answered a mid-seek GetlocL INT3 where its runs
 * 2-3 answered INT5; cd-seek-map's two-valued rows split by run. Something set when the drive starts holds for a
 * run: if it is the decoder's alignment against the subchannel - the data's lag 0 or 2 sectors after a spin-up - it
 * is src/cdservo.js DATA_LAG's KNOWN-OPEN (the hold's kick back two sectors early on the console).
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (2 s; CONSOLE RULE 1), Timer 2 at
 * sysclock/8 unwrapped in software. Ten times: Stop (and its INT2), a 1 s wait, Init, Demute, Setmode 00h; then twice a
 * SeekL to 10:00 and one to 29:01 - after each, GetlocP and GetlocL at once, and GetlocP every quarter sector for 20
 * sectors (the most it reached before the hold's first kick back). Then a GetlocL 0.2M into a SeekL to 50:00.
 *
 * READ IT LIKE THIS. Per spin-up: for each seek P/L (GetlocP's sector and GetlocL's header less the target, E an error)
 * and ^K (the highest GetlocP before the hold kicked back, less the target); then the mid-seek GetlocL's interrupt.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-spinup-phase --disc --frames=60000 --quiet=10000`.
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

static int32_t getlocL(uint8_t *flag) {
    command(0x10);
    const uint8_t f = waitInt(3); *flag = f;
    uint8_t r[8]; int n = 0;
    while (hasResp() && n < 8) r[n++] = resp();
    drain(); ackAll();
    if (f != 3 || n < 8) return -99999;
    return (int32_t)((unbcd(r[0]) * 60 + unbcd(r[1])) * 75 + unbcd(r[2])) - 150;
}

int main() {
    printf("\ncdrom/cd-spinup-phase\n");
    printf("After each of ten spin-ups: per SeekL P/L (GetlocP / GetlocL less the target) at once and ^K (GetlocP's highest\n");
    printf("before the hold's kick back); then a GetlocL's interrupt 0.2M into a SeekL. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
    static const uint32_t TG[3] = { 44850, 44850, 130462 };
    for (int run = 0; run < 10; run++) {
        command(0x08); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();          /* Stop */
        { const uint32_t tw = now(); while (now() - tw < 33868800u) {} }                    /* 1 s */
        command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();          /* Init */
        simple(0x0C); setmode(0x00);
        printf("spin-up %2d:", run + 1);
        for (int k = 0; k < 3; k++) {
            if (k == 1) seekTo(130462);
            seekTo(TG[k]);
            const int32_t P = getlocP(); uint8_t lf; const int32_t L = getlocL(&lf);
            int32_t top = P, last = P;
            const uint32_t t0 = now();
            while (now() - t0 < 20u * 451584u) {
                { const uint32_t tw = now(); while (now() - tw < 112896u) {} }
                const int32_t q = getlocP();
                if (q < last - 3) break;
                if (q > top) top = q;
                last = q;
            }
            printf("  %d/", (int)(P - (int32_t)TG[k]));
            if (L == -99999) printf("E"); else printf("%d", (int)(L - (int32_t)TG[k]));
            printf(" ^%d", (int)(top - (int32_t)TG[k]));
        }
        seekTo(130462); setloc(224850); command(0x15); waitInt(3); drain(); ackAll();
        { const uint32_t tw = now(); while (now() - tw < 200000u) {} }
        { uint8_t f; getlocL(&f); printf("  mid-seek GetlocL INT%d", (int)f); }
        { const uint32_t tw = now(); while (now() - tw < 33868800u) { uint8_t f = irqFlag() & 7; if (f) { drain(); ackAll(); } } }
        printf("\n");
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-spinup-phase: Done.\n");
    for (;;) VSync(0);
}
