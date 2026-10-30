/*
 * cdrom/cd-init-args - a Nop right after an Init refused for its parameters, and where the drive's slow maintenance
 * cycles fall after an Init (2026-10-05, for the SCPH-9001)
 *
 * WHY. pcsx-redux nugget's tests/cdrom/cdlinit.c, cdlInitWithArgs, validated on an SCPH-9001: Init (0Ah) written with
 * four FFh parameters answers INT5 03h 20h, and a Nop written after it must answer in 1,000-3,500 us ("Typical value
 * seems to be around 1.5ms"). The suite runs it right after cdlInitDelayed's Init has completed. This emulator's model
 * answers that Nop in 3,968 us on some CD timing draws (M367's default pair, M366's 0BADF00D): the Nop meets the drive
 * loop's slow maintenance cycle (M122: a 40,291-cycle poll period +-60% and 72,789 more about 15 times a second,
 * fitted to ps1-tests' CdlNop row, 24,024 / 45,993 / 137,104). The consoles of console day spread the same way in
 * general - their first responses (cd-ack-times) pass 3.5 ms in 2-4% of 64 tries - so the question is what happens
 * RIGHT AFTER AN INIT.
 *
 * WHAT IT DOES. Interrupts off, the CD polled, Timer 2 at sysclock/8 unwrapped. A real Init first, completed. Then:
 *   RANDOM PHASE: 256 times, a pseudo-random 0-5 ms wait, Init with FFh x4 (its error, expected INT5 03 20), then Nop
 *     at once: each one's time from its write to its interrupt.
 *   CONTROL: 256 plain Nops after the same random waits.
 *   AFTER A COMPLETED INIT, as the nugget suite runs it: 64 times an Init to its INT2, then at once the refused Init
 *     and the Nop.
 *   SLOW CYCLES AFTER AN INIT: 8 times an Init to its INT2, then Nops back to back for ~250 ms; every Nop answered in
 *     more than 100,000 cycles is printed with its write's time after the INT2 - where the slow cycles fall.
 * Each set: min, average, max, the count over 3,500 us (118,541 cycles), and a histogram in 16,384-cycle steps (the
 * last 180,224+), as cd-ack-times prints them.
 *
 * READ IT LIKE THIS. RANDOM PHASE against CONTROL: the general rate. AFTER A COMPLETED INIT: the nugget test's own
 * case. SLOW CYCLES: when the first maintenance comes after an Init, and whether they then come every ~66 ms.
 *
 * THE RULE: run it here first. ON THIS CORE AT v0.367 (the model): over 3.5 ms RANDOM PHASE INT5 0 / Nop 3 of 256,
 * CONTROL 2 of 256; AFTER A COMPLETED INIT INT5 2 / Nop 1 of 64; SLOW CYCLES the first 466,000-1,061,000 cycles
 * after the INT2 (the model's maintenance clock runs free from power-on), then every ~2,260,000. ON THE SCPH-9001's
 * OWN PROGRAM (PSX_CD_FIRMWARE=CDROM-firmware/SC430944.S19, --settle=600): RANDOM PHASE INT5 4 / Nop 3 of 256 and
 * CONTROL 1 of 256 - the model's rate - but AFTER A COMPLETED INIT 0 and 0 of 64 (INT5 within 30,488-59,432, Nop
 * within 29,600-56,360), and SLOW CYCLES the first at 349,000-381,000 cycles after the INT2 on every Init (answered
 * in 146,000-201,000), then many a little slow (100,000-115,000) every 400,000-900,000 and a big one again near
 * 4,800,000 - not the model's one slow cycle in 66.7 ms. nugget's own case is RANDOM PHASE: cdlInitDelayed holds its
 * Init's INT3 for 500 ms, so cdlInitWithArgs meets the loop wherever it is - a ~1% draw on the model and the program
 * alike (STATUS, the Init draw). A model restarting its maintenance clock at an Init (scratch, 2026-10-05) got the
 * AFTER case right and moved cdlInitDelayed's own draw from 4 of 8 seed pairs failing to 7: the loop wants the
 * program's pattern, which this probe also asks the console for.
 * AT v0.377.9 (CD rebuild, piece 6): SLOW CYCLES the first at 344,000-376,000 after the INT2 (answered in 159,000-
 * 195,000), the next turn's at ~4.73M - the hold's jump back at the next frame after home (STATUS piece 6), where the
 * consoles read 341,000-371,000 (147,000-208,000) and ~4.62M-4.68M; AFTER A COMPLETED INIT 0 and 0 of 64, as on the
 * consoles. M368's quiet window (349,000-381,000 after every INT2, to v0.377.8) is gone.
 * `node hwtests/run.js cd-init-args --disc --frames=20000 --quiet=1500`
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
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }   /* rule 1: bounded (the FIFO holds 16) */

static uint32_t clk8;
static uint16_t lastT;
static inline void tick() { uint16_t t = readTimer(2); clk8 += (uint16_t)(t - lastT); lastT = t; }
static inline uint32_t now() { tick(); return clk8 * 8; }
static void waitUntil(uint32_t t) { while ((int32_t)(now() - t) < 0) { } }

static uint8_t nextInt(uint32_t* at) {
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t t = now();
        const uint8_t f = irqFlag();
        if (f) { *at = t; return f; }
        if (t - t0 > 70000000) { *at = t; return 0; }
    }
}
static bool tooLong(uint32_t t0) { return now() - t0 > 140000000; }
static uint8_t waitInt(uint8_t want) {
    const uint32_t t0 = now();
    for (;;) {
        uint32_t t;
        const uint8_t f = nextInt(&t);
        if (f == want || f == 5 || f == 0) return f;
        drain(); ackAll();
        if (tooLong(t0)) return 0;
    }
}
/* Init to its INT2; the time the INT2 was seen. */
static uint32_t init() {
    uint32_t t = 0;
    command(0x0A); waitInt(3); drain(); ackAll();
    const uint32_t t0 = now();
    for (;;) { const uint8_t f = nextInt(&t); drain(); ackAll(); if (f == 2 || f == 5 || f == 0 || tooLong(t0)) break; }
    return t;
}

static uint32_t rng = 0x2545F491u;
static uint32_t rand32() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define N 256
static uint32_t ack[N], err[N];
static uint8_t errType[N], errByte[N];

static void report(const char* name, const uint32_t* v, int n) {
    uint32_t mn = 0xFFFFFFFFu, mx = 0, over = 0, sum = 0; int hist[12] = { 0 };
    for (int i = 0; i < n; i++) {
        const uint32_t x = v[i];
        if (x < mn) mn = x;
        if (x > mx) mx = x;
        sum += x;
        if (x > 118541) over++;
        int b = x / 16384; if (b > 11) b = 11; hist[b]++;
    }
    printf("  %-24s min %7u  avg %7u  max %7u  over 3.5 ms %3u of %d  |", name, (unsigned)mn, (unsigned)(sum / n), (unsigned)mx, (unsigned)over, n);
    for (int b = 0; b < 12; b++) printf(" %d", hist[b]);
    printf("\n");
}

/* The refused Init and the Nop after it, into err[i] / ack[i]. */
static void refusedThenNop(int i) {
    uint32_t t;
    param(0xFF); param(0xFF); param(0xFF); param(0xFF);
    uint32_t tw = now();
    command(0x0A);                                  /* Init, four parameters: refused */
    errType[i] = nextInt(&t); err[i] = t - tw;
    idx(1); errByte[i] = 0; if (hasResp()) { resp(); if (hasResp()) errByte[i] = resp(); }
    drain(); ackAll();
    tw = now();
    command(0x01);                                  /* Nop */
    const uint8_t f = nextInt(&t); ack[i] = t - tw;
    drain(); ackAll();
    if (f != 3) printf("  try %d: the Nop answered INT%d\n", i, f);
}

int main() {
    printf("\ncdrom/cd-init-args\n");
    printf("A Nop right after an Init refused for its four parameters (nugget cdlInitWithArgs), and where the slow\n");
    printf("maintenance cycles fall after an Init. Cycles from each write to its interrupt; histogram in 16,384-cycle\n");
    printf("steps (the last 180,224+). No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    const uint16_t prev = initTimer(2, 2);          /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    init();
    uint32_t t;
    for (int i = 0; i < N; i++) { waitUntil(now() + rand32() % 169344); refusedThenNop(i); }   /* 0-5 ms */
    int int5 = 0, b20 = 0;
    for (int i = 0; i < N; i++) { if (errType[i] == 5) int5++; if (errByte[i] == 0x20) b20++; }
    printf("RANDOM PHASE (the refused Init: INT5 %d of %d, its second byte 20h %d of %d):\n", int5, N, b20, N);
    report("Init refused, to INT5", err, N);
    report("then Nop, to INT3", ack, N);
    for (int i = 0; i < N; i++) {
        waitUntil(now() + rand32() % 169344);
        const uint32_t tn = now();
        command(0x01);
        nextInt(&t); ack[i] = t - tn;
        drain(); ackAll();
    }
    printf("CONTROL:\n");
    report("Nop alone", ack, N);
    for (int i = 0; i < 64; i++) { waitUntil(now() + rand32() % 169344); init(); refusedThenNop(i); }
    printf("AFTER A COMPLETED INIT (64 tries):\n");
    report("Init refused, to INT5", err, 64);
    report("then Nop, to INT3", ack, 64);
    printf("SLOW CYCLES AFTER AN INIT (Nops back to back for ~250 ms; those over 100,000 cycles, at their write's time\n");
    printf("after the Init's INT2):\n");
    for (int k = 0; k < 8; k++) {
        waitUntil(now() + rand32() % 169344);
        const uint32_t t2 = init();
        printf("  Init %d:", k);
        int shown = 0;
        while (now() - t2 < 8467200) {                /* ~250 ms */
            const uint32_t tw = now();
            command(0x01);
            nextInt(&t);
            drain(); ackAll();
            if (t - tw > 100000 && shown < 12) { printf(" %u(%u)", (unsigned)(tw - t2), (unsigned)(t - tw)); shown++; }
        }
        printf("\n");
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
