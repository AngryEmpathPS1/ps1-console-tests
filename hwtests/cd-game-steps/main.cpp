/*
 * cdrom/cd-game-steps - three games' own CD steps, with their spacing, where the emulator's drive and the drive's
 * own program answer differently (2026-10-07). On The X-Files Disc 1, the console-day disc.
 *
 * WHY. tools/cdtrace.js recorded each game's conversation on the model and on the SCPH-7501's program (SC430940),
 * and scratch/cdreplay.js replayed the PROGRAM's conversation on the model - the same commands at the same delays
 * after the same interrupts - so where the two answer differently is a per-command difference, not the game's
 * reaction to one. Three kinds of step stand out, each a whole disc's worth of games' pattern:
 *   FILE SEARCH, Nightmare Creatures (USA)'s libcd: read one sector, Pause, SeekL two to four sectors on, ReadN,
 *     Pause... Its SeekLs: the program 0.61M-1.85M cycles, the model 0.2M-2.3M, as the head sits somewhere else
 *     after a Pause taken right after a read's FIRST INT1 (the program: its INT2 1.6 sectors before the Setloc
 *     position, the hold a sector before it; the model: past the sector read). cd-pause times that Pause; nothing
 *     says where it leaves the head.
 *   MEDIUM SEEKS, Omiai Commando (Japan): back and forth over 3,000-4,000 sectors around 07:00, ReadN to the first
 *     sector: the program 3.8M-5.7M cycles, the model 4.6M-8.8M (300-odd tracks by 2N jumps, which cd-seek's rows
 *     time only at the hub and at 29:00). On the model the game dies in the stream after one of them on 2 draws of 4.
 *   BACK A FEW, Lagnacure Legend (Japan): read some sectors, Pause, Getstat, Setloc 1-12 sectors BEHIND the last one
 *     read, ReadN. cd-head's RESUME reads the next sector only.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at the system clock unwrapped in software (cd-head's
 * clock and helpers). Double speed throughout, each wait the game's own (cycles after the interrupt it followed):
 *   FILE SEARCH x6: Setmode 80h; Setloc 00:02:16, ReadN, the INT1, Pause 40,000 after it; then three times
 *     Setloc (the next file: 00:02:18, :22, :23), SeekL, Setmode 80h, ReadN, the INT1, Pause - with Nightmare
 *     Creatures' spacing (Setloc 18,000 after the Pause's INT2 or a Getstat 350,000 / 170,000 after it).
 *   MEDIUM SEEKS x3: six of Omiai Commando's: a read of n sectors at A, Pause 40,000 after the last INT1, and W after
 *     its INT2 Setloc B and ReadN - the first INT1 after the ReadN's write, and its sector.
 *   BACK A FEW x2: from 00:08:00 and 30:00:00, five sectors read and a Pause 6,000 after the last INT1; Getstat
 *     24,000 after its INT2; Setloc b back from the last (b = 1, 2, 3, 5, 8, 12) 40,000 after the Getstat's INT3;
 *     ReadN 30,000 after the Setloc's INT3; the first INT1 and its sector.
 * Nothing prints while a part runs (Unirom's TTY is 115,200 baud, ~2,940 cycles a character).
 *
 * READ IT LIKE THIS. Every number is cycles from the write of the command named to the interrupt named. The rows
 * that decide: FILE SEARCH's SeekL INT2s (after the first-INT1 Pause: where the head waits), MEDIUM SEEKS' first INT1
 * (2N-jump seeks at 07:00), BACK A FEW's first INT1 by b (where a Pause leaves the head, read backward).
 *
 * ON THIS CORE AT v0.377.5 (2026-10-07, The X-Files Disc 1; v0.377.4 the same to a few percent but each ReadN after
 * its SeekL, 0.64M-0.66M, and the fourth medium seek, 8.61M) / ON THE DRIVE'S OWN PROGRAMS SC430940 and SC430944
 * (PSX_CD_FIRMWARE), cycles:
 *   FILE SEARCH  first-INT1 Pause 1.31M (1.99M when its acknowledge comes late) / 1.05M-1.09M (consoles' cd-pause:
 *                1.33M-1.41M from its INT3); SeekL :18 0.20M-0.22M / 0.87M-0.94M; :22 2.26M / 0.99M-1.09M; :23
 *                1.86M-1.91M / 0.66M-0.71M; each ReadN after its SeekL 0.31M-0.42M / 0.37M-0.43M, the last 0.19M or a turn
 *                (2.23M-2.28M) on the programs.
 *   MEDIUM SEEKS 8.88M / 3.1M-5.6M; 8.68M / 5.05M-5.11M; 8.81M / 4.94M-4.98M; 6.31M-6.34M / 4.03M-4.11M; 4.20M /
 *                3.63M-3.69M; 4.56M-4.58M / 1.55M-1.58M. The last, 239 sectors on from a Pause: both drives make
 *                a 2N jump of 18 tracks and one single jump, and the emulator's single jump lands too close to
 *                the target to read it after the settle CD_PLAN.ej (2.4 sectors, fitted to cd-seek) - the head
 *                is past it, and a jump back and a turn follow. The servo the programs run on settles sooner.
 *   BACK A FEW   alike on all three to a few percent at every b, but b = 5 from 00:08:00 (3.23M / 0.90M-1.09M) and
 *                b = 12 from 30:00:00 (4.55M or 0.95M / 0.77M-0.96M).
 * THE RULE: run it here first: `node hwtests/run.js cd-game-steps --disc --ntsc --frames=30000 --quiet=6000 --settle=600`.
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
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }
static uint8_t data() { idx(0); return *CD2; }
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static uint32_t unbcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idleUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) {} }

/* The next interrupt: which, when, and for an INT1 with `hdr` its sector's header; drained and acknowledged. */
struct Got { uint8_t flag, b[4]; uint32_t t; };
static Got take(uint32_t limit = MS(3000), bool hdr = false) {
    Got g; g.flag = 0; g.t = 0;
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        /* An old board's flag can read 1 before 3, or 4 before 5 (psx-spx "Unstable IRQ Flag polling"; the SCPH-1001's
         * cd-irq-glitch: once in 3,000): such a first read is read again, or the INT3 is acknowledged as an INT1 and a
         * later wait for it never ends (cd-ack-times hung the 1001 so, 2026-10-09). The time is the first read's. */
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    drain();
    if (g.flag == 1 && hdr) { want(true); g.b[1] = data(); g.b[2] = data(); g.b[3] = data(); want(false); }
    ackAll();
    return g;
}
static Got takeKind(uint8_t k, uint32_t limit = MS(3000), bool hdr = false) {
    for (;;) { const Got g = take(limit, hdr); if (!g.flag || g.flag == k || g.flag == 5) return g; }
}
static int lbaOf(uint8_t m, uint8_t s, uint8_t f) { return (int)((unbcd(m) * 60 + unbcd(s)) * 75 + unbcd(f)) - 150; }
static uint32_t at(uint8_t m, uint8_t s, uint8_t f) { return (uint32_t)((m * 60 + s) * 75 + f - 150); }
static void msf(int lba) { if (lba < 0) { printf("--:--:--"); return; } const int a = lba + 150; printf("%02d:%02d:%02d", a / 4500, (a / 75) % 60, a % 75); }

/* A command written now (its parameters first), its INT3 taken; returns the write's time. */
static uint32_t cmd(uint8_t c, int np = 0, uint8_t p0 = 0, uint8_t p1 = 0, uint8_t p2 = 0) {
    ackAll();
    if (np > 0) param(p0);
    if (np > 1) param(p1);
    if (np > 2) param(p2);
    const uint32_t t = now();
    command(c);
    return t;
}
static uint32_t setlocW(uint32_t lba) { const uint32_t a = lba + 150; return cmd(0x02, 3, bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75)); }

/* FILE SEARCH: one row per time - the four Pauses' INT2, the three SeekLs' INT2, the three ReadNs' first INT1. */
struct Fs { uint32_t p[4], s[3], r[3]; int lba[3]; };
static Fs fs[6];
/* MEDIUM SEEKS */
struct Ms { uint8_t a[3]; int n; uint8_t b[3]; uint32_t w; };
static const Ms MSK[6] = {
    { {7, 34, 27}, 5, {6, 44, 25}, 1445000 }, { {6, 40, 62}, 1, {7, 27, 55}, 3048000 }, { {7, 27, 55}, 5, {6, 40, 56}, 3000000 },
    { {7, 23, 62}, 47, {6, 40, 22}, 3000000 }, { {7, 34, 22}, 5, {7, 23, 62}, 3000000 }, { {7, 28, 26}, 35, {7, 32, 1}, 1117000 } };
static uint32_t msT[6][3]; static int msL[6][3];
/* BACK A FEW */
static const int BACK[6] = { 1, 2, 3, 5, 8, 12 };
static uint32_t bkT[2][6][2]; static int bkL[2][6][2];

int main() {
    printf("\ncdrom/cd-game-steps\n");
    printf("Three games' CD steps with their spacing: a file search, medium seeks, a read back a few sectors.\n");
    printf("Needs The X-Files Disc 1 (or any data disc of 31+ minutes). About a minute; nothing prints until the end.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    cmd(0x0A); take(); takeKind(2, MS(5000));                  /* Init */
    cmd(0x0E, 1, 0x80); takeKind(3);
    idleUntil(now() + MS(1000));

    for (int k = 0; k < 6; k++) {                               /* FILE SEARCH */
        Fs &r = fs[k];
        setlocW(16); takeKind(3);
        cmd(0x06); takeKind(3); Got g = takeKind(1);
        idleUntil(g.t + 40000);
        uint32_t tw = cmd(0x09); takeKind(3); g = takeKind(2); r.p[0] = g.t - tw;
        static const uint32_t NEXT[3] = { 18, 22, 23 };
        for (int f = 0; f < 3; f++) {
            if (f == 0) idleUntil(g.t + 18000);
            else { idleUntil(g.t + (f == 1 ? 350000 : 170000)); cmd(0x01); g = takeKind(3); idleUntil(g.t + 16000); }
            setlocW(NEXT[f]); g = takeKind(3);
            idleUntil(g.t + 6000);
            tw = cmd(0x15); takeKind(3); g = takeKind(2, MS(5000)); r.s[f] = g.t - tw;
            idleUntil(g.t + 14000);
            cmd(0x0E, 1, 0x80); g = takeKind(3);
            idleUntil(g.t + 6000);
            tw = cmd(0x06); takeKind(3); g = takeKind(1, MS(3000), true); r.r[f] = g.t - tw;
            r.lba[f] = g.flag == 1 ? lbaOf(g.b[1], g.b[2], g.b[3]) : -1;
            idleUntil(g.t + 40000);
            tw = cmd(0x09); takeKind(3); g = takeKind(2); r.p[f + 1] = g.t - tw;
        }
        idleUntil(now() + MS(200));
    }

    cmd(0x0E, 1, 0xA0); takeKind(3);                            /* MEDIUM SEEKS */
    for (int rep = 0; rep < 3; rep++) for (int k = 0; k < 6; k++) {
        const Ms &m = MSK[k];
        setlocW(at(m.a[0], m.a[1], m.a[2])); takeKind(3);
        cmd(0x06); takeKind(3);
        Got g; for (int i = 0; i < m.n; i++) g = takeKind(1);
        idleUntil(g.t + 40000);
        cmd(0x09); takeKind(3); g = takeKind(2);
        idleUntil(g.t + m.w);
        setlocW(at(m.b[0], m.b[1], m.b[2])); g = takeKind(3);
        idleUntil(g.t + 40000);
        const uint32_t tw = cmd(0x06); takeKind(3); g = takeKind(1, MS(3000), true);
        msT[k][rep] = g.t - tw; msL[k][rep] = g.flag == 1 ? lbaOf(g.b[1], g.b[2], g.b[3]) : -1;
        idleUntil(g.t + 40000);
        cmd(0x09); takeKind(3); takeKind(2);
    }

    for (int pos = 0; pos < 2; pos++) for (int rep = 0; rep < 2; rep++) for (int k = 0; k < 6; k++) {   /* BACK A FEW */
        const uint32_t A = pos ? 30 * 4500 - 150 : 450;
        setlocW(A); takeKind(3);
        cmd(0x06); takeKind(3);
        Got g; for (int i = 0; i < 5; i++) g = takeKind(1);
        idleUntil(g.t + 6000);
        cmd(0x09); takeKind(3); g = takeKind(2);
        idleUntil(g.t + 24000);
        cmd(0x01); g = takeKind(3);
        idleUntil(g.t + 40000);
        setlocW(A + 4 - BACK[k]); g = takeKind(3);
        idleUntil(g.t + 30000);
        const uint32_t tw = cmd(0x06); takeKind(3); g = takeKind(1, MS(3000), true);
        bkT[pos][k][rep] = g.t - tw; bkL[pos][k][rep] = g.flag == 1 ? lbaOf(g.b[1], g.b[2], g.b[3]) : -1;
        idleUntil(g.t + 6000);
        cmd(0x09); takeKind(3); takeKind(2);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("FILE SEARCH (Nightmare Creatures): read 00:02:16 and Pause, then three times Setloc / SeekL / ReadN / Pause\n");
    printf("  (00:02:18, :22, :23); cycles: Pause INT2 | SeekL INT2  ReadN INT1  Pause INT2 | ...\n");
    for (int k = 0; k < 6; k++) {                               /* mode 80h hands the data, so no header to show */
        const Fs &r = fs[k];
        printf("  %7u", (unsigned)r.p[0]);
        for (int f = 0; f < 3; f++) printf(" | %7u %7u %7u", (unsigned)r.s[f], (unsigned)r.r[f], (unsigned)r.p[f + 1]);
        printf("\n");
    }
    printf("MEDIUM SEEKS (Omiai Commando): n sectors read at A, Pause, W after its INT2 Setloc B + ReadN; the first INT1:\n");
    for (int k = 0; k < 6; k++) {
        const Ms &m = MSK[k];
        printf("  "); msf(at(m.a[0], m.a[1], m.a[2])); printf(" x%-2d -> ", m.n); msf(at(m.b[0], m.b[1], m.b[2]));
        printf(" W %7u:", (unsigned)m.w);
        for (int rep = 0; rep < 3; rep++) { printf(" %8u ", (unsigned)msT[k][rep]); msf(msL[k][rep]); }
        printf("\n");
    }
    printf("BACK A FEW (Lagnacure Legend): five sectors read, Pause, Getstat, Setloc b back from the last, ReadN; the first INT1:\n");
    for (int pos = 0; pos < 2; pos++) {
        printf("  from %s\n", pos ? "30:00:00" : "00:08:00");
        for (int k = 0; k < 6; k++) {
            printf("    b %2d:", BACK[k]);
            for (int rep = 0; rep < 2; rep++) { printf(" %8u ", (unsigned)bkT[pos][k][rep]); msf(bkL[pos][k][rep]); }
            printf("\n");
        }
    }
    printf("\ncdrom/cd-game-steps: Done.\n");
    for (;;) VSync(0);
}
