/*
 * cdrom/cd-speed-seek3 - when the sled starts and when the drive stops seeking, after a speed change (2026-11-16). Any pressed data
 * disc whose data reaches 06:45 (Doom (USA), The X-Files).
 *
 * WHY. On the second SCPH-5501 a long inward move after a speed change ends 0.6-0.9M cycles sooner than the same unit's plain move
 * (hwtests/cd-speed-seek2, STATUS 2026-11-16), where this emulator's drive program (SC430930 on cdservo.js) takes the same 4.1M for
 * both: it kicks the sled ~0.4M after the frames come back, drives 59 ms, brakes 35 ms, and waits for the PLL to settle 20 ms before
 * the landing's first Q. A 10 ms settle fits Doom's level load on that unit and the SCPH-7501's cd-sled, but not the same 5501's
 * cd-sled nor the 9001's (experiments/doomload/NOTES.md section 16): the settle is assumed (LOCK_SETTLE_S), and what each unit does
 * between the frames' return and the landing has not been measured. cd-speed-seek2 polls GetlocP every quarter sector, which shows
 * only the landing - GetlocP answers the last GOOD Q, so it says nothing while the head is off the track.
 *
 * WHAT IT DOES. cd-seek-long's helpers (interrupts off, the flags polled, every wait bounded, Timer 2 at sysclock/8 unwrapped). Per
 * cell, five times: Setmode M0, 60 ms, SeekL to the origin, 1.2 s in the hold; [Setmode M1 and] at once SeekL to the target; from its
 * INT3 to its INT2, GetlocP and Getstat back to back, alternately - each answer kept where it differs from the last of its kind (a
 * sector step of more than two, a new stat byte, an error), with its time. Then the same SeekL again with no polling (the control:
 * does polling move the seek?). The cells, Doom's level read 5 (25,475 sectors) in both directions, with and without a change:
 *   CI  06:45:07 (30,232) single -> double, to 01:05:32 (4,757)    NI  the same at double speed, no change
 *   CO  01:05:32 double -> single, to 06:45:07                      NO  the same at single speed, no change
 *
 * READ IT LIKE THIS. A row per seek: the cell, the control's and the polled seek's write-to-INT2 in M cycles, then the kept answers in
 * time order - P<time>:<sector less the target> for GetlocP (E an error), S<time>:<stat hex> for Getstat; | marks the INT2. Getstat's
 * bit 6 (40h) is Seeking: its first S..:42 after the change is when the drive began the move, its last before S..:02 when it ended,
 * and the first P near the target is the landing's first good Q. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-speed-seek3 --disc=doom --ntsc --frames=14000 --quiet=12000 --settle=600`.
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

#define NC 4
#define NREP 5
#define NP 200
struct Cell { const char *name; uint8_t m0; int flip; uint8_t m1; uint32_t o, t; };
static const Cell CELL[NC] = {
    { "CI", 0x00, 1, 0x80, 30232, 4757 }, { "NI", 0x80, 0, 0x80, 30232, 4757 },
    { "CO", 0x80, 1, 0x00, 4757, 30232 }, { "NO", 0x00, 0, 0x00, 4757, 30232 },
};
static uint32_t kT[NP]; static int32_t kV[NP]; static char kK[NP]; static int nk;
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* One command's answer while a seek may end: 3 (the bytes in r[], n set), 5 an error, 2 if the seek's INT2 came first (*t2 set),
 * 0 a wait that ran out. */
static int answer(uint8_t *r, int *n, uint32_t t0, uint32_t *t2) {
    const uint32_t tw = now();
    for (;;) {
        if (now() - tw > LIMIT) { timeouts++; return 0; }
        uint8_t f = irqFlag() & 7;
        if (!f) continue;
        if (f != 2 && f != 3 && f != 5) { f = irqFlag() & 7; if (f != 2 && f != 3 && f != 5) { drain(); ackAll(); continue; } }
        if (f == 2) { if (!*t2) *t2 = now() - t0; drain(); ackAll(); continue; }
        if (f == 5) { drain(); ackAll(); return 5; }
        *n = 0; while (hasResp() && *n < 8) r[(*n)++] = resp();
        drain(); ackAll();
        return 3;
    }
}
static void keep(char k, uint32_t t, int32_t v) { if (nk < NP) { kK[nk] = k; kT[nk] = t; kV[nk] = v; nk++; } }

/* SeekL to lba, GetlocP and Getstat alternately from its INT3 to its INT2; the changes kept. */
static uint32_t seekPolled(uint32_t lba) {
    setloc(lba);
    const uint32_t t0 = now();
    command(0x15);
    if (waitInt(3) != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    uint32_t t2 = 0; int32_t lastP = -999999; int lastS = -1; nk = 0; int which = 0;
    while (!t2 && now() - t0 < LIMIT) {
        uint8_t r[8]; int n = 0;
        command(which ? 0x01 : 0x11);
        const int a = answer(r, &n, t0, &t2);
        const uint32_t t = now() - t0;
        if (!a) break;
        if (which == 0) {
            if (a == 5 || n < 8) { if (lastP != -1) keep('P', t, -1); lastP = -1; }
            else { const int32_t s = (int32_t)((unbcd(r[5]) * 60 + unbcd(r[6])) * 75 + unbcd(r[7])) - 150 - (int32_t)lba;
                   if (lastP == -999999 || lastP == -1 || s - lastP > 2 || lastP - s > 2) keep('P', t, s); lastP = s; }
        } else {
            const int st = (a == 3 && n >= 1) ? r[0] : 0x100;
            if (st != lastS) keep('S', t, st); lastS = st;
        }
        which ^= 1;
    }
    return t2;
}

int main() {
    printf("\ncdrom/cd-speed-seek3\n");
    printf("When the sled starts and the drive stops seeking after a speed change: per seek the cell, the control's and the polled\n");
    printf("seek's write-to-INT2 (M cycles), then P<time>:<sector less target> (GetlocP) and S<time>:<stat> (Getstat) where they\n");
    printf("changed; | marks the INT2. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    for (int c = 0; c < NC; c++) {
        const Cell &ce = CELL[c];
        for (int rep = 0; rep < NREP; rep++) {
            /* the polled seek */
            setmode(ce.m0); idle(2032128u);
            seekTo(ce.o); idle(40642560u);
            if (ce.flip) setmode(ce.m1);
            const uint32_t pol = seekPolled(ce.t);
            /* the control: the same, unpolled */
            setmode(ce.m0); idle(2032128u);
            seekTo(ce.o); idle(40642560u);
            if (ce.flip) setmode(ce.m1);
            const uint32_t ctl = seekTo(ce.t);
            printf("%s rep %d: %u.%02u %u.%02u :", ce.name, rep + 1, (unsigned)(ctl / 1000000u), (unsigned)((ctl % 1000000u) / 10000u),
                   (unsigned)(pol / 1000000u), (unsigned)((pol % 1000000u) / 10000u));
            int marked = 0;
            for (int i = 0; i < nk; i++) {
                if (!marked && pol && kT[i] > pol) { printf(" |"); marked = 1; }
                const unsigned ms = (unsigned)(kT[i] / 1000000u), cs = (unsigned)((kT[i] % 1000000u) / 10000u);
                if (kK[i] == 'P') { if (kV[i] == -1) printf(" P%u.%02u:E", ms, cs); else printf(" P%u.%02u:%d", ms, cs, (int)kV[i]); }
                else { if (kV[i] > 0xFF) printf(" S%u.%02u:E", ms, cs); else printf(" S%u.%02u:%02X", ms, cs, (unsigned)kV[i]); }
            }
            if (!marked) printf(" |");
            printf("\n");
        }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-speed-seek3: Done.\n");
    for (;;) VSync(0);
}
