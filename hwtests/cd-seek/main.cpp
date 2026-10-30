/*
 * cdrom/cd-seek - how long does a seek take, by distance, direction, radius
 * and speed?
 *
 * WHY. This core's seek time is DuckStation's curve (CDROM seekTicks): a
 * target just ahead is read up to, one a track behind is a track jump and a
 * read forward, a short seek is a flat 0.05 s or 0.1 s either side of a
 * switch point that falls from ~330 sectors at the disc's start to ~60 at its
 * end, and a long one is a log + linear sled ramp to ~0.9 s - plus a landing
 * draw of up to 280,000 cycles (CD_SEEK_LANDING). None of it is a console
 * measurement anyone published. Sony's Fall '96 developer seminar says a seek
 * within +/-128 revolutions is done with the LENS alone, with no sled move,
 * and gives no times (ROAD A10) - which is a different shape: a flat short
 * seek out to ~1,100 sectors near the hub and ~2,700 at the rim. Games race
 * seeks: several CD knife-edges in STATUS are a seek against a frame.
 *
 * WHAT IT DOES. Reads the disc's length (GetTD 0). For each speed, from two
 * origins (near the start and the middle), for each distance from 1 to
 * 131,072 sectors by powers of two, forward and back: Setloc + SeekL to the
 * origin and wait for it to finish, then Setloc + SeekL to the target and time
 * the command write to its INT2, three times. Then the same shape for ReadN's
 * first INT1 at a few distances, where the drive reads forward to a target
 * just ahead instead of seeking.
 *
 * THE CLOCK. Timer2 at sysclock/8, read continuously and unwrapped in
 * software, as cd-pause.
 *
 * THE RULE: run it here first. This core's rows follow seekTicks: the close
 * forward rows grow a sector's time a sector, the rows out to the switch point
 * sit near 0.05 s (1.69M cycles), then 0.1 s (3.39M), then the sled ramp, each
 * with up to 280,000 of landing on top.
 *
 * ON THIS CORE AT v0.236 (2026-09-27, `node hwtests/run.js cd-seek --disc
 * --frames=60000 --quiet=6000`, the synthetic 74-minute disc): at 2x from LBA 1,000, +1..+4
 * read 0.83-1.64M (read forward, a sector's time a sector), +8..+256 1.76-2.01M
 * (0.05 s and the landing), +512..+4096 3.41-3.68M (0.1 s), then the sled:
 * +8192 10.5M rising to 19.6M at +131,072. From the middle (167,062) the switch
 * point sits between 64 and 128. Backwards inside a track is a jump and a read
 * forward (-1 0.54-0.78M, -8 0.56-0.67M). 1x seeks the same; its read-forward
 * rows are twice as long. A console's rows can be laid over these.
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

static uint32_t discSectors() {
    param(0); command(0x14); waitInt(3);
    resp(); uint8_t mm = unbcd(resp()), ss = unbcd(resp()); drain(); ackAll();
    return (mm * 60 + ss) * 75;
}

/* SeekL to lba and wait for it to finish; returns command write -> INT2 in
 * cycles, 0 on an error. */
static uint32_t seekTo(uint32_t lba) {
    setloc(lba);
    uint32_t t0 = now();
    command(0x15);
    if (waitInt(3) == 5) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    uint8_t r = waitInt(2); drain(); ackAll();
    return r == 5 ? 0 : now() - t0;
}

/* ReadN at lba from a paused head; returns command write -> first INT1. */
static uint32_t readTo(uint32_t lba) {
    setloc(lba);
    uint32_t t0 = now();
    command(0x06);
    if (waitInt(3) == 5) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    uint8_t r = waitInt(1);
    uint32_t t = now() - t0;
    drain(); ackAll();
    simple(0x09); waitInt(2); drain(); ackAll();     /* Pause, and its INT2 */
    return r == 5 ? 0 : t;
}

int main() {
    printf("\ncdrom/cd-seek\n");
    printf("Seek time by distance, direction, radius and speed: command write -> INT2, cycles\n");
    printf("(33,868,800 a second). There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */

    const uint32_t total = discSectors();
    printf("disc: %d sectors (%d:%02d)\n", total, total / 4500, (total / 75) % 60);
    const uint32_t origins[2] = { 1000, total / 2 };
    for (int speed = 1; speed >= 0; speed--) {
        setmode(speed ? 0x80 : 0x00);
        for (int o = 0; o < 2; o++) {
            for (int dir = 1; dir >= -1; dir -= 2) {
                for (uint32_t d = 1; d <= 131072; d *= 2) {
                    const int32_t target = (int32_t)origins[o] + dir * (int32_t)d;
                    if (target < 16 || target > (int32_t)total - 200) continue;
                    printf("%s SeekL from %6d %c%6d :", speed ? "2x" : "1x", origins[o], dir > 0 ? '+' : '-', d);
                    for (int run = 0; run < 3; run++) {
                        seekTo(origins[o]);
                        printf(" %8d", seekTo((uint32_t)target));
                    }
                    printf("\n");
                }
            }
        }
        /* ReadN from a paused head: the close targets, where the drive reads
         * forward to the sector instead of seeking. */
        static const int32_t rd[] = { 1, 2, 4, 8, 16, 32, 64, -1, -4, -16, -64 };
        for (unsigned i = 0; i < sizeof(rd) / sizeof(rd[0]); i++) {
            const uint32_t from = total / 2;
            printf("%s ReadN from %6d %c%6d :", speed ? "2x" : "1x", from, rd[i] > 0 ? '+' : '-', rd[i] > 0 ? rd[i] : -rd[i]);
            for (int run = 0; run < 3; run++) {
                seekTo(from);
                printf(" %8d", readTo((uint32_t)((int32_t)from + rd[i])));
            }
            printf("\n");
        }
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
