/*
 * cdrom/cd-getlocl-seek - GetlocL sent while a SeekL, a SeekP or a ReadN's seek runs: what it answers (2026-10-18)
 *
 * WHY. cd-seek-cmd on the SCPH-7001: a GetlocL 0.2M or 2M into a SeekL answered INT3, every run - where psx-spx says
 * GetlocL "fails (with error code 80h) when the drive is in Seek phase (such like shortly after a new ReadN/ReadS
 * command)", this core's HLE and the drive's own program in it answer INT5. nugget's cdlgetlocl (SCPH-9001): after a
 * SeekP, INT5 03h,80h. Which header an INT3 carries - the sector before the seek, the target's, one on the way - and
 * whether a SeekP or a read's seek differ, decides what the HLE answers.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled psx-spx's way, every wait bounded (2 s; CONSOLE RULE 1), Timer 2 at
 * sysclock/8 unwrapped in software. Single speed. From 29:01 after a SeekL there (so the last header read is known), for
 * SeekL / SeekP / ReadN to 50:00 and to six sectors on, GetlocL D = 0.1M, 0.5M or 2M after the command: its interrupt and
 * bytes; then a Pause (a read) or the seek's end, and GetlocL again.
 *
 * READ IT LIKE THIS. Per row: the interrupt (3 or 5) and the 8 bytes (header mm ss ff mode, subheader file chan sub ci, BCD
 * as read) of the mid-seek GetlocL, then after "|" the GetlocL once the access is over.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-getlocl-seek --disc --frames=60000 --quiet=6000`.
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

static void getlocL(char *out) {
    command(0x10);
    uint8_t f = waitInt(3);
    uint8_t r[8]; int n = 0;
    while (hasResp() && n < 8) r[n++] = resp();
    drain(); ackAll();
    char *o = out; *o++ = (char)(f ? '0' + f : '-'); *o++ = ' ';
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < n; i++) { *o++ = H[r[i] >> 4]; *o++ = H[r[i] & 15]; *o++ = i == 3 ? '/' : ' '; }
    *o = 0;
}

int main() {
    printf("\ncdrom/cd-getlocl-seek\n");
    printf("GetlocL while a seek runs: per row its interrupt and bytes (header / subheader), then | GetlocL after. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
    simple(0x0C);
    setmode(0x00);
    static const uint32_t TARGETS[2] = { 224850, 130468 };
    static const uint32_t DELAY[3] = { 100000, 500000, 2000000 };
    static const uint8_t CMDS[3] = { 0x15, 0x16, 0x06 };
    static const char *CNAME[3] = { "SeekL", "SeekP", "ReadN" };
    char mid[64], after[64];
    for (int c = 0; c < 3; c++)
        for (int tg = 0; tg < 2; tg++)
            for (int dl = 0; dl < 3; dl++) {
                seekTo(130462);
                setloc(TARGETS[tg]);
                command(CMDS[c]);
                if (waitInt(3) != 3) { drain(); ackAll(); printf("%s refused\n", CNAME[c]); continue; }
                drain(); ackAll();
                { const uint32_t tw = now(); while (now() - tw < DELAY[dl]) {} }
                getlocL(mid);
                if (CMDS[c] == 0x06) {   /* a read: let it run a little, then Pause */
                    { const uint32_t tw = now(); while (now() - tw < 3000000u) { uint8_t f = irqFlag() & 7; if (f) { drain(); ackAll(); } } }
                    command(0x09); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
                } else {
                    { const uint32_t tw = now(); while (now() - tw < 33868800u) { uint8_t f = irqFlag() & 7; if (f) { drain(); ackAll(); } } }
                }
                getlocL(after);
                printf("%s 29:01 -> %s at %sM: %s | %s\n", CNAME[c], tg ? "+6   " : "50:00", dl == 0 ? "0.1" : dl == 1 ? "0.5" : "2  ", mid, after);
            }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-getlocl-seek: Done.\n");
    for (;;) VSync(0);
}
