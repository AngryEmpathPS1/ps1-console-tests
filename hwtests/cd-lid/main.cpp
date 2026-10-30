/*
 * cdrom/cd-lid - what the drive answers while its lid is open, and how long after the lid shuts it reads the disc.
 *
 * WHY (M331). GameShark CDX 3.3 starts a game only after it has counted, from GetlocP polled through a disc swap,
 * 50 of INT5 11h 80h (the lid open), then 50 of 13h 80h (shut, the motor on, the new disc not read yet), then 50 of
 * the new disc's start. psx-spx: "80h appears on some commands (02h..09h, 0Bh..0Dh, 10h..16h, 1Ah, 1Bh?, and 1Dh)
 * when the disk is missing", and GetID's table "Door Open INT5(11h,80h) ... Spin-up INT5(01h,80h) ... Detect busy
 * INT5(03h,80h)". The SCPH-9001's own program (src/cddrive.js), polled every 10 ms on Crash Bandicoot (USA):
 * 11h 80h to +0.87 s after the close, 13h 80h to +3.51 s, then the position - so the model's CD_DETECT is 2.64 s,
 * on top of DuckStation's 1 s spin-up. A console's timings are not known: the program's servo has assumed
 * mechanics. This test asks a console.
 *
 * WHAT IT DOES. Init and Demute, then: "OPEN THE LID". It watches the door switch (Test 19h,21h bit 1 - it does
 * not read the shell latch away, as Getstat would) and, from the moment it reads open, sends GetlocP every ~10 ms
 * for 2 s and prints every change of answer with its time. Then "SHUT THE LID" (the same disc back in): from the
 * moment the switch reads shut, GetlocP every ~10 ms, every change printed, until it has answered a position for
 * a second (or 20 s). Then the same once more with ReadN instead of GetlocP after the close (its error is 40h on
 * the program, not 80h), and a Getstat at the end. No Getstat in between: it would clear the shell latch and the
 * 11h / 13h would read 01h / 03h (psx-spx's GetID rows are after one).
 *
 * THE CLOCK. Timer2 at sysclock/8, unwrapped in software (cd-pause's).
 *
 * THE RULE: run it here first (the runner opens the lid a second after each OPEN prompt and shuts it 1.5 s after
 * each SHUT one). This core must read 11h 80h through the open, 11h 80h for CD_MOTOR_SPINUP after the close, 13h
 * 80h for CD_DETECT, then the position; ReadN 40h through the same window.
 * TESTED ON ITSELF: its first version took the door's own INT5 as an answer and from then on paired every answer
 * with the command before it - it read the door "shut" 1.5 s early, so the spin-up seemed to last 2.5 s. Now:
 * ON THIS CORE AT v0.332 (2026-10-03, synthetic disc):
 *   open:  GetlocP 13h 80h (the spindle still stopping) to +0.377 s, then 11h 80h; the door's INT5 once, stat 01h
 *   shut:  11h 80h to +1.001 s, 13h 80h to +3.645 s, then INT3 01 00 (track 1, index 0); Getstat 12h after
 *   ReadN: 13h/11h 80h open; shut, 11h 40h to +1.035 s, 13h 40h to +3.678 s, then taken (INT3 12h)
 * THE SCPH-9001's PROGRAM, polled every 10 ms (scratch probe, Crash Bandicoot (USA)): 11h 80h to +0.87 s, 13h 80h
 * to +3.51 s; its door INT5 reads 16h 08h (shell, stat bit 2, motor) where this core sends 01h 08h (M120, M281).
 *   `node hwtests/run.js cd-lid --disc --lid`
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

/* Wait for a given INT, acknowledging others; returns it (5 = error). */
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

/*
 * THE DOOR'S OWN INTERRUPT. Opening the lid makes the drive send INT5 with 08h unasked (psx-spx: "These values
 * appear even if no command was sent"). Taken as a command's answer it would pair every later answer with the
 * command before it - this test's first version read the door "shut" 1.5 s before it was, on this core. So it is
 * noted (doorInts) and skipped wherever it turns up.
 */
static int doorInts;
static uint8_t doorStat;
static bool takeDoor(uint8_t f, uint8_t b1, uint8_t b0) { if (f == 5 && b1 == 0x08) { doorInts++; doorStat = b0; return true; } return false; }

/* One command's first answer as INT << 16 | byte0 << 8 | byte1 (0 = none in 50 ms). */
static uint32_t ask(uint8_t cmd, int n, uint8_t p0) {
    for (int i = 0; i < 4 && (irqFlag() & 7); i++) {          /* nothing left over: the door's INT5, or a sector */
        uint8_t f = irqFlag() & 7, b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
        drain(); ackAll(); takeDoor(f, b1, b0);
    }
    if (n) param(p0);
    command(cmd);
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = irqFlag() & 7;
        if (f) {
            uint8_t b0 = hasResp() ? resp() : 0xFF;
            uint8_t b1 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            if (takeDoor(f, b1, b0)) continue;
            return ((uint32_t)f << 16) | ((uint32_t)b0 << 8) | b1;
        }
        if (now() - t0 > SECOND / 20) return 0;
    }
}
/* A second response a command may still send (ReadN's sectors, an INT2): taken and dropped for 50 ms. */
static void settle() { const uint32_t t0 = now(); while (now() - t0 < SECOND / 20) { if (irqFlag() & 7) { drain(); ackAll(); } } }
static bool doorOpen() { uint32_t r = ask(0x19, 1, 0x21); return (r >> 16) == 3 && (r & 0x0200); }
static void show(uint32_t t, const char *what, uint32_t r) {
    uint32_t ms = t / 33869;                         /* cycles a millisecond, to 0.004% (no 64-bit math here) */
    if (!r) printf("+%d.%03d s  %s  no answer\n", (int)(ms / 1000), (int)(ms % 1000), what);
    else printf("+%d.%03d s  %s  INT%d %02x %02x\n", (int)(ms / 1000), (int)(ms % 1000), what, (int)(r >> 16), (int)((r >> 8) & 0xFF), (int)(r & 0xFF));
}
static void wait10ms() { const uint32_t t0 = now(); while (now() - t0 < SECOND / 100) { } }

/* One lid cycle, polling `cmd` (GetlocP or ReadN) through it. */
static void cycle(uint8_t cmd, const char *name) {
    printf("OPEN THE LID now, and leave it open until asked to shut it.\n");
    while (!doorOpen()) wait10ms();
    const uint32_t open = now();
    printf("+0.000 s  the door switch reads open\n");
    uint32_t last = 0xFFFFFFFF;
    while (now() - open < 2 * SECOND) {
        uint32_t r = ask(cmd, 0, 0);
        if (cmd == 0x06) settle();
        if (r != last) { show(now() - open, name, r); last = r; }
        wait10ms();
    }
    printf("SHUT THE LID now, with the same disc in.\n");
    while (doorOpen()) wait10ms();
    const uint32_t shut = now();
    printf("+0.000 s  the door switch reads shut\n");
    last = 0xFFFFFFFF;
    uint32_t readySince = 0;
    while (now() - shut < 20 * SECOND) {
        uint32_t r = ask(cmd, 0, 0);
        if (r != last) { show(now() - shut, name, r); last = r; }
        const bool ready = (r >> 16) == 3;
        if (ready && cmd == 0x06) { simple(0x09); settle(); break; }   /* a ReadN taken: paused, and done */
        if (cmd == 0x06) settle();
        if (ready && !readySince) readySince = now();
        if (!ready) readySince = 0;
        if (readySince && now() - readySince > SECOND) break;
        wait10ms();
    }
    show(now() - shut, "Getstat", ask(0x01, 0, 0));
    printf("the door's own INT5 seen %d time(s), last with stat %02x\n", doorInts, doorStat);
    doorInts = 0;
}

int main() {
    printf("\ncdrom/cd-lid\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;                               /* CD interrupts masked: polled */
    ackAll();
    initTimer(2, 2);                                /* sysclock/8 */
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                   /* Demute */
    show(0, "GetlocP before", ask(0x11, 0, 0));
    printf("-- GetlocP through a lid cycle\n");
    cycle(0x11, "GetlocP");
    printf("-- ReadN through a lid cycle\n");
    cycle(0x06, "ReadN");
    printf("Done.\n");
    /* Interrupts back on before parking (console day, 2026-10-05): Unirom's debug handler hears the cable through
     * them, and after this program the SCPH-5501 went deaf until a reset. */
    ExitCriticalSection();
    for (;;) { }
}
