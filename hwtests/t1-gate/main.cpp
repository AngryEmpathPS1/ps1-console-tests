/*
 * timer/t1-gate - Timer 1 at the system clock in sync mode 1, watched straight through the v-blank: when it is reset,
 * and whether it is held at 0 for the gate (2026-10-08, after t1-edges).
 *
 * WHY. t1-edges dated the first drop of this counter after the VBlank IRQ - which is its 16-bit wrap, not the reset - and
 * the SCPH-5501's came 4,287 cycles before this core's (two lines are 4,305): the counter restarted about two lines
 * earlier there. This core resets it where the v-blank gate ENDS (M236) and timer-sync's hblank-clocked mode 1 rows
 * ("261 0x2 1": the counter 0 for two lines) agree with the consoles - so does the system-clock count restart at the
 * gate's START, held or not?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Each frame: the VBlank edge polled (I_STAT bit 0), then
 * Timer 1 and Timer 2 read in a tight loop for 40 lines (~86,000 cycles), and each event kept: a DROP (Timer 1 below the
 * read before: its value before and after - a wrap leaves FFFFh-ish, a reset anything), and a HOLD (Timer 1 the same at
 * two reads more than 30 cycles apart: from when, to when, at what value). 16 frames.
 *
 * READ IT LIKE THIS. Every time is cycles from the polled IRQ edge. A reset at the gate's start then free counting is
 * one DROP from a mid value; a reset held through the gate is a DROP then a HOLD at 0 for the gate's length, counting
 * after it. This core: the DROP and HOLD where its gate is; a console: where its own is.
 *
 * THE RULE: run it here first: `node hwtests/run.js t1-gate --ntsc --frames=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_STAT   0x1F801070u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

#define FRAMES 16
#define EV 12
struct Ev { char kind; uint32_t t0, t1; uint16_t a, b; };
static Ev ev[FRAMES][EV];
static int nev[FRAMES];

int main() {
    printf("\ntimer/t1-gate\n");
    printf("Timer 1 (system clock, sync mode 1) through the v-blank: DROPs (value before -> after) and HOLDs, cycles from the polled IRQ. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0000);
    lastT = read16(T2_COUNT); clk = 0;
    write16(T1_MODE, 0x0003);
    for (int f = -1; f < FRAMES; f++) {
        write32(I_STAT, ~1u);
        const uint32_t w0 = now();
        while (!(read32(I_STAT) & 1)) if (now() - w0 > 1200000u) break;
        const uint32_t irq = now();
        write32(I_STAT, ~1u);
        if (f < 0) continue;                    /* the first edge only syncs */
        nev[f] = 0;
        uint16_t prev = read16(T1_COUNT);
        uint32_t tp = now(), holdFrom = 0; bool holding = false;
        for (;;) {
            const uint16_t v = read16(T1_COUNT);
            const uint32_t t = now();
            if (v < prev && nev[f] < EV) { Ev& e = ev[f][nev[f]++]; e.kind = 'D'; e.t0 = tp - irq; e.t1 = t - irq; e.a = prev; e.b = v; }
            if (v == prev) { if (!holding) { holding = true; holdFrom = tp; } }
            else if (holding) {
                holding = false;
                if (t - holdFrom > 30 && nev[f] < EV) { Ev& e = ev[f][nev[f]++]; e.kind = 'H'; e.t0 = holdFrom - irq; e.t1 = t - irq; e.a = prev; e.b = v; }
            }
            prev = v; tp = t;
            if (t - irq > 86000u) break;
        }
    }
    write16(T1_MODE, m1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int f = 0; f < FRAMES; f++) {
        printf("frame %2d:", f);
        for (int i = 0; i < nev[f]; i++) {
            const Ev& e = ev[f][i];
            if (e.kind == 'D') printf("  DROP %u..%u %04x->%04x", (unsigned)e.t0, (unsigned)e.t1, e.a, e.b);
            else printf("  HOLD %04x %u..%u", e.a, (unsigned)e.t0, (unsigned)e.t1);
        }
        printf("\n");
    }
    printf("\ntimer/t1-gate: Done.\n");
    for (;;) VSync(0);
}
