/*
 * cdrom/cd-seek-cancel - does a command sent during a SeekL cancel the seek's INT2? (2026-11-02)
 *
 * WHY. Read off SC430916 (docs/DRIVE-PROGRAM.md, suspect 6): every command the program accepts writes 020Ch (2DBD), and the
 * seek's completion is sent only on a match (S_45A3). Run on this emulator's drive (the program itself), a SeekL to 20:00:00
 * with a Getstat 50 ms in lands and holds and never sends INT2; without the Getstat it does. Games poll Getstat while seeking,
 * so if a console does the same, a library that waits for that INT2 would wait for ever there; if it does not, the program's
 * host side differs from what the emulator gives it (src/cddrive.js's host interface, the command's timing).
 *
 * WHAT IT DOES. From 00:02:16, SeekL to 20:00:00 and back, at single speed: first with nothing sent during the seek (the
 * control), then with a Getstat (01h) sent 5, 20, 50, 100 and 300 ms after the SeekL's INT3, then with a GetlocP (11h) at
 * 50 ms. Each line: the delay, the Getstat's own answer and its time, then the INT2 (or INT5) and its time after the SeekL,
 * or "no INT2 in 3 s". Then a Getstat to show where the drive is. Interrupts off, Timer 2 at the system clock / 8, every wait
 * bounded (rule 1). No seek past 20:00:00. No hands.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-seek-cancel --disc=xfiles`.
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

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static const uint32_t SECOND = 33868800;
static uint32_t ms(uint32_t t) { return t / 33869; }

/* An interrupt within the time given: INT << 8 | its first byte, 0 for none. The flag read twice before taking a
 * 1 or a 4 (psx-spx "Unstable IRQ Flag polling"); the door's unasked INT5 (08h) skipped. */
static uint32_t take(uint32_t within) {
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = irqFlag() & 7;
        if (f) {
            if (f == 1 || f == 4) { const uint8_t g = irqFlag() & 7; if (g != f) f = g; }
            const uint8_t b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            if (f == 5 && b1 == 0x08) continue;
            return ((uint32_t)f << 8) | b0;
        }
        if (now() - t0 > within) return 0;
    }
}
static void simple(uint8_t cmd) { command(cmd); take(SECOND); }
static void setloc(uint8_t m, uint8_t s, uint8_t f) { param(m); param(s); param(f); simple(0x02); }
static void seekAndWait(uint8_t m, uint8_t s, uint8_t f) { setloc(m, s, f); command(0x15); take(SECOND); take(SECOND * 4); }

/* One trial: SeekL to 20:00:00, cmd sent `after` ms after its INT3 (none when cmd is 0), then wait for INT2. */
static void trial(uint8_t cmd, uint32_t after) {
    seekAndWait(0x00, 0x02, 0x16);
    setloc(0x20, 0x00, 0x00);
    command(0x15);
    const uint32_t t0 = now();
    const uint32_t a = take(SECOND);
    const uint32_t ta = ms(now() - t0);
    uint32_t r = 0, tr = 0, other = 0;
    if (cmd) {
        while (now() - t0 < after * 33869) { if (irqFlag() & 7) { other = take(0); break; } }
        if (!other) { command(cmd); r = take(SECOND / 2); tr = ms(now() - t0); }
    }
    const uint32_t b = other ? other : take(SECOND * 3);
    const uint32_t tb = ms(now() - t0);
    if (!cmd) printf("none        SeekL INT%d %02x at %d ms; ", (int)(a >> 8), (int)(a & 0xFF), (int)ta);
    else printf("%02x at %3d   SeekL INT%d %02x at %d ms; %02x: INT%d %02x at %d ms; ", cmd, (int)after, (int)(a >> 8), (int)(a & 0xFF), (int)ta, cmd, (int)(r >> 8), (int)(r & 0xFF), (int)tr);
    if (b) printf("then INT%d %02x at %d ms%s", (int)(b >> 8), (int)(b & 0xFF), (int)tb, other ? " (before the command went)" : "");
    else printf("no INT2 in 3 s");
    command(0x01);
    const uint32_t g = take(SECOND / 2);
    printf("; Getstat %02x\n", (int)(g & 0xFF));
    if (!b) take(SECOND);   /* a late INT2 must not land in the next trial */
}

int main() {
    printf("\ncdrom/cd-seek-cancel\n");
    printf("SeekL 00:02:16 -> 20:00:00 at 1x, a command sent during it at a delay, and whether the seek's INT2 still comes.\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); take(SECOND); take(SECOND * 10);   /* Init */
    simple(0x0C);                                      /* Demute */
    param(0x00); simple(0x0E);                         /* Setmode: single speed */
    trial(0, 0);
    static const uint16_t AFTER[] = { 5, 20, 50, 100, 300 };
    for (int i = 0; i < 5; i++) trial(0x01, AFTER[i]);
    trial(0x11, 50);
    trial(0, 0);
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) { }
}
