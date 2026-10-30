/*
 * cdrom/cd-int1-spread - how evenly a reading drive's sectors come in: every interval between INT1s over 300
 * sectors at double speed and 300 at single, each sector taken as a game takes it (2026-10-06).
 *
 * WHY. tools/cddrivetest.js runs the drive's own program and finds "INT1 every sector exactly, its spread within 10%
 * of the console's (wider than his, most at double speed - open)": the spread is open. This emulator's model jitters
 * each INT1 by up to CD_SECTOR_JITTER (M174) at the sub-CPU loop's phase. Traced at double speed (tools/cdtrace.js,
 * 2026-10-06): the model's intervals mean 225.7k cycles, sd ~9.9k, 204k-247k; the SCPH-7501's and 9001's programs sd
 * 11.1k-12.7k, 198k-252k. Nightmare Creatures (USA) loses a sector when an INT1 lands while another interrupt is
 * being handled, which an interval far from the mean makes likelier - a race the 7501's program loses too (STATUS
 * "The failing games as console tests"). No console's spread at either speed has been recorded.
 *
 * WHAT IT DOES. Needs a data disc (The X-Files Disc 1; a burned disc is unlocked first - psx-spx's secret unlock).
 * Interrupts off, the CD's flags polled; Timer 2 at sysclock/8 unwrapped in software. ReadN from 00:30:00 at double
 * speed (Setmode 80h), then single (00h); each INT1 acknowledged ~1,900 cycles after it shows, its 512 words read by
 * DMA at +6,800 (Famitsu Wave's handler, traced) - the read finishes well inside a sector. 300 intervals each, kept
 * in RAM and printed with the drive paused (over Unirom's cable the TTY is 115,200 baud, ~2,940 cycles a character).
 *
 * READ IT LIKE THIS. Per speed: min, max, mean and sd of the intervals (sd to 64 cycles); a histogram in 4,000-cycle bins; and the
 * first 48 intervals as they came (a regular sector clock with a jitter on top, or a loop's phase stepping).
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-int1-spread --disc --ntsc --frames=6000 --quiet=3000`.
 * ON THIS CORE AT v0.367 AND ON THE DRIVE'S OWN PROGRAMS (2026-10-06, The X-Files Disc 1 in):
 *              2x min / max / mean / sd               1x min / max / mean / sd
 *   v0.367     202,176 / 248,904 / 225,824 / 10,880   426,928 / 476,248 / 451,618 / 11,072   flat (CD_SECTOR_JITTER)
 *   SC430940   198,920 / 251,328 / 225,732 /  7,232   430,056 / 474,904 / 451,584 /  7,424   peaked, long tails
 *   SC430944   205,496 / 250,080 / 225,719 /  7,488   425,416 / 478,808 / 451,565 /  8,896   peaked, long tails
 * The same mean everywhere (a sector is 225,792 at 2x); the model's spread is a third wider and uniform where the
 * programs' piles up near the mean. (In the games' traces, with commands between the sectors, the programs read sd
 * 11.1k-12.7k: the commands' work moves the sectors too.)
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
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x00008000);
    write32(0x1F8010B0, (uint32_t)sink & 0x1FFFFFFF);
    write32(0x1F8010B4, words);
    write32(0x1F8010B8, 0x11000000);
    for (uint32_t k = 0; (read32(0x1F8010B8) & 0x01000000) && k < 2000000; k++) { tick(); }   /* rule 1: bounded */
}

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
    if (f == 1) { until(t + 5300); want(false); want(true); until(t + 6800); cdDma(512); }
    return true;
}
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

#define N 300
static uint32_t gap[N];

static void spread(const char *name, uint8_t mode) {
    const uint8_t m[1] = { mode };
    issue(0x0E, m, 1); idle(30000000);                     /* the speed settled */
    const uint32_t a = 2250 + 150;                         /* 00:30:00 */
    const uint8_t p[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) };
    issue(0x02, p, 3); issue(0x06);
    uint32_t last = 0; int n = -8;                         /* the first eight let the read settle */
    while (n < N) {
        if (!take(now() + 70000000)) break;
        if (evType != 1) continue;
        if (n >= 0 && last) gap[n] = evT - last;
        last = evT; n++;
    }
    issue(0x09); idle(4000000);                           /* paused: now print */
    if (n < N) { printf("%s: only %d intervals\n", name, n); return; }
    /* No 64-bit integers in this toolchain: the sum fits 32 bits (300 x ~470,000), the variance is kept in units
     * of 64 cycles squared (a deviation of 50,000 is 781 units, 610,000 squared, 183M over 300). */
    uint32_t lo = 0xFFFFFFFF, hi = 0, s = 0;
    for (int i = 0; i < N; i++) { if (gap[i] < lo) lo = gap[i]; if (gap[i] > hi) hi = gap[i]; s += gap[i]; }
    const uint32_t mean = s / N;
    uint32_t v = 0;
    for (int i = 0; i < N; i++) { const int32_t d = ((int32_t)(gap[i] - mean)) / 64; v += (uint32_t)(d * d); }
    uint32_t sd64 = 0; { const uint32_t x = v / N; while ((sd64 + 1) * (sd64 + 1) <= x) sd64++; }
    const uint32_t sd = sd64 * 64;
    printf("%s: %d intervals, min %d max %d mean %d sd %d\n", name, N, lo, hi, mean, sd);
    printf("  histogram (4,000-cycle bins from %d):", (lo / 4000) * 4000);
    for (uint32_t b = (lo / 4000) * 4000; b <= hi; b += 4000) {
        int c = 0; for (int i = 0; i < N; i++) if (gap[i] >= b && gap[i] < b + 4000) c++;
        printf(" %d", c);
    }
    printf("\n  first 48:");
    for (int i = 0; i < 48; i++) printf("%s%d", (i % 12) ? " " : "\n   ", gap[i]);
    printf("\n");
}

int main() {
    printf("\ncdrom/cd-int1-spread\n");
    printf("Every interval between INT1s over 300 sectors at double and at single speed. Needs a data disc.\n");
    printf("Cycles at the system clock (1 ms = 33,869). No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    unlock();
    issue(0x0A); idle(35000000);                           /* Init and its INT2 */
    issue(0x0C);
    spread("2x", 0x80);
    spread("1x", 0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
