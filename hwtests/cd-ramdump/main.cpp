/*
 * cdrom/cd-ramdump - the drive program's whole RAM, read over the command port, in five states (2026-11-02)
 *
 * WHY. Under its own program (CDROMLLE) this emulator's drive IS the program: its RAM is the program's RAM, so a dump taken the
 * same way on a console can be compared byte for byte. docs/DRIVE-PROGRAM.md names what is in it: the Q-minus-header lag the
 * program measures at every spin-up ($01CB-$01CD; the model's DATA_LAG gives 01, psx-spx saw 01, 02 and 06 on nocash's console),
 * the focus balance and gain ($01E9, $01EA; the model assumes 4), the tick count ($60), the speed and state flags. Bytes that
 * change with time (the tick, Q) will differ; the ones that do not are the drive's own measurements and its state machine.
 *
 * WHAT IT DOES. 19h,60h,lo,hi (psx-spx "Read HC05 SUB-CPU RAM and I/O Ports") for every byte of 040h-23Fh, in five states: idle
 * after Init; a SeekL to 00:20:00 at single speed; after reading 20 sectors there and a Pause; a SeekL to 30:00:00 at double
 * speed; a Stop from double speed. Each row: R state address, the time it began (ms since Init), 16 bytes, -- where no answer.
 * Interrupts off, Timer 2 at the system clock / 8; every wait bounded (rule 1). No seek past 30:00:00. No hands.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-ramdump`.
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

/* Wait for a given INT, acknowledging others; returns it (5 = error, 0 = none in 10 s). */
static uint8_t waitInt(uint8_t want) {
    const uint32_t c0 = clk8;
    for (;;) {
        tick();
        uint8_t f = irqFlag() & 7;
        if (!f) { if (clk8 - c0 > 42336000u) return 0; continue; }
        if (f == want || f == 5) return f;
        /* an old board's flag can read 1 before 3 (psx-spx "Unstable IRQ Flag polling"): read it again first */
        { const uint8_t g = irqFlag() & 7; if (g == want || g == 5) return g; }
        drain(); ackAll();
    }
}
static void simple(uint8_t cmd) { command(cmd); waitInt(3); drain(); ackAll(); }
/* the door's own INT5 (08h), unasked: noted and skipped */
static int doorInts;
static bool takeDoor(uint8_t f, uint8_t b1) { if (f == 5 && b1 == 0x08) { doorInts++; return true; } return false; }
/* a second response a command may still send (ReadN's sectors, an INT2): taken and dropped for t */
static void settle(uint32_t t) { const uint32_t t0 = now(); while (now() - t0 < t) { if (irqFlag() & 7) { drain(); ackAll(); } } }

/* 19h,60h,lo,hi: a byte of the drive's RAM; -1 when no answer in 50 ms */
static int peek(uint16_t addr) {
    for (int i = 0; i < 4 && (irqFlag() & 7); i++) { const uint8_t f = irqFlag() & 7, b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF; (void)b0; drain(); ackAll(); takeDoor(f, b1); }
    param(0x60); param(addr & 0xFF); param(addr >> 8);
    command(0x19);
    const uint32_t t0 = now();
    for (;;) {
        const uint8_t f = irqFlag() & 7;
        if (f) {
            const uint8_t b0 = hasResp() ? resp() : 0xFF, b1 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            if (takeDoor(f, b1)) continue;
            return f == 3 ? b0 : -1;
        }
        if (now() - t0 > SECOND / 20) return -1;
    }
}
static uint32_t t0;
static void dump(char state) {
    for (uint16_t a = 0x040; a < 0x240; a += 16) {
        const uint32_t at = ms(now() - t0);
        int v[16];
        for (int i = 0; i < 16; i++) v[i] = peek(a + i);
        printf("R %c %03x %6d", state, a, (int)at);
        for (int i = 0; i < 16; i++) { if (v[i] < 0) printf(" --"); else printf(" %02x", v[i]); }
        printf("\n");
    }
}
static void seekL(uint8_t m, uint8_t s) {
    param(m); param(s); param(0x00); simple(0x02);                /* Setloc */
    command(0x15); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* SeekL */
}

int main() {
    printf("\ncdrom/cd-ramdump\n");
    printf("The drive program's RAM 040h-23Fh by 19h,60h, in five states: A idle after Init, B SeekL 00:20:00 at 1x, C after ReadN and Pause,\n");
    printf("D SeekL 30:00:00 at 2x, E Stop from 2x. R state address ms-since-Init 16 bytes (-- no answer).\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    t0 = now();
    simple(0x0C);                                                   /* Demute */
    settle(SECOND * 2);
    dump('A');
    seekL(0x00, 0x20);
    settle(SECOND);
    dump('B');
    command(0x06); waitInt(3); drain(); ackAll();                   /* ReadN: twenty sectors at 1x */
    settle(SECOND * 20 / 75);
    command(0x09); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Pause */
    settle(SECOND / 2);
    dump('C');
    param(0x80); simple(0x0E);                                      /* Setmode: double speed */
    seekL(0x30, 0x00);
    settle(SECOND);
    dump('D');
    command(0x08); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Stop */
    settle(SECOND);
    dump('E');
    param(0x00); simple(0x0E);                                      /* back to single speed, and the motor on for the next test */
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();
    ExitCriticalSection();
    printf("door interrupts %d\n", doorInts);
    printf("Done.\n");
    for (;;) { }
}
