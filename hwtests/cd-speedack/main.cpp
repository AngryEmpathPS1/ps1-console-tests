/*
 * cdrom/cd-speedack - how long Setmode and Setloc take to answer after a paused read, by the read's speed and whether the Setmode
 * changes it (2026-11-07).
 *
 * WHY. hwtests/cd-playagain2 on The X-Files (console-logs/2026-11-07-SCPH-7501-xfcd, -SCPH-5501-u2-playxf): its row clock starts
 * before Setmode 05h and Setloc 32:03:00, and the two take 155-213k cycles to answer after a SINGLE-speed paused read on the 7501 and
 * 160-215k in this emulator (the drive's own program, SC430940) - alike - but after a DOUBLE-speed one ~135k on the 7501 (99-162k),
 * ~106k on the second 5501 (61-146k) and 77-89k here. Something in going from double speed to single, or in a paused double-speed
 * drive, holds the consoles' answers ~1.5 ms longer than the program does here (the drive's body, src/cdservo.js, if so). Which
 * command waits, and on what: the speed change, the Pause, or the time since it, is this test.
 *
 * WHAT IT DOES. Mute; then rows of: Setmode (the read's speed), Setloc 20:57:19, ReadN, 8 sectors, Pause to its INT2, a wait W,
 * then the row's commands, each timed from its write to its INT3 (cycles at the system clock); the response's stat beside it.
 *   A  2X read, W 430k: Setmode 05h (to single), Setloc 32:03:00
 *   B  2X read, W 430k: Setmode 85h (stays double), Setloc 32:03:00
 *   C  2X read, W 430k: Getstat, Setmode 05h, Setloc 32:03:00
 *   D  1X read, W 430k: Setmode 05h (stays single), Setloc 32:03:00
 *   E  2X read, W 3.0M: Setmode 05h, Setloc 32:03:00
 *   F  2X read, W 430k: Setmode 05h, 430k, Setloc 32:03:00
 * Each kind four times, interleaved (A B C D E F A ...). Interrupts off; every wait bounded (rule 1); a half-set flag read again.
 * Any data disc reaching 33:00 (The X-Files Disc 1). No assertions - compare against your emulator. Prints "Done.".
 *
 * Run here first: `node hwtests/run.js cd-speedack --disc=xfiles` (PSX_MODEL and PSX_CD_FIRMWARE the console's).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

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
/* A command to its INT3 (INT1s on the way taken and dropped), and with `second` its INT2. Returns the cycles from the write to the
 * INT3 seen (0: none in 3 s), its stat in *st. */
static uint32_t timed(uint8_t c, const uint8_t *p, int np, bool second, uint8_t *st) {
    for (int i = 0; i < np; i++) param(p[i]);
    const uint32_t t0 = now();
    command(c);
    uint8_t s = 0, f; uint32_t at = 0;
    do { f = take(MS(3000), &s); } while (f == 1 && now() - t0 < MS(3000));
    if (f == 3) at = now() - t0;
    if (st) *st = s;
    if (second && f == 3) { uint8_t s2; do { f = take(MS(10000), &s2); } while (f == 1 && now() - t0 < MS(10000)); }
    return at;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void prepare(uint8_t speed, uint32_t w) {
    uint8_t st; const uint8_t pm[1] = { speed }; timed(0x0E, pm, 1, false, &st);
    const uint8_t pl[3] = { bcd(20), bcd(57), bcd(19) }; timed(0x02, pl, 3, false, &st);
    command(0x06);
    int n = 0; const uint32_t t0 = now();
    while (n < 8 && now() - t0 < MS(3000)) { const uint8_t f = take(MS(1000), &st); if (f == 1) n++; else if (f == 0) break; }
    timed(0x09, 0, 0, true, &st);
    idle(w);
}

struct Row { char kind; uint32_t t[3]; uint8_t s[3]; int n; };
static Row rows[24];

static void runRow(Row &r) {
    const char k = r.kind;
    prepare(k == 'D' ? 0x00 : 0x80, k == 'E' ? 3000000u : 430000u);
    const uint8_t m05[1] = { 0x05 }, m85[1] = { 0x85 }, loc[3] = { bcd(32), bcd(3), bcd(0) };
    r.n = 0;
    if (k == 'C') { r.t[r.n] = timed(0x01, 0, 0, false, &r.s[r.n]); r.n++; }
    r.t[r.n] = timed(0x0E, k == 'B' ? m85 : m05, 1, false, &r.s[r.n]); r.n++;
    if (k == 'F') idle(430000);
    r.t[r.n] = timed(0x02, loc, 3, false, &r.s[r.n]); r.n++;
    uint8_t st; timed(0x09, 0, 0, true, &st);                /* Pause: the next row starts from a paused drive as each one before */
}

int main() {
    printf("\ncdrom/cd-speedack\nSetmode and Setloc's answers after a paused read, by its speed and the Setmode's.\n");
    printf("Cycles at the system clock (1 ms = 33,869) from each command's write to its INT3, its stat beside it. No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    uint8_t st;
    timed(0x0B, 0, 0, false, &st);                          /* Mute */
    const char order[6] = { 'A', 'B', 'C', 'D', 'E', 'F' };
    for (int i = 0; i < 24; i++) { rows[i].kind = order[i % 6]; runRow(rows[i]); }
    timed(0x0C, 0, 0, false, &st);                          /* Demute */
    timed(0x09, 0, 0, true, &st);
    ExitCriticalSection();
    static const char *what[6] = {
        "2X read, 430k: Setmode 05h, Setloc", "2X read, 430k: Setmode 85h, Setloc", "2X read, 430k: Getstat, Setmode 05h, Setloc",
        "1X read, 430k: Setmode 05h, Setloc", "2X read, 3.0M: Setmode 05h, Setloc", "2X read, 430k: Setmode 05h, 430k, Setloc" };
    for (int i = 0; i < 24; i++) {
        const Row &r = rows[i];
        printf("%c  %s:", r.kind, what[r.kind - 'A']);
        for (int j = 0; j < r.n; j++) printf(" %u (%02X)", (unsigned)r.t[j], r.s[j]);
        printf("\n");
    }
    printf("cdrom/cd-speedack: Done.\n");
    return 0;
}
