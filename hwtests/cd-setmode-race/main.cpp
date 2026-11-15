/*
 * cdrom/cd-setmode-race - a ReadS written a fixed 104,000 cycles after a Setmode, whether or not the Setmode was answered:
 * GameShark Lite's own conversation, at 32 places across two sectors of the paused hold (2026-11-15). Any data disc.
 *
 * WHY. GameShark Lite (USA) (Unl) reads one sector at 00:13:59 at double speed, pauses, and 14 frames later writes Setmode C0h
 * and then ReadS after a delay loop - 104,337 cycles after the Setmode if no interrupt came in between, 117,785 if the
 * Setmode's INT3 did (the kernel's handler runs inside the loop). It never looks for the INT3. On this emulator's planner as the
 * SCPH-9001 (v0.377.357, refresh phase 0; STATUS "v0.377.357" KNOWN-OPEN) the Setmode fell 22,651 cycles before one of the
 * paused hold's busy stretches (the subcode block of a sector: 77,488 cycles), the mainloop's next command check fell inside
 * it, the Setmode was taken 102,600 after its write - and the ReadS, written 1,700 later on top of it, got INT5 03 20; the
 * kernel answers with an Init, the game never reads again and its picture stops at frame 2065. A 100-cycle move of the same
 * Setmode on v0.377.355 had it answered in 49,148. The consoles do have slow answers there (hwtests/cd-pausewin: GetlocP back
 * to back in a double-speed hold on the SCPH-7501, 5-7 answers of 15 over 70,000 cycles, 79,000-242,000) - so the question is
 * what a console does with the game's two writes when the first lands before one: is the Setmode ever still unanswered at
 * +104,337, and what does the ReadS get then?
 *
 * WHAT IT DOES. Interrupts off, the CD's flags polled, every wait bounded, Timer 2 at the system clock unwrapped in software
 * (cd-doom-phase's helpers). Per step k = 0..31, twice: Setmode 80h; Setloc 00:13:59, ReadN, its first INT1, Pause 23,000 after
 * it, its INT2 (the game's read); the drive holds 7,900,000 + k x 14,112 cycles (a sixteenth of a sector at double speed) from
 * that INT2; then Setmode C0h is written and the flags are polled for 104,337 cycles: an interrupt seen there is timed, read
 * and acknowledged as the kernel does, and the ReadS goes out 117,785 after the Setmode; with none it goes out at 104,337.
 * Then every interrupt for 20 ms more... and until the first INT1 or INT5 (bounded at 1.5 s), each with its first two bytes.
 *
 * READ IT LIKE THIS. A line per pass of 32 steps, each step "A/R": A the Setmode's INT3 in k cycles from its write (-- when
 * none came before the ReadS), R what followed the ReadS: "ok" an INT3 then an INT1; "5:xx" an INT5 with its error byte;
 * "3,5:xx" an INT3 first and then the INT5; "?" neither in 1.5 s. Then the counts.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-setmode-race --disc=xfiles --ntsc --frames=9000 --quiet=8000 --settle=600`.
 */
#define NSTEP 32
#define NPASS 2
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
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */
static uint8_t bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

/* The next interrupt: which and when; drained and acknowledged. A first read of 1 or 4 is read again (an old board's flag can
 * show 1 before 3, psx-spx "Unstable IRQ Flag polling"); every wait is bounded. */
struct Got { uint8_t flag; uint32_t t; };
static Got take(uint32_t limit) {
    Got g; g.flag = 0; g.t = 0;
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { g.flag = f; g.t = t; break; }
        if (t - t0 > limit) { g.t = t; return g; }
    }
    drain(); ackAll();
    return g;
}
static Got takeKind(uint8_t k, uint32_t limit) {
    for (;;) { const Got g = take(limit); if (!g.flag || g.flag == k || g.flag == 5) return g; }
}
static void idleUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) {} }
static uint32_t setloc(uint32_t lba) {
    const uint32_t a = lba + 150;
    ackAll(); param(bcd(a / 4500)); param(bcd((a / 75) % 60)); param(bcd(a % 75));
    const uint32_t t = now(); command(0x02); return t;
}

/* The next interrupt with its first two response bytes; drained and acknowledged. */
struct Got2 { uint8_t flag, b0, b1; uint32_t t; };
static Got2 take2(uint32_t until) {
    Got2 g; g.flag = 0; g.b0 = 0; g.b1 = 0; g.t = 0;
    for (;;) {
        const uint32_t t = now();
        uint8_t f = irqFlag();
        if (f == 1 || f == 4) { const uint8_t f2 = irqFlag(); if (f2) f = f2; }
        if (f) { g.flag = f; g.t = t; break; }
        if ((int32_t)(t - until) > 0) { g.t = t; return g; }
    }
    if (hasResp()) g.b0 = resp();
    if (hasResp()) g.b1 = resp();
    drain(); ackAll();
    return g;
}

static uint32_t A[NPASS][NSTEP];          /* the Setmode's INT3 from its write; 0 = none before the ReadS */
static uint8_t R[NPASS][NSTEP], E[NPASS][NSTEP];   /* R: 1 ok, 2 INT5 first, 3 INT3 then INT5, 0 neither; E: the INT5's error byte */
static int bad;

int main() {
    printf("\ncdrom/cd-setmode-race\n");
    printf("A ReadS a fixed 104,000 cycles after a Setmode in a double-speed hold, answered or not (GameShark Lite's own). Any data disc.\n");
    printf("About a minute and a half; nothing prints until the end.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    idx(1); *CD2 = 0; ackAll(); drain();
    ackAll(); command(0x0A); take(MS(3000)); takeKind(2, MS(8000));       /* Init */
    idleUntil(now() + MS(1000));

    for (int pass = 0; pass < NPASS; pass++) for (int k = 0; k < NSTEP && bad <= 12; k++) {
        ackAll(); param(0x80); command(0x0E); Got g = takeKind(3, MS(3000));
        idleUntil(now() + MS(60));
        setloc(884); g = takeKind(3, MS(3000));
        idleUntil(g.t + 2700); ackAll(); command(0x06); takeKind(3, MS(3000));
        g = takeKind(1, MS(10000));
        if (g.flag != 1) { bad++; }
        idleUntil(g.t + 23000);
        ackAll(); command(0x09); takeKind(3, MS(3000)); g = takeKind(2, MS(8000));
        if (g.flag != 2) { bad++; }
        idleUntil(g.t + 7900000u + (uint32_t)k * 14112u);

        ackAll(); param(0xC0); const uint32_t tw = now(); command(0x0E);
        Got2 a = take2(tw + 104337u - 600u);
        uint32_t rs = tw + 104337u;
        if (a.flag) { A[pass][k] = a.t - tw; rs = tw + 117785u; }
        idleUntil(rs);
        idx(0); *CD1 = 0x1B;                                   /* ReadS, on top of whatever is there */
        uint8_t r = 0, e = 0; bool int3 = false;
        const uint32_t end = now() + MS(1500);
        for (;;) {
            const Got2 x = take2(end);
            if (!x.flag) break;
            if (x.flag == 3) { int3 = true; continue; }
            if (x.flag == 1) { r = 1; break; }
            if (x.flag == 5) { r = int3 ? 3 : 2; e = x.b1; break; }
        }
        R[pass][k] = r; E[pass][k] = e;
        /* back to a known state: the kernel's own answer to the error is an Init */
        idleUntil(now() + 30000);
        ackAll(); drain();
        if (r == 1) { command(0x09); takeKind(3, MS(3000)); takeKind(2, MS(8000)); }
        else { command(0x0A); take(MS(3000)); takeKind(2, MS(8000)); idleUntil(now() + MS(300)); }
        idleUntil(now() + 200000);
    }
    write16(T2_MODE, m2);
    ExitCriticalSection();

    int ok = 0, e5 = 0, none = 0, unans = 0;
    for (int pass = 0; pass < NPASS; pass++) {
        printf("pass %d:", pass + 1);
        for (int k = 0; k < NSTEP; k++) {
            if (A[pass][k]) printf(" %u/", (unsigned)((A[pass][k] + 500) / 1000)); else { printf(" --/"); unans++; }
            const uint8_t r = R[pass][k];
            if (r == 1) { printf("ok"); ok++; } else if (r == 2) { printf("5:%02x", E[pass][k]); e5++; }
            else if (r == 3) { printf("3,5:%02x", E[pass][k]); e5++; } else { printf("?"); none++; }
        }
        printf("\n");
    }
    printf("of %d: the Setmode unanswered at the ReadS %d; the ReadS read %d, got an INT5 %d, neither %d\n", NPASS * NSTEP, unans, ok, e5, none);
    printf("reads or Pauses before the Setmode that failed: %d\n", bad);
    printf("\ncdrom/cd-setmode-race: Done.\n");
    for (;;) VSync(0);
}
