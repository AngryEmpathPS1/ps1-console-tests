/*
 * cdrom/cd-stop - NOT A TEST: the drive's motor off between CD tests (2026-10-12).
 *
 * WHY. On the borrowed SCPH-9001's unattended runs, Unirom leaves the disc spinning for as long as it sits in the
 * drive, and an overnight run reads the CD for an hour of its eight. The PC side
 * sends this through the runner whenever its next program does not read the disc, so the motor runs only for the CD
 * tests. It is not in CONSOLE-RUN-SHEET.md's table: no console owes a log of it, and nothing grades it.
 *
 * WHAT IT DOES. Interrupts off (the kernel's CD handler would take the acknowledges), every flag acknowledged and every
 * response drained; Getstat, then Stop (08h) to its INT2 - psx-spx: INT3 at once, INT2 when the spindle has stopped -
 * then Getstat again. Each wait is bounded (a drive that does not answer costs 3 s, a Stop 10 s), and a first flag of 1
 * or 4 is read again before it is taken, since an older board can show 3 or 5 half set (psx-spx "Unstable IRQ Flag
 * polling"; CLAUDE.md's console rule 1). Prints the two status bytes - bit 1 is the motor - and "Done.".
 *
 * Run here first: `node hwtests/run.js cd-stop --disc` (the motor bit 1 before, 0 after).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int i = 0; i < 32 && hasResp(); i++) resp(); }

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

/* The next flag within `limit` cycles (0: none), its first response byte in *stat; drained and acknowledged. */
static uint8_t take(uint32_t limit, uint8_t *stat) {
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
    *stat = hasResp() ? resp() : 0;
    drain();
    ackAll();
    return f;
}

int main() {
    printf("\ncdrom/cd-stop: the drive's motor off (not a test)\n");
    EnterCriticalSection();
    write16(T2_MODE, 0);
    now();
    ackAll(); drain();
    uint8_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    command(0x01);
    const uint8_t f0 = take(MS(3000), &s0);
    ackAll(); drain();
    command(0x08);
    const uint8_t f1 = take(MS(3000), &s1);
    const uint8_t f2 = f1 == 3 ? take(MS(10000), &s2) : 0;
    idle(MS(100));
    ackAll(); drain();
    command(0x01);
    const uint8_t f3 = take(MS(3000), &s3);
    ackAll(); drain();
    printf("  Getstat INT%d %02x (motor %s); Stop INT%d %02x, INT%d %02x; Getstat INT%d %02x (motor %s)\n",
           f0, s0, (s0 & 2) ? "on" : "off", f1, s1, f2, s2, f3, s3, (s3 & 2) ? "on" : "off");
    ExitCriticalSection();
    printf("Done.\n");
    return 0;
}
