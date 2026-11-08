/*
 * cdrom/cd-firstread - the first sectors a ReadN hands over after an Init: how long each sector takes from its INT1 to the
 * data FIFO's DRQSTS, and which sector it is (2026-11-07).
 *
 * WHY. cd-2x-burned's disc check on the SCPH-7502 (console-logs/2026-11-07-SCPH-7502-2x, -2xb; four runs): Init, Demute,
 * GetTD, Setmode 00h, Setloc 00:02:16, ReadN - and the FIRST sector's four marker bytes came as zeros, the next four D17 ..
 * D20; the SCPH-7001's first run had other bytes there. That check waits at most 1 ms for DRQSTS (1F801800h bit 6) after
 * BFRD and then reads, so a sector whose data reaches the FIFO later than that reads as an empty FIFO; or the drive hands
 * over a sector that is not the one asked for (on that disc LBA 0-15 are zero sectors). And cd-unlock reads its six bytes
 * straight after BFRD without waiting at all, and on the 7502 byte 0 always comes back wrong, bytes 1-5 right. This
 * emulator gives D16 and the right byte 0 in both. psx-spx says nothing of the time from INT1 to DRQSTS.
 *
 * WHAT IT DOES. Needs a disc whose sectors say where they are: THE DOUBLE-SPEED DISC (tools/cd2xdisc.js: every data sector
 * 'D' + its LBA in its first four bytes) - any data disc runs, its bytes printed as they are. Interrupts off, the CD's flags
 * polled (a first 1 or 4 read again, CLAUDE.md's console rule 1), every wait bounded, Timer 2 at the system clock. Five
 * cases, each its first four INT1s: the time from the ReadN's write to each INT1, then BFRD and DRQSTS polled for up to
 * 20 ms (the delay printed), and the sector's first 16 bytes read:
 *   A  Init (to its INT2), Demute, GetTD, Setmode 00h, Setloc 00:02:16, ReadN - cd-2x-burned's check, exactly
 *   B  the same with Setmode 20h (2340 bytes: the header comes first, so each sector says which it is)
 *   C  after B's Pause, no Init: Setmode 00h, Setloc 00:02:16, ReadN
 *   D  Init, Setmode 00h, Setloc 00:02:16, ReadN, and the bytes read straight after BFRD, no wait (as cd-unlock reads)
 *   E  Init, Setmode 20h, Setloc 40:00:00, ReadN - away from the hub
 * Each case ends with Pause to its INT2. Nothing prints until the cases are done. Prints "Done.".
 *
 * Run here first: `node hwtests/run.js cd-firstread --disc=2x` (PSX_MODEL the console's).
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
static void param(uint8_t p) { idx(0); *CD2 = p; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }
static void want(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next flag within `limit` cycles (0: none); up to `n` response bytes into r (the rest drained); acknowledged. */
static uint8_t take(uint32_t limit, uint8_t *r, int n) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) {
            if (f == 1 || f == 4) { idle(200); f = irqFlag(); }   /* half of 3 or 5? read it again */
            break;
        }
        if (now() - t0 > limit) return 0;
    }
    for (int i = 0; i < n; i++) r[i] = hasResp() ? resp() : 0;
    drain();
    ackAll();
    return f;
}
/* A command to its first answer, and (second) its INT2 - or its INT5. Returns the last flag. */
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second) {
    uint8_t r[8];
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t f = take(MS(3000), r, 8);
    if (second && f == 3) f = take(MS(10000), r, 8);
    return f;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

struct Got { uint32_t t, drq; uint8_t stat, ready, b[16]; };
struct Case { const char *name; Got g[4]; int n; uint8_t other[8], otherStat[8]; int nother; uint32_t tOther[8]; uint8_t pause; };
static Case cs[5];

static void readN(Case &c, uint8_t mode, uint32_t lba, bool waitDrq) {
    const uint8_t m[1] = { mode };
    cmd(0x0E, m, 1, false);
    const uint32_t a = lba + 150;
    const uint8_t loc[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) };
    cmd(0x02, loc, 3, false);
    command(0x06);
    const uint32_t t0 = now();
    c.n = 0; c.nother = 0;
    while (c.n < 4 && now() - t0 < MS(3000)) {
        uint8_t f = irqFlag();
        if (!f) continue;
        if (f == 1 || f == 4) { idle(200); f = irqFlag(); }
        const uint32_t t = now() - t0;
        const uint8_t st = hasResp() ? resp() : 0;
        drain();
        if (f == 1) {
            Got &g = c.g[c.n++];
            g.t = t; g.stat = st;
            want(true);
            const uint32_t tw = now();
            if (waitDrq) { while (!(*CD0 & 0x40) && now() - tw < MS(20)) {} }
            g.drq = now() - tw; g.ready = (*CD0 & 0x40) ? 1 : 0;
            for (int i = 0; i < 16; i++) g.b[i] = *CD2;
            want(false);
        } else if (c.nother < 8) { c.tOther[c.nother] = t; c.otherStat[c.nother] = st; c.other[c.nother++] = f; }
        ackAll();
    }
    c.pause = cmd(0x09, 0, 0, true);
}

static void init() { cmd(0x0A, 0, 0, true); }

int main() {
    printf("\ncdrom/cd-firstread\nThe first sectors after an Init: INT1 to DRQSTS, and which sector each is. Needs the double-speed disc (any data disc runs).\n");
    printf("Cycles at the system clock (1 ms = 33,869) from the ReadN's write. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();

    cs[0].name = "A Init, Demute, GetTD, mode 00h, 00:02:16"; init(); cmd(0x0C, 0, 0, false);
    { const uint8_t z[1] = { 0 }; cmd(0x14, z, 1, false); } readN(cs[0], 0x00, 16, true);
    cs[1].name = "B Init, Demute, GetTD, mode 20h, 00:02:16"; init(); cmd(0x0C, 0, 0, false);
    { const uint8_t z[1] = { 0 }; cmd(0x14, z, 1, false); } readN(cs[1], 0x20, 16, true);
    cs[2].name = "C after a Pause, no Init, mode 00h, 00:02:16"; readN(cs[2], 0x00, 16, true);
    cs[3].name = "D Init, mode 00h, 00:02:16, read at once (no DRQSTS wait)"; init(); readN(cs[3], 0x00, 16, false);
    cs[4].name = "E Init, mode 20h, 40:00:00"; init(); readN(cs[4], 0x20, 179850, true);

    ExitCriticalSection();
    for (int k = 0; k < 5; k++) {
        const Case &c = cs[k];
        printf("%s:\n", c.name);
        for (int i = 0; i < c.n; i++) {
            const Got &g = c.g[i];
            printf("  INT1 #%d +%9u stat %02x  DRQSTS %s +%6u  bytes", i + 1, (unsigned)g.t, g.stat, g.ready ? "set" : "CLEAR", (unsigned)g.drq);
            for (int j = 0; j < 16; j++) printf(" %02x", g.b[j]);
            printf("\n");
        }
        if (!c.n) printf("  no INT1 in 3 s\n");
        for (int i = 0; i < c.nother; i++) printf("  INT%d +%u stat %02x\n", c.other[i], (unsigned)c.tOther[i], c.otherStat[i]);
        printf("  Pause %s\n", c.pause == 2 ? "to its INT2" : c.pause == 5 ? "INT5" : "no INT2");
    }
    printf("\ncdrom/cd-firstread: Done.\n");
    return 0;
}
