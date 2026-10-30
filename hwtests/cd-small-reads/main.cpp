/*
 * cdrom/cd-small-reads - a new read started a few sectors ahead while the drive is still reading, with the
 * commands, spacing and sector reads of Lagnacure Legend (Japan) (2026-10-06).
 *
 * WHY. Under the sweep's deep schedule that disc crashes or sticks on this emulator's CD model on 3 of 4 draws of
 * M366's core and 3 of 4 of M368's, and never on the SCPH-7501's own drive program (4 refresh phases, and 5
 * schedules shifted to put the presses where the model's fall). Its loader reads small files one after another
 * without a Pause: Setmode A0h, Getstat, Setloc, ReadN, the file's sector (header and 512 words), then only the
 * headers of the sectors after it, and 0.75M-6M cycles later the next file's Setmode, Getstat, Setloc and ReadN -
 * a ReadN with a new Setloc a few sectors ahead of a drive still reading. From that ReadN to the new file's sector
 * (tools/cdtrace.js, both drives, 00:13:29 on):
 *     the drive's program  1.59M-2.08M cycles every time (a turn of the disc and a little)
 *     the model            0.72M-2.25M, mostly 0.72M-1.20M (its seek curve reads forward to a target just ahead)
 * so on the model the loader runs at up to twice the pace, groups its files differently (00:13:32 and 00:13:33 in
 * one read where the program does two), and dies at f2010 through a pointer to code. No console has been asked.
 *
 * WHAT IT DOES. Needs a data disc (The X-Files Disc 1; the games disc, tools/cdgamesdisc.js, is unlocked first -
 * psx-spx's secret unlock). Interrupts off, the CD's flags polled; Timer 2 at sysclock/8 unwrapped in software.
 * Interrupts are taken as the game takes them: acknowledged ~2,400 cycles after they show, an INT1's header (3
 * words by DMA) at +3,400 and the file's sector (512 words) at +3,900. Nothing prints while a sequence runs (over
 * Unirom's cable the TTY is 115,200 baud, ~2,940 cycles a character): the numbers print with the drive paused.
 *   CHAIN  Lagnacure's own reads from 00:13:29 (eleven files), each started as long after the last one's first
 *          sector as the drive program's run started it, with the game's Setmode, Getstat, Setloc, ReadN spacing.
 *          Twice. Per file: ReadN's INT3 and the file's first sector, from the ReadN's write.
 *   AHEAD  reading at double speed, 8 sectors in, then Setloc to the sector k after the one just delivered and
 *          ReadN, written 0.25 or 0.75 of a sector after it - k = 1..12. From the ReadN's write, the target's first
 *          sector (and which came first, if not it).
 *
 * READ IT LIKE THIS. A drive that reads forward to a target just ahead gives the target in about k sector times;
 * one that realigns waits about a turn of the disc whatever k is. The CHAIN rows are the game's own: their pace
 * is what the loader meets.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-small-reads --disc --ntsc --frames=12000 --quiet=6000`.
 * ON THIS CORE AT v0.367, ON M368's BRANCH CORE AND ON THE DRIVE'S OWN PROGRAMS (2026-10-06, The X-Files Disc 1 in):
 *   CHAIN (the file's first sector from its ReadN, both passes, the first file aside):
 *     v0.367 / M368   1.61M-2.25M          SC430940 / SC430944   1.45M-1.94M / 1.45M-1.94M
 *     - at the program's pace the model is ~0.3M slower; in the game's own run (its own pace) it was mostly 0.72M-1.20M.
 *   AHEAD, the target from the ReadN's write, k = 1, 2, 3, 6, 9, 12:
 *     v0.367          0.88M / 2.60M, 0.78M-0.94M, 0.82M-1.07M, 1.47M-1.80M, 2.27M-2.56M, 2.10M-2.16M
 *     M368            0.92M / 2.62M, 0.79M-0.92M, 0.81M-0.86M, 1.52M-1.84M, 2.26M-2.61M, 2.14M-2.17M
 *     SC430940        2.53M-2.64M, 0.20M-0.38M, 0.42M-0.51M, 1.08M-1.28M, 1.76M-1.88M, 2.54M-2.72M
 *     SC430944        2.46M-2.62M, 0.27M-0.28M, 0.43M-0.60M, 1.13M-1.27M, 1.80M-1.95M, 2.58M-2.69M
 *   The programs read forward to the target at a sector time a sector (~225k) with almost nothing on top - but the
 *   very NEXT sector costs them a turn of the disc, as it is already passing. The model puts ~0.5M of seek on every
 *   short one (seekTicks's two-sector minimum and its landing draw), is flat past ~8 (its 0.05 s), and gives the
 *   next sector at once on some phases: Lagnacure's next file often starts at the next sector.
 * AT v0.377.8 (CD rebuild, piece 5), five CD timing draws: k = 1 2.53M-2.68M (a turn, as the consoles), k = 2
 *   0.27M-0.30M at both offsets on every draw - judged from the head as the ReadN runs, as SC430940 judges it (its
 *   servo's head less 2); v0.377.7 took a turn at three quarters on 1 draw of 5, and with piece 5's held sector alone
 *   on 4 of 5 (the held sector's INT1 between the Setloc and the ReadN made k = 2 count as k = 1). The SCPH-9001
 *   reads k = 2 at three quarters in 207,592-252,944 (three runs).
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
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void until(uint32_t t) { while ((int32_t)(now() - t) < 0) { } }

static uint32_t sink[600];
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

/* An interrupt, taken as Lagnacure's handler takes it (its trace on the drive's program): acknowledged ~2,400 after
 * it shows; an INT1's header by DMA at +3,400 and, when it is the file's (wanted), the sector at +3,900. */
static uint32_t evT, evLba; static uint8_t evType, evN, evB[8];
static uint32_t wanted = 0xFFFFFFFF;     /* the file's sector: its data is read once */
static bool take(uint32_t deadline) {
    uint8_t f;
    for (;;) {
        f = irqFlag();
        if (f) break;
        if ((int32_t)(now() - deadline) >= 0) return false;
    }
    const uint32_t t = now();
    evT = t; evType = f; evN = 0; evLba = 0xFFFFFFFF;
    until(t + 2400);
    while (hasResp() && evN < 8) evB[evN++] = resp();
    for (int dn = 0; dn < 32 && hasResp(); dn++) resp();
    ackAll();
    if (f == 1) {
        until(t + 3100);
        want(false); want(true);
        until(t + 3400);
        cdDma(3);
        evLba = lbaOf((const uint8_t *)sink);
        if (evLba == wanted) { until(t + 3900); want(true); cdDma(512); wanted = 0xFFFFFFFF; }
    }
    return true;
}
/* A command; returns the time its first response (3 or 5) was seen, 0 on a timeout. */
static uint32_t issue(uint8_t c, const uint8_t *p = 0, int np = 0) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t tw = now();
    command(c);
    for (;;) {
        if (!take(tw + 140000000)) return 0;
        if (evType == 3 || evType == 5) return evT;
    }
}
static void idle(uint32_t cycles) { const uint32_t end = now() + cycles; while (take(end)) { } }
static void setlocP(uint32_t lba, uint8_t *p) { const uint32_t a = lba + 150; p[0] = bcd(a / 4500); p[1] = bcd((a / 75) % 60); p[2] = bcd(a % 75); }

static void unlock() {
    const uint8_t t22[1] = { 0x22 };
    issue(0x19, t22, 1);
    char reg[12] = { 0 };
    for (int i = 0; i < evN && i < 8; i++) reg[i] = (char)evB[i];
    const char *r = (reg[4] == 'U') ? "of America" : (reg[4] == 'E') ? "(Europe)" : 0;
    printf("drive region: %s - %s\n", reg, r ? "unlocking (a burned disc needs it; a pressed one ignores it)" : "no unlock for it");
    if (!r) return;
    static const char *const S[7] = { 0, "Licensed by", "Sony", "Computer", "Entertainment", 0, 0 };
    for (int k = 0; k < 7; k++) {
        const char *s = (k == 5) ? r : S[k];
        uint8_t p[16]; int n = 0;
        if (s) while (s[n] && n < 16) { p[n] = (uint8_t)s[n]; n++; }
        issue(0x50 + k, p, n);
    }
}

/* One of the game's reads: Setmode A0h, Getstat, Setloc, ReadN with its spacing (from the ack before each: ~1,400,
 * ~5,000, ~1,500); the time from the ReadN's write to its INT3 and to the file's first sector (0 if none in 1 s). */
static uint32_t lastFirst;
static void gameRead(uint32_t lba, uint32_t *r3, uint32_t *r1, uint32_t *got) {
    const uint8_t a0[1] = { 0xA0 };
    uint8_t loc[3]; setlocP(lba, loc);
    issue(0x0E, a0, 1); idle(1400);
    issue(0x01); idle(5000);
    issue(0x02, loc, 3); idle(1500);
    wanted = lba;
    const uint32_t tw = now();
    const uint32_t t3 = issue(0x06);
    *r3 = t3 ? t3 - tw : 0; *r1 = 0; *got = 0xFFFFFFFF;
    const uint32_t end = tw + 33868800;
    while (take(end)) if (evType == 1 && evLba >= lba && evLba < lba + 600) { *r1 = evT - tw; *got = evLba; lastFirst = evT; break; }
}

/* Lagnacure Legend's reads from 00:13:29, each started this long after the last one's first sector (the drive
 * program's run, tools/cdtrace.js; the first after a settle). */
static const uint16_t CHAIN_MSF[11][2] = { {13, 29}, {13, 31}, {13, 32}, {13, 33}, {13, 34}, {13, 35}, {13, 41}, {13, 42},
                                           {13, 47}, {13, 64}, {13, 73} };
static const uint32_t CHAIN_GAP[11] = { 0, 753773, 2511911, 2512640, 2750829, 2327865, 3426457, 2288363, 3405559,
                                        6179769, 4323407 };

int main() {
    printf("\ncdrom/cd-small-reads\n");
    printf("A read started a few sectors ahead while the drive reads, with Lagnacure Legend's commands and spacing. Needs a data disc.\n");
    printf("Cycles at the system clock (1 ms = 33,869), from the ReadN's write. No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                                     /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                      /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    unlock();
    issue(0x0A); idle(140000000 / 4);                     /* Init and its INT2 */
    issue(0x0C);                                          /* Demute */

    for (int pass = 0; pass < 2; pass++) {
        uint32_t r3[11], r1[11], got[11];
        const uint8_t a0[1] = { 0xA0 };
        issue(0x0E, a0, 1); idle(20000000);               /* double speed, settled */
        for (int i = 0; i < 11; i++) {
            const uint32_t lba = (CHAIN_MSF[i][0]) * 75 + CHAIN_MSF[i][1] - 150;
            if (i) until(lastFirst + CHAIN_GAP[i]);
            gameRead(lba, &r3[i], &r1[i], &got[i]);
        }
        idle(1000000); issue(0x09); idle(4000000);         /* Pause; the drive paused, now print */
        printf("CHAIN %d (the file's first sector after its ReadN; ReadN's INT3):\n", pass + 1);
        for (int i = 0; i < 11; i++) {
            const uint32_t lba = (CHAIN_MSF[i][0]) * 75 + CHAIN_MSF[i][1] - 150;
            printf("  00:%02d:%02d  %8d (%s%d)  INT3 %6d\n", CHAIN_MSF[i][0], CHAIN_MSF[i][1], r1[i],
                   got[i] == lba ? "+" : "?", got[i] == 0xFFFFFFFF ? -1 : (int)(got[i] - lba), r3[i]);
        }
    }

    printf("\nAHEAD: reading at 2x, a new Setloc to the sector k after the one just delivered and ReadN, written a\n");
    printf("quarter / three quarters of a sector after it; the target's first sector from the ReadN's write:\n");
    for (int k = 1; k <= 12; k++) {
        uint32_t t[2] = { 0, 0 }, first[2] = { 0, 0 };
        for (int q = 0; q < 2; q++) {
            const uint8_t a0[1] = { 0xA0 };
            uint8_t loc[3]; setlocP(30000, loc);
            issue(0x0E, a0, 1); issue(0x02, loc, 3); issue(0x06);
            uint32_t last = 0, lastL = 0;
            for (int n = 0; n < 8; n++) { if (!take(now() + 140000000)) break; if (evType == 1) { last = evT; lastL = evLba; } }
            until(last + (q ? 169344 : 56448));
            setlocP(lastL + k, loc);
            issue(0x02, loc, 3);
            wanted = lastL + k;
            const uint32_t tw = now();
            issue(0x06);
            const uint32_t end = tw + 33868800;
            while (take(end)) {
                if (evType != 1) continue;
                if (!first[q]) first[q] = evLba - lastL;                /* the first sector after the ReadN, whichever */
                if (evLba == lastL + k) { t[q] = evT - tw; break; }
            }
            issue(0x09); idle(3000000);
        }
        printf("  k %2d : %8d (first +%d)  %8d (first +%d)\n", k, t[0], first[0], t[1], first[1]);
    }

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
