/*
 * cdrom/cd-playagain2 - what puts the SCPH-7502's seek bit late: the read before the Play at double speed or single (2026-11-07).
 *
 * WHY. hwtests/cd-playagain (console-logs/2026-11-07-SCPH-7502-playagain, 32 rows): after a paused DOUBLE-speed ReadN at 20:57:19,
 * Setmode 05h (single speed), Setloc 32:03:00 and Play show the seek bit (40h) ~0.1M cycles after the Play in ~80% of rows and
 * only at ~0.3 s (8-11M) in ~20%: about a spindle change from double speed to single. If it is the spindle, a read at SINGLE
 * speed before the same Play leaves nothing to change and every row is early.
 *
 * WHAT IT DOES. As cd-playagain's ONE row (one Play, Getstat every ~30k cycles for 0.74 s; any data disc reaching 33:00 - the
 * double-speed disc, The X-Files Disc 1), twelve rows alternating: 2X the read before at double speed (Setmode 80h), 1X at single
 * (Setmode 00h). Per row: when 40h and 80h first show after the Play, the last stat. Nothing prints inside a row. Prints "Done.".
 *
 * Run here first: `node hwtests/run.js cd-playagain2 --disc=2x` (PSX_MODEL the console's).
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
static void bfrd(bool on) { idx(0); *CD3 = on ? 0x80 : 0x00; }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next flag within `limit` cycles (0: none), its first response byte into *st; acknowledged (an INT1's FIFO reset). */
static uint8_t take(uint32_t limit, uint8_t *st) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    const uint8_t s = hasResp() ? resp() : 0;
    if (st) *st = s;
    drain();
    if (f == 1) bfrd(false);
    ackAll();
    return f;
}
/* A command to its INT3 (INT1s on the way taken and dropped), and with `second` its INT2. Returns the INT3's stat. */
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second) {
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t st = 0, f;
    const uint32_t t0 = now();
    do { f = take(MS(3000), &st); } while (f == 1 && now() - t0 < MS(3000));
    if (second && f == 3) { uint8_t s2; do { f = take(MS(10000), &s2); } while (f == 1 && now() - t0 < MS(10000)); }
    return st;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static void setloc(uint32_t m, uint32_t s, uint32_t f) { const uint8_t p[3] = { bcd(m), bcd(s), bcd(f) }; cmd(0x02, p, 3, false); }
static void setmode(uint8_t m) { const uint8_t p[1] = { m }; cmd(0x0E, p, 1, false); }

struct Row { const char *name; int plays; uint32_t first40, first80, from40, from80, firstRep; uint8_t lastStat; };
static Row rows[12];

static void prepare(uint8_t speed) {
    setmode(speed); setloc(20, 57, 19);
    command(0x06);
    int n = 0; uint8_t st; const uint32_t t0 = now();
    while (n < 8 && now() - t0 < MS(3000)) { const uint8_t f = take(MS(1000), &st); if (f == 1) n++; else if (f == 0) break; }
    cmd(0x09, 0, 0, true);
    idle(430000);
}
static void play(bool b) { setmode(0x05); if (b) setloc(40, 0, 0); else setloc(32, 3, 0); command(0x03); }

/* policy: 0 ONE, 1 SAME, 2 ALT, 3 ONCE */
static void runRow(Row &r, int policy, uint8_t speed) {
    prepare(speed);
    r.plays = 0; r.first40 = r.first80 = r.from40 = r.from80 = r.firstRep = 0; r.lastStat = 0;
    const uint32_t t0 = now();
    play(false); r.plays = 1;
    uint32_t lastPlay = now() - t0, lastPoll = 0;
    bool waiting = true;                                      /* the Play's own INT3 to come */
    for (;;) {
        const uint32_t t = now() - t0;
        if (t > MS(740)) break;
        uint8_t f = irqFlag();
        if (f) {
            if (f == 1 || f == 4) { idle(200); f = irqFlag(); }
            const uint8_t st = hasResp() ? resp() : 0;
            drain();
            if (f == 1) { bfrd(false); if (!r.firstRep) r.firstRep = t ? t : 1; }
            ackAll();
            if (f == 3 || f == 5) {
                waiting = false; r.lastStat = st;
                if ((st & 0x40) && !r.first40) { r.first40 = t; r.from40 = t - lastPlay; }
                if ((st & 0x80) && !r.first80) { r.first80 = t; r.from80 = t - lastPlay; }
                const bool restart = (policy == 1 || policy == 2) ? (t - lastPlay > 146000 && !(st & 0xC0))
                                   : policy == 3 ? (r.plays == 1 && t - lastPlay > 146000) : false;
                if (restart && r.plays < 200) { play(policy == 2 ? (r.plays & 1) : policy == 3); r.plays++; lastPlay = now() - t0; waiting = true; }
            }
            continue;
        }
        if (!waiting && t - lastPoll > 30000) { command(0x01); lastPoll = t; waiting = true; }
    }
    cmd(0x09, 0, 0, true);
}

int main() {
    printf("\ncdrom/cd-playagain2\nThe read before a Play at double speed or single: when the seek bit (40h) and play (80h) first show.\n");
    printf("Cycles at the system clock (1 ms = 33,869), from the first Play's write. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain(); bfrd(false);
    cmd(0x0B, 0, 0, false);                                   /* Mute */
    for (int k = 0; k < 12; k++) { rows[k].name = (k & 1) ? "1X   the read before at single speed" : "2X   the read before at double speed"; runRow(rows[k], 0, (k & 1) ? 0x00 : 0x80); }
    cmd(0x0C, 0, 0, false);                                   /* Demute */
    ExitCriticalSection();
    for (int k = 0; k < 12; k++) {
        const Row &r = rows[k];
        printf("%s: %d Play%s; 40h ", r.name, r.plays, r.plays == 1 ? "" : "s");
        if (r.first40) printf("at %u (%u after its Play)", (unsigned)r.first40, (unsigned)r.from40); else printf("never");
        printf("; 80h ");
        if (r.first80) printf("at %u (%u after its Play)", (unsigned)r.first80, (unsigned)r.from80); else printf("never");
        printf("; first report %u; last stat %02x\n", (unsigned)r.firstRep, r.lastStat);
    }
    printf("\ncdrom/cd-playagain2: Done.\n");
    return 0;
}
