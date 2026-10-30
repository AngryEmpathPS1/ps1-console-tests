/*
 * cdrom/cd-init-after - Init right after a double-speed read is Paused far out on the disc, with the commands,
 * spacing and sector reads of Famitsu Wave 10gatsu-gou Vol. 15 (Japan) (2026-10-06).
 *
 * WHY. Under the sweep's deep schedule that disc freezes on its Winning Eleven title on 1 or 2 of 16 CD timing
 * draws of EVERY core this emulator has had (M366, M367, M368 alike; DuckStation 0 of 8 press shifts). Re-seeding
 * the model's CD timing at a frame of a freezing run (scratch_fwreseed.js, 8 seeds a frame) saves it from f3050 and
 * from f3209, and never from f3214: the freeze is decided inside f3209-f3213, and the one CD timing draw made there
 * is the landing of the seek inside an Init - 24,217 cycles on a good draw, 175,530 on the freezing one. Its own CD
 * code, traced (tools/cdtrace.js), around it:
 *     Setloc 09:00:55, SeekL; Setmode 80h, ReadN; 150 sectors, each INT1 acknowledged ~1,900 cycles after it shows,
 *     its want-data set at +5,300 and 512 words read by DMA at +6,800; Pause ~42,000 after the last (09:02:55, its
 *     file's end); Getstat ~281,000 after the Pause's INT2; Init ~2,000 after the Getstat's answer
 *   and on the model that Init's INT2 comes ~33.6M cycles (1 s) later: a speed change to single speed and a seek to
 *   the disc's start. Then Demute; 6.7M cycles on, Getstat and Init again (INT2 ~0.48M after); Demute, Setloc
 *   00:02:16, Setmode A0h, ReadN - its first sector 22.3M later (the speed change back). Where the first Init's INT2
 *   falls against the frame is what the draw moved. cd-init times Init from eleven states; none is this one (a
 *   Pause after a double-speed read nine minutes out). This one decides whether the freeze is a console's too: a
 *   console whose Init spreads by more than ~5 ms here meets it as the model does.
 *
 * WHAT IT DOES. Needs a disc at least ten minutes long (The X-Files Disc 1 on console day; the games disc,
 * tools/cdgamesdisc.js, has Famitsu's own layout there, and a burned disc is unlocked first - psx-spx "Secret
 * Unlock Commands"). Interrupts off, the CD's flags polled; Timer 2 at sysclock/8 unwrapped in software. Six times:
 * the sequence above with the game's spacing; each time, the Pause's INT3 and INT2, the first Init's INT3 and INT2,
 * the second Init's, and the ReadN's INT3 and first INT1 with its header. The first time starts from single speed
 * (after this program's own Init), the others as the game's did, from the last time's double-speed ReadN.
 * Nothing prints while a time runs: over Unirom's cable the TTY goes out at 115,200 baud, ~2,940 cycles a
 * character, and a line printed inside the sequence would move the game's spacing.
 *
 * READ IT LIKE THIS. One row per time, cycles: "Pause 3 +x 2 +y | Init 3 +x 2 +y | Init 3 +x 2 +y | ReadN 3 +x 1 +y".
 * The first Init's INT2 (from its write) is the number the freeze turns on: its spread over the six rows against
 * the model's (CD_SEEK_LANDING, up to ~280,000) is the verdict.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-init-after --disc --ntsc --frames=20000 --quiet=6000`.
 * ON THIS CORE AT v0.367 (2026-10-06, graded with The X-Files Disc 1 in, as tools/drivecompare.js grades; M368's
 * branch core the same to the cycle but for the Pause's INT3):
 *   Pause    INT3 +42k-62k (M368 +20k-105k), INT2 +2.24M-2.44M after it
 *   Init     INT3 +81,240, INT2 +33.76M-33.91M (1.0 s, a 4.4 ms spread: CD_SEEK_LANDING)
 *   Init     INT3 +81,240, INT2 +557,560
 *   ReadN    INT3 +25k-57k, first sector +22.24M-22.51M (the speed change back)
 * ON THE DRIVE'S OWN PROGRAMS (PSX_CD_FIRMWARE): SC430940 / SC430944
 *   Pause    INT2 +2.22M-2.25M / +2.23M-2.26M
 *   Init     INT3 +68k-124k, INT2 +22.4M-32.5M / +20.2M-32.3M - 0.6-0.96 s, spread 0.3 s
 *   Init     INT3 +68k-81k, INT2 +1.015M-1.047M / +1.031M-1.046M
 *   ReadN    first sector +22.26M-22.37M / +22.27M-22.55M
 * The speed change back agrees everywhere; the first Init does not: the model sits at the programs' top with a
 * spread seventy times narrower, and the second Init answers in half the programs' time.
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

/* An interrupt, taken as Famitsu Wave's handler takes it: acknowledged ~1,900 cycles after it shows, and an INT1's
 * sector read - want-data cleared and set at +5,300, 512 words by DMA at +6,800 (it never reads a header). */
static uint32_t evT; static uint8_t evType, evN, evB[8];
static bool take(uint32_t deadline) {
    uint8_t f;
    for (;;) {
        f = irqFlag();
        if (f) break;
        if ((int32_t)(now() - deadline) >= 0) return false;
    }
    const uint32_t t = now();
    evT = t; evType = f; evN = 0;
    until(t + 1900);
    while (hasResp() && evN < 8) evB[evN++] = resp();
    for (int dn = 0; dn < 32 && hasResp(); dn++) resp();
    ackAll();
    if (f == 1) {
        until(t + 5300);
        want(false); want(true);
        until(t + 6800);
        cdDma(512);
    }
    return true;
}
/* Writes a command; returns its first response's time (0 on a timeout); `type` gets 3 or 5. */
static uint32_t issue(uint8_t c, const uint8_t *p, int np, uint8_t *type) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t tw = now();
    command(c);
    for (;;) {
        if (!take(tw + 140000000)) { if (type) *type = 0; return 0; }
        if (evType == 3 || evType == 5) { if (type) *type = evType; return evT; }
    }
}
static void quiet(uint8_t c, const uint8_t *p = 0, int np = 0) { issue(c, p, np, 0); }
/* The next interrupt of one type (others taken as they come); its time, or 0 after `cycles`. */
static uint32_t waitFor(uint8_t type, uint32_t cycles) {
    const uint32_t end = now() + cycles;
    while (take(end)) if (evType == type || evType == 5) return evType == type ? evT : 0;
    return 0;
}
static void idle(uint32_t cycles) { const uint32_t end = now() + cycles; while (take(end)) { } }
static void setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    const uint8_t p[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) };
    quiet(0x02, p, 3);
}

static void unlock() {
    const uint8_t t22[1] = { 0x22 };
    issue(0x19, t22, 1, 0);
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
        quiet(0x50 + k, p, n);
    }
}

static const uint32_t FROM = 40405, SECTORS = 150, START = 16;   /* 09:00:55, 150 sectors; 00:02:16 */

static void once(int rep) {
    uint8_t ty;
    setloc(FROM);
    const uint32_t ts = issue(0x15, 0, 0, &ty); (void)ts;              /* SeekL */
    waitFor(2, 140000000);
    idle(12000);
    const uint8_t m80[1] = { 0x80 };
    quiet(0x0E, m80, 1); idle(4500);
    quiet(0x06);                                                      /* ReadN */
    uint32_t last = 0;
    for (uint32_t n = 0; n < SECTORS; n++) { last = waitFor(1, 140000000); if (!last) break; }
    until(last + 42400);
    /* Nothing prints until the time is over (see the header): its numbers are kept and printed with the drive paused. */
    uint32_t tw = now();
    const uint32_t p3 = issue(0x09, 0, 0, &ty), p2 = waitFor(2, 140000000);
    const uint8_t pty = ty; const uint32_t pw = tw;
    until(p2 + 281600);
    quiet(0x01); idle(2000);
    tw = now();
    const uint32_t i3 = issue(0x0A, 0, 0, &ty), i2 = waitFor(2, 140000000);
    const uint8_t ity = ty; const uint32_t iw = tw;
    idle(1100); quiet(0x0C);                                          /* Demute */
    idle(6720000);
    quiet(0x01); idle(2000);
    tw = now();
    const uint32_t j3 = issue(0x0A, 0, 0, &ty), j2 = waitFor(2, 140000000);
    const uint8_t jty = ty; const uint32_t jw = tw;
    idle(1100); quiet(0x0C);
    idle(1400); setloc(START);
    const uint8_t ma0[1] = { 0xA0 };
    idle(2800); quiet(0x0E, ma0, 1);
    idle(2100); tw = now();
    const uint32_t r3 = issue(0x06, 0, 0, &ty), r1 = waitFor(1, 140000000);
    idle(16000);
    quiet(0x09); waitFor(2, 140000000);
    printf("%d  Pause %d +%d %d +%d |", rep, pty, p3 - pw, 2, p2 ? p2 - p3 : 0);
    printf(" Init %d +%d 2 +%d |", ity, i3 - iw, i2 ? i2 - iw : 0);
    printf(" Init %d +%d 2 +%d |", jty, j3 - jw, j2 ? j2 - jw : 0);
    printf(" ReadN %d +%d 1 +%d\n", ty, r3 - tw, r1 ? r1 - tw : 0);
}

int main() {
    printf("\ncdrom/cd-init-after\n");
    printf("Init after a double-speed read is Paused at 09:02:55, with Famitsu Wave's commands and spacing. Needs a disc of 10+ minutes.\n");
    printf("Cycles at the system clock (1 ms = 33,869), each from its command's write. No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                                     /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                      /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    unlock();
    quiet(0x0A); waitFor(2, 140000000);                    /* Init */
    quiet(0x0C);                                           /* Demute */
    for (int rep = 0; rep < 6; rep++) once(rep);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
