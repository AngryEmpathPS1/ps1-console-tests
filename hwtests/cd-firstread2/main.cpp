/*
 * cdrom/cd-firstread2 - a sector left part-read with BFRD set: what the next ReadN's first INT1 hands over (2026-11-07).
 *
 * WHY. hwtests/cd-firstread on the SCPH-7502 (console-logs/2026-11-07-SCPH-7502-firstread, four runs of four): straight after
 * cd-unlock - which reads six bytes of LBA 16 and leaves BFRD set - Init, Demute, GetTD, Setmode 00h, Setloc 00:02:16, ReadN
 * gave a FIRST sector of zeros (the rest of LBA 16 on that disc is zeros) with DRQSTS set, and the next sectors right; after
 * a FIFO reset (BFRD 0) every first sector was the one asked for. This emulator gives D16 there: its Init clears the data
 * FIFO and BFRD (CDROM.clearSector, as DuckStation's ClearSectorBuffers). If the console keeps the part-read sector through
 * Init, the seek and the next ReadN while BFRD stays set, a remainder with bytes of its own shows it.
 *
 * WHAT IT DOES. Needs THE DOUBLE-SPEED DISC (tools/cd2xdisc.js: every data sector 'D' + its LBA, little-endian, in its first
 * four bytes; LBA 40000 begins 44 40 9C 00). Interrupts off, flags polled (a first 1 or 4 read again), every wait bounded,
 * Timer 2 at the system clock. Every case starts with BFRD 0 (the FIFO reset), then reads LBA 40000 in mode 00h and takes N
 * of its bytes after BFRD and DRQSTS, and acknowledges with BFRD LEFT SET; Pause. Then the second read, mode 00h, 00:02:16,
 * and at its first INT1 DRQSTS is read BEFORE anything is written, then the case's BFRD write, DRQSTS, 16 bytes:
 *   F  N 1; Init; ReadN 00:02:16; BFRD 1 again      - a remainder would read 40 9C 00 00 ...
 *   G  N 1; no Init (Pause only); ReadN; BFRD 1 again
 *   H  N 1; Init; ReadN; BFRD 0 then 1               - the control: a reset FIFO, D16 expected
 *   I  N 2048 (the sector read out); Init; ReadN; BFRD 1 again - the FIFO empty: does the new sector load on its own?
 *   J  N 1; Init; ReadN; no BFRD write at all        - what the FIFO holds as it stands
 * Nothing prints until the cases are done. Prints "Done.".
 *
 * Run here first: `node hwtests/run.js cd-firstread2 --disc=2x` (PSX_MODEL the console's).
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
static bool drq() { return (*CD0 & 0x40) != 0; }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static uint8_t take(uint32_t limit, uint8_t *r, int n) {
    const uint32_t t0 = now();
    uint8_t f = 0;
    for (;;) {
        f = irqFlag();
        if (f) { if (f == 1 || f == 4) { idle(200); f = irqFlag(); } break; }
        if (now() - t0 > limit) return 0;
    }
    for (int i = 0; i < n; i++) r[i] = hasResp() ? resp() : 0;
    drain();
    ackAll();
    return f;
}
static uint8_t cmd(uint8_t c, const uint8_t *p, int np, bool second) {
    uint8_t r[8];
    for (int i = 0; i < np; i++) param(p[i]);
    command(c);
    uint8_t f = take(MS(3000), r, 8);
    if (second && f == 3) f = take(MS(10000), r, 8);
    return f;
}
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static void setloc(uint32_t lba) { const uint32_t a = lba + 150; const uint8_t loc[3] = { bcd(a / 4500), bcd((a / 75) % 60), bcd(a % 75) }; cmd(0x02, loc, 3, false); }
static void setmode(uint8_t m) { const uint8_t p[1] = { m }; cmd(0x0E, p, 1, false); }

/* ReadN to its first INT1 (3 s at most); the INT1 left unacknowledged. 0: none came. */
static bool toInt1() {
    command(0x06);
    const uint32_t t0 = now();
    while (now() - t0 < MS(3000)) {
        uint8_t f = irqFlag();
        if (!f) continue;
        if (f == 1 || f == 4) { idle(200); f = irqFlag(); }
        drain();
        if (f == 1) return true;
        ackAll();
    }
    return false;
}

struct Case { const char *name; uint8_t first[4]; bool got1, got2, drqBefore, drqAfter; uint8_t b[16]; uint8_t pause; };
static Case cs[5];

/* mode: 0 BFRD 1 again, 1 BFRD 0 then 1, 2 no write */
static void runCase(Case &c, int n, bool doInit, int mode) {
    bfrd(false); ackAll(); drain();
    setmode(0x00); setloc(40000);
    c.got1 = toInt1();
    if (c.got1) {
        bfrd(true);
        const uint32_t tw = now(); while (!drq() && now() - tw < MS(20)) {}
        for (int i = 0; i < n; i++) { const uint8_t v = *CD2; if (i < 4) c.first[i] = v; }
        ackAll();                                             /* BFRD left set */
    }
    cmd(0x09, 0, 0, true);
    if (doInit) cmd(0x0A, 0, 0, true);
    setmode(0x00); setloc(16);
    c.got2 = toInt1();
    if (c.got2) {
        c.drqBefore = drq();
        if (mode == 1) bfrd(false);
        if (mode != 2) bfrd(true);
        const uint32_t tw = now(); while (!drq() && now() - tw < MS(20)) {}
        c.drqAfter = drq();
        for (int i = 0; i < 16; i++) c.b[i] = *CD2;
        bfrd(false);
        ackAll();
    }
    c.pause = cmd(0x09, 0, 0, true);
    bfrd(false);
}

int main() {
    printf("\ncdrom/cd-firstread2\nA sector left part-read with BFRD set: the next ReadN's first sector. Needs the double-speed disc.\n");
    printf("No assertions - compare against your emulator.\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    cs[0].name = "F 1 byte of LBA 40000, Init, ReadN 00:02:16, BFRD 1 again"; runCase(cs[0], 1, true, 0);
    cs[1].name = "G 1 byte, no Init, ReadN, BFRD 1 again";                  runCase(cs[1], 1, false, 0);
    cs[2].name = "H 1 byte, Init, ReadN, BFRD 0 then 1";                    runCase(cs[2], 1, true, 1);
    cs[3].name = "I 2048 bytes (read out), Init, ReadN, BFRD 1 again";      runCase(cs[3], 2048, true, 0);
    cs[4].name = "J 1 byte, Init, ReadN, no BFRD write";                    runCase(cs[4], 1, true, 2);
    ExitCriticalSection();
    for (int k = 0; k < 5; k++) {
        const Case &c = cs[k];
        printf("%s:\n", c.name);
        if (!c.got1) { printf("  no INT1 at LBA 40000\n"); continue; }
        printf("  LBA 40000's first bytes taken: %02x", c.first[0]);
        if (k == 3) printf(" %02x %02x %02x", c.first[1], c.first[2], c.first[3]);
        printf("\n");
        if (!c.got2) { printf("  no INT1 at 00:02:16\n"); continue; }
        printf("  00:02:16's INT1: DRQSTS before %s, after %s; bytes", c.drqBefore ? "set" : "clear", c.drqAfter ? "set" : "clear");
        for (int j = 0; j < 16; j++) printf(" %02x", c.b[j]);
        printf("\n  Pause %s\n", c.pause == 2 ? "to its INT2" : c.pause == 5 ? "INT5" : "no INT2");
    }
    printf("\ncdrom/cd-firstread2: Done.\n");
    return 0;
}
