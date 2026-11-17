/*
 * cdrom/cd-jumpclass2 - cd-jumpclass at three radii: which turn a short seek back lands on, near the hub, mid-data and at the data's
 * outer end (2026-11-16). A pressed data disc (Doom (USA): its data to 08:01, so its outer region is ~35 mm; The X-Files Disc 1: to
 * ~58:00, ~48 mm and past).
 *
 * WHY. cd-jumpclass's two-turn classes on the second SCPH-5501 are its separate decoder's blind after a jump (src/cdservo.js
 * SEPARATE_DECODER_BLIND, 4 sectors): near the hub its class shares put the blind at ~3.6 (three targets crossing, each at ~3.6), but
 * cd-hold-locl's double-speed rows split by radius - near the hub the unit's first header comes 1-2 past the first Q (3.6's), at
 * 130,462 3-4 past and never 2 (4's) (experiments/doomload/NOTES.md 21). One constant in sectors does not fit; this maps the blind
 * against the radius before a form is chosen.
 *
 * WHAT IT DOES. cd-jumpclass's sequence (double speed: Setloc origin, ReadN, 45 INT1s, Pause to INT2, 5.29M cycles, Setloc target,
 * ReadN timed to its first INT1, Pause) from one origin a region 2,257 sectors past its first target (cd-jumpclass's 7,157 to 4,900), to 16 targets 6 apart in each of three
 * regions: from 4,900, from 40% of the data's end and from 90% of it (the data's end: the start of track 2, or the disc's lead-out). The
 * regions and repeats interleave (repeat outer, region and target inner). Init, Demute, GetTN / GetTD first; no disc: it says so and stops.
 *
 * READ IT LIKE THIS. A row per target, "R<region> T <lba>:", then each repeat's write-to-first-INT1 in hundredths of a M cycle. No
 * assertions. hwtests/cd-jumpclass2/PREDICTED.txt has this emulator's rows at the blinds 4 and 3.6.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-jumpclass2 --disc=doom --ntsc --frames=40000 --quiet=38000 --settle=600`.
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
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

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
static uint8_t simple(uint8_t cmd) { command(cmd); const uint8_t r = waitInt(3); drain(); ackAll(); return r; }
static void setmode(uint8_t m) { param(m); simple(0x0E); }
static void setloc(uint32_t lba) {
    uint32_t a = lba + 150;
    param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    simple(0x02);
}
/* Pause to its INT2 (0 on none). */
static uint8_t pauseHold() { if (simple(0x09) != 3) return 0; const uint8_t r = waitInt(2); drain(); ackAll(); return r; }
/* ReadN at the Setloc: its write to the first INT1 in cycles (0 on an error), then n-1 INT1s more taken. */
static uint32_t readN(int n) {
    const uint32_t t0 = now();
    command(0x06);
    if (waitInt(3) != 3) { drain(); ackAll(); return 0; }
    drain(); ackAll();
    if (waitInt(1) != 1) { drain(); ackAll(); return 0; }
    const uint32_t t = now() - t0; drain(); ackAll();
    for (int i = 1; i < n; i++) { if (waitInt(1) != 1) { drain(); ackAll(); return 0; } drain(); ackAll(); }
    return t;
}

#define NT 16
#define NREG 3
#define NREP 8
static uint16_t res[NREG][NT][NREP];
static uint32_t base[NREG];

int main() {
    printf("\ncdrom/cd-jumpclass2\n");
    printf("cd-jumpclass at three radii: a short seek back at double speed after a 45-sector read and its Pause, the turn it lands on.\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    uint16_t prev = initTimer(2, 2);                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                   /* Demute */
    param(0); command(0x14);                        /* GetTD(0): the disc's length, or no disc */
    uint32_t total = 0;
    if (waitInt(3) == 3) { resp(); const uint8_t mm = unbcd(resp()), ss = unbcd(resp()); total = (mm * 60 + ss) * 75; }
    drain(); ackAll();
    uint32_t end = total;
    if (total) {
        command(0x13);                              /* GetTN: a second track ends the data */
        uint8_t last = 1;
        if (waitInt(3) == 3) { resp(); resp(); last = unbcd(resp()); }
        drain(); ackAll();
        if (last >= 2) {
            param(bcd(2)); command(0x14);
            if (waitInt(3) == 3) { resp(); const uint8_t mm = unbcd(resp()), ss = unbcd(resp()); end = (mm * 60 + ss) * 75 - 150; }
            drain(); ackAll();
        }
    }
    if (end < 12000) {
        restoreTimer(2, prev); ExitCriticalSection();
        printf("disc: %s - nothing to time\n", total ? "its data too short (needs 12,000 sectors)" : "none (GetTD gave nothing)");
        printf("\nDone.\n");
        for (;;) {}
    }
    base[0] = 4900; base[1] = end * 4 / 10; base[2] = end * 9 / 10 - 2400;
    printf("disc: %d sectors, data to %d; regions from %d, %d, %d\n", total, end, base[0], base[1], base[2]);
    setmode(0x80);
    for (int r = 0; r < NREP; r++)
        for (int g = 0; g < NREG; g++)
            for (int k = 0; k < NT; k++) {
                const uint32_t tg = base[g] + 6 * k;
                setloc(base[g] + 2257);                 /* one origin a region, as cd-jumpclass's 7,157: the landing walks over the targets */
                uint32_t ok = readN(45);
                if (ok) ok = pauseHold();
                uint32_t t = 0;
                if (ok) {
                    idle(5290000u);
                    setloc(tg);
                    t = readN(1);
                    pauseHold();
                }
                res[g][k][r] = (uint16_t)(t / 10000 > 65535 ? 65535 : t / 10000);
            }
    restoreTimer(2, prev);
    ExitCriticalSection();
    for (int g = 0; g < NREG; g++)
        for (int k = 0; k < NT; k++) {
            printf("R%d T %6d:", g, base[g] + 6 * k);
            for (int r = 0; r < NREP; r++) printf(" %d", res[g][k][r]);
            printf("\n");
        }
    printf("waits that ran out: %d\n", timeouts);
    printf("\nDone.\n");
    /* Stay here after Done., as the other tests do (console day, 2026-10-05). */
    for (;;) {}
}
