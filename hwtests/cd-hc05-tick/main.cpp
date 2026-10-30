/*
 * cdrom/cd-hc05-tick - the drive program's own tick, counted against the console's clock (2026-10-24)
 *
 * WHY. The drive's program ticks on its Timer 2 interrupt (FFF4h) and counts the ticks in RAM 60h (S_1C72: inc $60). This
 * emulator's tick is SysClk / 32 with OC2 = 41h, ~1,017 a second (src/cddrive.js, from the 4.2336 MHz oscillator - computed,
 * never measured). cd-lid-sweep2/3 on the 7501 and 9001: the program's focus sweep samples every ~3.1 ms there, every ~2 ms
 * here - a sample every second tick; the work has no wait on hardware and the signal holds (STATUS). If the console's tick is
 * slower, that is it. Read 60h every ~20 ms for 5 s (idle, then reading), each with the console's own Timer 2 time: the tick
 * rate is the count's slope (60h wraps at 256 - unwrapped here).
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


/* 19h,60h,lo,hi: a byte of the drive's RAM; -1 when no answer in 50 ms, -1 - the flag on another interrupt. */
static int peek(uint16_t addr) {
    for (int i = 0; i < 4 && (irqFlag() & 7); i++) {
        uint8_t f = irqFlag() & 7, b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
        drain(); ackAll(); takeDoor(f, b1, b0);
    }
    param(0x60); param(addr & 0xFF); param(addr >> 8);
    command(0x19);
    const uint32_t t0 = now();
    for (;;) {
        uint8_t f = irqFlag() & 7;
        if (f) {
            uint8_t b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            if (takeDoor(f, b1, b0)) continue;
            return f == 3 ? b0 : -1 - f;
        }
        if (now() - t0 > SECOND / 20) return -1;
    }
}

static int32_t ticksAt(uint32_t *t) { const int v = peek(0x060); *t = now(); return v; }

int main() {
    printf("\ncdrom/cd-hc05-tick\n");
    printf("The drive program's tick count (RAM 60h) against the console's clock: idle 5 s, then reading 5 s.\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                   /* Demute */
    for (int phase = 0; phase < 2; phase++) {
        if (phase == 1) {                           /* reading: Setloc 1000, ReadN, at 1x */
            param(0x00); param(0x15); param(0x25); simple(0x02);
            command(0x06); waitInt(3); drain(); ackAll();
        }
        uint32_t t0; const int32_t v0 = ticksAt(&t0);
        int32_t last = v0, total = 0, n = 0; uint32_t tl = t0;
        const uint32_t start = now();
        while (now() - start < 5 * SECOND) {
            { const uint32_t tw = now(); while (now() - tw < SECOND / 50) { if (phase == 1 && (irqFlag() & 7)) { drain(); ackAll(); } } }
            uint32_t t; const int32_t v = ticksAt(&t);
            if (v < 0) continue;
            total += (v - last) & 0xFF; last = v; tl = t; n++;
        }
        const uint32_t ms = (tl - t0) / 33869;
        printf("%s: %d ticks in %d.%03d s over %d reads = %d.%d a second\n", phase ? "reading" : "idle", (int)total, (int)(ms / 1000), (int)(ms % 1000), n,
               (int)(total * 1000 / (ms ? ms : 1)), (int)((total * 10000 / (ms ? ms : 1)) % 10));
        if (phase == 1) { command(0x09); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll(); }   /* Pause */
    }
    ExitCriticalSection();
    printf("Done.\n");
    for (;;) { }
}
