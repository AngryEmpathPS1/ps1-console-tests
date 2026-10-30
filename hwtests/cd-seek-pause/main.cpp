/*
 * cdrom/cd-seek-pause - a Pause sent while a SeekL runs (and refused): does the SeekL still answer its INT2? (2026-10-18)
 *
 * WHY. cd-seek-cmd on the SCPH-7001: every one of twelve commands sent while a SeekL runs takes the seek's INT2 away.
 * Pause was not among them, and a Pause inside a seek is REFUSED (INT5 80h - cd-ack-times, psx-spx). The drive's own
 * program in this machine says a refused Pause takes the INT2 away too; cd-ack-times' own run on the 7001 took 145.6 s
 * where its comment says a minute, as if its pauseAll() waited out 2 s for an INT2 after every refused Pause. Games
 * do this (JoJo's Bizarre Adventure, M179); it decides what the HLE does after a refused Pause.
 *
 * WHAT IT DOES. As cd-seek-cmd (its helpers, its seeks and delays), with Pause, Getstat, Pause again and two controls.
 *
 * READ IT LIKE THIS. Per command and delay: the interrupts after the command in order (3 = its INT3, 5 = INT5, 2 = an
 * INT2) and the INT2's time from the SeekL's write in M cycles (- if none). The control: 2 and the seek's time.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-pause --disc --frames=60000 --quiet=6000`.
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

#define NC 5
static const char *NAME[NC] = { "none", "Pause", "Getstat", "Pause", "none" };
static const int CMD[NC] = { -1, 9, 1, 9, -1 };
static const int NP[NC] = { 0, 0, 0, 0, 0 };
static const uint8_t PARAM[NC][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };

/* After the command: every interrupt for 2 s, in order (up to 6), and when an INT2 came (0 if none). */
static int seq[6]; static int nseq; static uint32_t t2;
static void collect(uint32_t t0) {
    nseq = 0; t2 = 0;
    const uint32_t tw = now();
    while (now() - tw < LIMIT && nseq < 6) {
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f == 1 || f == 4) { const uint8_t g = irqFlag() & 7; if (g) f = g; }   /* a half-set flag: read again */
        if (f == 2 && !t2) t2 = now() - t0;
        seq[nseq++] = f;
        drain(); ackAll();
    }
}

int main() {
    printf("\ncdrom/cd-seek-pause\n");
    printf("A command while a SeekL runs: per command and delay into the seek, the interrupts after it in order (3, 5, 2) and\n");
    printf("an INT2's time from the SeekL's write (M cycles, - if none). Single speed. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    setmode(0x00);
    static const uint32_t TARGETS[2] = { 224850, 130468 };
    static const uint32_t DELAY[2] = { 200000, 2000000 };
    for (int tg = 0; tg < 2; tg++)
        for (int dl = 0; dl < 2; dl++) {
            printf("29:01 -> %s, the command %s into the seek:\n", tg ? "+6" : "50:00", dl ? "2M" : "0.2M");
            for (int c = 0; c < NC; c++) {
                seekTo(130462);
                setloc(TARGETS[tg]);
                const uint32_t t0 = now();
                command(0x15);
                if (waitInt(3) != 3) { drain(); ackAll(); printf("  %-9s SeekL refused\n", NAME[c]); continue; }
                drain(); ackAll();
                { const uint32_t tw = now(); while (now() - tw < DELAY[dl]) {} }
                if (CMD[c] >= 0) {
                    const uint32_t a = TARGETS[tg] + 150;
                    if (CMD[c] == 0x02) { param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); }
                    else for (int i = 0; i < NP[c]; i++) param(PARAM[c][i]);
                    command((uint8_t)CMD[c]);
                }
                collect(t0);
                printf("  %-9s ", NAME[c]);
                for (int i = 0; i < nseq; i++) printf("%d", seq[i]);
                if (!nseq) printf("-");
                if (t2) printf("  INT2 %u.%02u\n", (unsigned)(t2 / 1000000u), (unsigned)((t2 % 1000000u) / 10000u)); else printf("  INT2 -\n");
            }
        }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-seek-pause: Done.\n");
    for (;;) VSync(0);
}
