/*
 * cdrom/cd-seek-map - a short seek's time, sector by sector, and where the head is when each seek answers (2026-10-07,
 * for the CD rebuild's seek piece)
 *
 * WHY. cd-seek's rows, laid over this emulator's at v0.377.10 (STATUS, CD rebuild piece 7): most match the consoles to
 * a few percent, but whole families are about one turn of the disc longer on the consoles, the SCPH-7501 and 9001
 * agreeing to the row - at single speed from LBA 1,000 +32/+64/+128 (9.6M / 11.6M / 10.4M cycles, here 5.8M / 7.4M /
 * 6.4M) and -8..-128, from 29:01 +256/+512 and -512 - and a seek ONE SECTOR BEHIND the hold answers in 0.4M on both
 * consoles where this emulator waits a turn (3.8M-7.5M). cd-seek steps by powers of two and never says where the head
 * is, so neither the distance at which the extra turn starts nor the landing rule behind it can be read off it.
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, Timer 2 at sysclock/8 unwrapped in software, as cd-seek. At each
 * speed, from LBA 1,000 and from 130,462 (29:01): for each distance -160..+160 (every sector out to 20, then every 8th),
 * Setloc + SeekL to the origin and wait for its INT2, GetlocP (the head when the next command comes); then Setloc +
 * SeekL to the target timed from the command write to its INT2, and GetlocP at once (the head when it answered). Twice
 * per distance. Printed per row: the two times and the two pairs of GetlocP absolute sectors.
 *
 * READ IT LIKE THIS. A row that jumps by about a turn's time (4.3M at 1x near the start, 6.5M at 29:01; half at 2x)
 * where its neighbours do not is where the extra turn starts. GetlocP at the target's INT2 against the target says
 * where a seek is "done": on it, a sector before, two after.
 *
 * THE RULE: run it here first. ON THIS CORE AT v0.377.10 (The X-Files Disc 1, PSX_BIOS scph7001.bin, --ntsc --settle=600):
 *   the head at every INT2 one sector BEFORE the target (GetlocP 999 for 1,000, the target less one after each seek); at
 *   1x the times climb a sector's time a sector (~450,000) and fall by a turn every ~9.4 sectors either way - a read
 *   forward inside a turn - with a turn more at +9 and +18; +32 / +64 / +128 5.8M / 7.4M / 6.4M. At 2x half that.
 * ON SC430940 (the 7501's own program, PSX_CD_FIRMWARE): the head ON the target at the INT2 (now and then a sector or two
 *   past); a target ten or more sectors ahead taken by a track jump forward and a short read (1.2M at +10..+12 and
 *   +19..+20), so nothing out to 160 passes ~4.4M; +32 / +64 / +128 2.1M / 4.4M / 4.3M. The consoles' cd-seek reads those
 *   three 9.64M / 11.63M / 10.37M on both the 7501 and 9001: longer than either - where they take the extra turn is what
 *   this map is for.
 * `node hwtests/run.js cd-seek-map --disc --frames=30000 --quiet=6000` (--disc: the grader gives it the console's disc; 2026-10-09)
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

static uint8_t waitInt(uint8_t want) {
    const uint32_t c0 = clk8;                                   /* rule 1: 0 after 10 s, never a deaf console */
    for (;;) {
        tick();
        uint8_t f = irqFlag() & 7;
        if (!f) { if (clk8 - c0 > 42336000u) return 0; continue; }
        if (f == want || f == 5) return f;
        /* An old board's flag can read 1 before 3 (psx-spx "Unstable IRQ Flag polling"; the SCPH-1001's cd-irq-glitch: once
         * in 3,000 Getstats): read it again before taking it for another interrupt, or the INT3 is acknowledged here and
         * waited for for ever - cd-seek-map hung the 1001 twice this way (2026-10-09). The normal path is unchanged. */
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
/* SeekL to lba and wait for it to finish; command write -> INT2 in cycles, 0 on an error. */
static uint32_t seekTo(uint32_t lba) {
    setloc(lba);
    uint32_t t0 = now();
    command(0x15);
    if (waitInt(3) == 5) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    uint8_t r = waitInt(2); const uint32_t t = now() - t0; drain(); ackAll();
    return r == 5 ? 0 : t;
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

#define NDIST 76
static int32_t dist[NDIST]; static int nd;
static uint32_t tt[NDIST][2]; static int32_t pA[NDIST][2], pB[NDIST][2];

int main() {
    printf("\ncdrom/cd-seek-map\n");
    printf("A short seek's time sector by sector, and where the head is when each seek answers: command write -> INT2,\n");
    printf("cycles (33,868,800 a second), then GetlocP's absolute sector - at the origin's INT2 / at the target's. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */

    nd = 0;                                         /* -160..-24 by 8, -20..-1, 1..20, 24..160 by 8 */
    for (int32_t d = -160; d <= -24; d += 8) dist[nd++] = d;
    for (int32_t d = -20; d <= -1; d++) dist[nd++] = d;
    for (int32_t d = 1; d <= 20; d++) dist[nd++] = d;
    for (int32_t d = 24; d <= 160; d += 8) dist[nd++] = d;
    static const uint32_t origins[2] = { 1000, 130462 };
    for (int speed = 0; speed < 2; speed++) {
        setmode(speed ? 0x80 : 0x00);
        for (int o = 0; o < 2; o++) {
            for (int i = 0; i < nd; i++) {
                for (int run = 0; run < 2; run++) {
                    seekTo(origins[o]);
                    pA[i][run] = getlocP();
                    tt[i][run] = seekTo((uint32_t)((int32_t)origins[o] + dist[i]));
                    pB[i][run] = getlocP();
                }
            }
            for (int i = 0; i < nd; i++) {
                printf("%s from %6d %c%4d : %9d %9d   head %6d/%6d %6d/%6d\n", speed ? "2x" : "1x", origins[o],
                       dist[i] > 0 ? '+' : '-', dist[i] > 0 ? dist[i] : -dist[i], tt[i][0], tt[i][1],
                       pA[i][0], pB[i][0], pA[i][1], pB[i][1]);
            }
        }
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\ncdrom/cd-seek-map: Done.\n");
    for (;;) VSync(0);
}
