/*
 * cdrom/cd-ack-window - a sector waiting behind a command's INT3: how late may the host acknowledge that INT3 and
 * still have the drive announce the sector in the same pass, and does a ReadN written just after the acknowledge
 * get in before it? (CD rebuild, 2026-10-07)
 *
 * WHY. The drive's program takes a command and handles a freshly decoded sector in the same pass of its loop; the
 * sector then waits behind the command's INT3, and the same pass looks at the host status once more a little after
 * raising it (S_3F05 in SC430940): acknowledged by then, the sector goes out ~16,000 cycles after the INT3; not, it
 * waits for a later pass, and a new read taken first drops it. cd-race measured the first half on all three consoles
 * (15,864-16,504 cycles, the test acknowledging at once). The look's own time is from the program's instructions:
 * ~2,600 cycles after the INT3's HIFCTL write for Setloc and Setmode, ~2,900 for Getstat (src/psx.js CDP_STEP4_CMD).
 * No console has been asked about an acknowledge that late - and Lagnacure Legend (Japan) acknowledges its Setloc
 * ~2,500 cycles after the IRQ and writes ReadN ~1,500 after that. In time, the old read's next sector comes ~14,600
 * cycles after the ReadN's write and the game takes it for its new file (a wrong header: it Pauses until the drive
 * accepts, ~6 frames, and its loading runs a different course); late, the ReadN is taken first and the sector is
 * dropped. The drive's program as emulated here cannot decide it: it runs in 512-cycle slices, so the IRQ reaches
 * the CPU up to a slice late. This emulator's model sends it (in time); the program's runs never do.
 *
 * WHAT IT DOES. Needs a data disc (The X-Files Disc 1; a burned disc gets psx-spx's secret unlock first). Interrupts
 * off, the CD's flags polled (seen ~60-130 cycles after they rise); Timer 2 at sysclock/8 unwrapped in software.
 * Reading at double speed in mode A0h (whole sectors, as Lagnacure), no data taken, the INT1 period P measured. Per
 * trial: the INT1 phase taken afresh, a command written D cycles before the next INT1 is due (D = 50,000-65,000 in
 * turn, where cd-race found the sector waiting behind the INT3 on every console), its INT3 acknowledged A cycles
 * after it was seen, and the next INT1's time from the INT3.
 *   WINDOW   Getstat, Setmode A0h, Setloc (to a sector the read never reaches; no ReadN follows) - A from 0 to 5,000,
 *            fine around 2,000-3,400, eight trials each.
 *   LAGNA    Setloc to the sector 2 behind or 8 ahead of the one just delivered, its INT3 acknowledged at A, ReadN
 *            1,461 cycles after the acknowledge (Lagnacure's own spacing) - which interrupt comes first after the
 *            ReadN's write, the old read's sector or the ReadN's INT3; and the new read's first sector.
 * Nothing prints while the drive reads: the numbers print with it paused.
 *
 * READ IT LIKE THIS. WINDOW, per trial: the INT3 after the write / the acknowledge after the INT3 / the INT1 after the
 * INT3 (13 when the INT1 came first). The A = 0 rows are the control: most trials put the INT1 15,900-16,500 after the
 * INT3 (the same-pass announce). Where those trials stop, at some A, is the look's time for that command, to compare
 * with ~2,600 / ~2,900. LAGNA, per trial: 1 (the old read's sector came first, with its offset from the target and its
 * time after the Setloc's INT3) or 3 (the ReadN's INT3 first), then the new read's first sector after the ReadN.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-ack-window --disc --ntsc --frames=6000 --quiet=3000`.
 * ON THIS CORE AND THE DRIVE'S OWN PROGRAMS (2026-10-07, The X-Files Disc 1 in, --settle=600; "acknowledge" is from
 * when this program saw the INT3, which its timer read before the flag puts up to ~130 cycles before the raise):
 *   WINDOW, the last acknowledge with the INT1 15,000-17,000 after the INT3, the first without:
 *     SC430940          Getstat 2,816 / 2,888, Setmode 2,696 / 2,792, Setloc 2,688 / 2,792 (one at 3,000)
 *     SC430944, SC430930  the same within a step, each with one or two later (the interrupt-delayed look, below)
 *     v0.377.12         Getstat 2,912 / 2,992, Setmode 2,512 / 2,600, Setloc 2,616 / 2,688
 *     v0.377.13         Getstat 2,808 / 2,888, Setmode 2,512 / 2,592, Setloc 2,512 / 2,608
 *     v0.377.14         Getstat 2,816 / 2,888, Setmode 2,512 / 2,600, Setloc 2,512 / 2,592 (the A = 0 rows' share of
 *                       trials with a sector waiting down to the programs', piece 8b)
 *   LAGNA, the old read's sector before the ReadN's INT3 (among trials whose Setloc INT3 came first), A = 2,400 and on:
 *     SC430940 3/3, then 0 of 19 (A 2,500-3,500); SC430944 4/5, then 0 of 30; SC430930 3/3, then 5 of 23 (4 at 2,500)
 *     v0.377.12 8/8, then 5/8, 2/7, 0/6, 0/6, 2/7, 1/8 - the 2,600-3,500 ones a NEW sector let out at the model's
 *       next pass while the ReadN waited (CD rebuild piece 8 found and closed it)
 *     v0.377.13 5/7, then 0/6, 0/6, 0/8, 2/8 (the delayed look), 0/7, 0/8 (an early build of piece 8; as landed 7/7,
 *       then 1/7, 0/8, 0/8, 0/8, 0/8, 0/7)
 *     v0.377.14 7/7, then 1/6, 0/7, 0/8, 0/8, 0/8, 0/8
 * WHAT THE PROGRAM DOES (scratch_lookprobe.js, the program in the machine driven by exact cycles, every program): the
 * look is S_3F05's HIFSTS read, 2,576 cycles after the INT3's HIFCTL write for every command but Getstat (2,832), and
 * 464 later when the drive's Timer 2 interrupt comes in between; its next command check 11,280 after the raise (Getstat
 * 11,536), with a sector kept the look and 10,544. A console that announces later or earlier than 2,576 moves the edge.
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
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void until(uint32_t t) { while ((int32_t)(now() - t) < 0) { } }

static uint32_t sink[8];
static void cdDma(uint32_t words) {
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x00008000);   /* DMA3 on */
    write32(0x1F8010B0, (uint32_t)sink & 0x1FFFFFFF);
    write32(0x1F8010B4, words);
    write32(0x1F8010B8, 0x11000000);                          /* to RAM, start, trigger */
    for (uint32_t k = 0; (read32(0x1F8010B8) & 0x01000000) && k < 2000000; k++) { tick(); }   /* rule 1: bounded */
}
static uint32_t lbaOf(const uint8_t *h) {
    const uint32_t m = (h[0] >> 4) * 10 + (h[0] & 15), s = (h[1] >> 4) * 10 + (h[1] & 15), f = (h[2] >> 4) * 10 + (h[2] & 15);
    return (m * 60 + s) * 75 + f - 150;
}

/* The next interrupt of any kind and when it was first seen; 0 after ~4 s. */
static uint8_t nextInt(uint32_t *at) {
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        const uint8_t f = irqFlag();
        if (f) { *at = t; return f; }
        if (t - t0 > 140000000) { *at = t; return 0; }
    }
}
static bool tooLong(uint32_t t0) { return now() - t0 > 140000000; }
static uint8_t waitInt(uint8_t w) {
    const uint32_t t0 = now();
    for (;;) {
        uint32_t t;
        const uint8_t f = nextInt(&t);
        if (f == w || f == 5 || f == 0) return f;
        drain(); ackAll();
        if (tooLong(t0)) return 0;
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
static void setlocP(uint32_t lba) { const uint32_t a = lba + 150; param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75)); }

/* An INT1, acknowledged at once; the time it was seen. With hdr, its header read by DMA after (lastLba). */
static uint32_t lastLba;
static uint32_t takeInt1(bool hdr) {
    uint32_t t;
    const uint32_t t0 = now();
    for (;;) {
        const uint8_t f = nextInt(&t);
        drain(); ackAll();
        if (f == 1) {
            if (hdr) { want(false); want(true); cdDma(3); want(false); lastLba = lbaOf((const uint8_t *)sink); }
            return t;
        }
        if (f == 0 || tooLong(t0)) return t;
    }
}

static void unlock() {
    param(0x22); command(0x19);
    uint32_t t; nextInt(&t);
    char reg[12] = { 0 }; int n = 0;
    while (hasResp() && n < 11) reg[n++] = (char)resp();
    drain(); ackAll();
    const char *r = (reg[4] == 'U') ? "of America" : (reg[4] == 'E') ? "(Europe)" : 0;
    printf("drive region: %s - %s\n", reg, r ? "unlocking (a burned disc needs it; a pressed one ignores it)" : "no unlock for it");
    if (!r) return;
    static const char *const S[7] = { 0, "Licensed by", "Sony", "Computer", "Entertainment", 0, 0 };
    for (int k = 0; k < 7; k++) {
        const char *s = (k == 5) ? r : S[k];
        int m = 0;
        if (s) while (s[m] && m < 16) param((uint8_t)s[m++]);
        command(0x50 + k); nextInt(&t); drain(); ackAll();
    }
}

static uint32_t P;
static const int TRIALS = 8;
static const int32_t DS[4] = { 50000, 55000, 60000, 65000 };

/* WINDOW: three commands x the acknowledge delays x eight trials; per trial the INT3 after the write, the
 * acknowledge after the INT3, the INT1 after the INT3 (order 13: the INT1 first - then its time after the write). */
static const uint8_t WCMD[3] = { 0x01, 0x0E, 0x02 };
static const char *const WNAME[3] = { "Getstat", "Setmode A0h", "Setloc" };
static const int NA = 22;
static const uint16_t WA[NA] = { 0, 500, 1000, 1500, 1800, 2000, 2100, 2200, 2300, 2400, 2500, 2600, 2700, 2800, 2900,
                                 3000, 3100, 3200, 3400, 3700, 4000, 5000 };
static int32_t w3[3][NA][TRIALS], wk[3][NA][TRIALS], w1[3][NA][TRIALS];
static uint8_t wo[3][NA][TRIALS];

static void windowTrial(int c, int a, int i) {
    takeInt1(false);
    const uint32_t t1 = takeInt1(false);
    until(t1 + P - DS[i & 3]);
    if (WCMD[c] == 0x0E) param(0xA0);
    if (WCMD[c] == 0x02) setlocP(60000);
    const uint32_t tw = now();
    command(WCMD[c]);
    uint32_t ta, tb;
    const uint8_t fa = nextInt(&ta);
    if (fa == 3) {
        until(ta + WA[a]);
        drain(); ackAll();
        const uint32_t tk = now();
        uint8_t fb;
        for (;;) { fb = nextInt(&tb); drain(); ackAll(); if (fb == 1 || fb == 0 || tooLong(tw)) break; }
        wo[c][a][i] = 31; w3[c][a][i] = ta - tw; wk[c][a][i] = tk - ta; w1[c][a][i] = fb == 1 ? (int32_t)(tb - ta) : -1;
    } else {
        drain(); ackAll();
        uint8_t fb;
        for (;;) { fb = nextInt(&tb); drain(); ackAll(); if (fb == 3 || fb == 5 || fb == 0 || tooLong(tw)) break; }
        wo[c][a][i] = fa * 10 + fb; w3[c][a][i] = tb - tw; wk[c][a][i] = 0; w1[c][a][i] = ta - tw;
    }
}

/* LAGNA: per acknowledge delay, eight trials: the first interrupt after the ReadN's write (1 or 3), its time after the
 * Setloc's INT3, the old sector's offset from the target when it is an INT1; the new read's first sector's offset and
 * its time after the ReadN's write. */
static const int NL = 12;
static const uint16_t LA[NL] = { 0, 1500, 2000, 2200, 2300, 2400, 2500, 2600, 2700, 2800, 3000, 3500 };
static int32_t lFirstT[NL][TRIALS], lOld[NL][TRIALS], lNewT[NL][TRIALS], lNew[NL][TRIALS], lAck[NL][TRIALS];
static uint8_t lFirst[NL][TRIALS], lOrder[NL][TRIALS];

static void lagnaTrial(int a, int i) {
    takeInt1(true);
    const uint32_t t1 = takeInt1(true);
    const uint32_t target = lastLba + ((i & 1) ? 8 : (uint32_t)-2);
    until(t1 + P - DS[i & 3]);
    setlocP(target);
    const uint32_t tw = now();
    command(0x02);
    uint32_t ta, t;
    const uint8_t fa = nextInt(&ta);
    lOrder[a][i] = fa;
    if (fa != 3) { drain(); ackAll(); waitInt(3); ta = now(); }
    until(ta + LA[a]);
    drain(); ackAll();
    const uint32_t tk = now();
    lAck[a][i] = tk - ta;
    until(tk + 1461);
    const uint32_t tr = now();
    command(0x06);
    lFirst[a][i] = 0; lOld[a][i] = 0; lNew[a][i] = 0x7FFF; lNewT[a][i] = 0; lFirstT[a][i] = 0;
    bool acked = false;
    for (;;) {
        const uint8_t f = nextInt(&t);
        drain(); ackAll();
        if (f == 0 || tooLong(tr)) break;
        if (!lFirst[a][i]) { lFirst[a][i] = f; lFirstT[a][i] = t - ta; }
        if (f == 3 || f == 5) { acked = true; continue; }
        if (f == 1) {
            want(false); want(true); cdDma(3); want(false);
            const uint32_t lba = lbaOf((const uint8_t *)sink);
            if (!acked) { lOld[a][i] = (int32_t)(lba - target); continue; }
            lNew[a][i] = (int32_t)(lba - target); lNewT[a][i] = t - tr;
            break;
        }
    }
    (void)tw;
}

int main() {
    printf("\ncdrom/cd-ack-window\n");
    printf("A sector waiting behind a command's INT3: how late may the acknowledge come for the drive to announce it in the\n");
    printf("same pass, and does a ReadN written just after the acknowledge get in first? Needs a data disc.\n");
    printf("Cycles at the system clock (1 ms = 33,869). No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                                     /* CD interrupts masked: polled */
    ackAll();
    const uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    unlock();
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    param(0xA0); simple(0x0E);                                                      /* double speed, whole sectors */
    setlocP(1000); simple(0x02);
    command(0x06); waitInt(3); drain(); ackAll();                                   /* ReadN */
    for (int i = 0; i < 6; i++) takeInt1(false);
    const uint32_t first = takeInt1(false);
    uint32_t last = first;
    for (int i = 0; i < 16; i++) last = takeInt1(false);
    P = (last - first) / 16;

    for (int c = 0; c < 3; c++) for (int a = 0; a < NA; a++) for (int i = 0; i < TRIALS; i++) windowTrial(c, a, i);
    for (int a = 0; a < NL; a++) for (int i = 0; i < TRIALS; i++) lagnaTrial(a, i);
    simple(0x09); waitInt(2); drain(); ackAll();                                    /* Pause, then print */

    printf("2x, mode A0h: INT1 period %d cycles\n\n", P);
    for (int c = 0; c < 3; c++) {
        printf("WINDOW %s (per trial: INT3 after the write / acknowledge after the INT3 / INT1 after the INT3):\n", WNAME[c]);
        for (int a = 0; a < NA; a++) {
            int same = 0, n31 = 0;
            for (int i = 0; i < TRIALS; i++) if (wo[c][a][i] == 31) { n31++; if (w1[c][a][i] >= 15500 && w1[c][a][i] <= 16900) same++; }
            printf("  A=%5d  same pass %d of %d :", WA[a], same, n31);
            for (int i = 0; i < TRIALS; i++) {
                if (wo[c][a][i] == 31) printf(" %d/%d/%d", w3[c][a][i], wk[c][a][i], w1[c][a][i]);
                else printf(" %d:%d/%d", wo[c][a][i], w1[c][a][i], w3[c][a][i]);
            }
            printf("\n");
        }
        printf("\n");
    }
    printf("LAGNA Setloc (2 behind / 8 ahead in turn), its INT3 acknowledged at A, ReadN 1,461 after the acknowledge.\n");
    printf("Per trial: the first interrupt after the ReadN (1 = the old read's sector [its offset from the target] /\n");
    printf("3 = the ReadN's INT3), its time after the Setloc's INT3; then the new read's first sector [offset] after the ReadN:\n");
    for (int a = 0; a < NL; a++) {
        int old = 0;
        for (int i = 0; i < TRIALS; i++) if (lFirst[a][i] == 1) old++;
        printf("  A=%5d  old sector first %d of %d :", LA[a], old, TRIALS);
        for (int i = 0; i < TRIALS; i++) {
            if (lOrder[a][i] != 3) printf(" (%d)", lOrder[a][i]);
            if (lFirst[a][i] == 1) printf(" 1[%d]@%d", lOld[a][i], lFirstT[a][i]);
            else printf(" %d@%d", lFirst[a][i], lFirstT[a][i]);
            printf(">[%d]%d", lNew[a][i], lNewT[a][i]);
        }
        printf("\n");
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
