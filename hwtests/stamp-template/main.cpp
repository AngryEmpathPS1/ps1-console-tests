/*
 * misc/stamp-template - the stamp-and-scatter instrument as a test to copy (2026-11-16, the SPU session). stamp.h has the tools
 * and their two traps. To make a test: copy this folder, rename it (and TARGET in its Makefile), change the title line, and write
 * trial(). The example here is real and runs: Timer 1 counting horizontal blanks, read in the array loop from a scattered start -
 * each line places the next two hblanks between two reads ~22 cycles apart, so their spacing is the scanline's length in CPU
 * cycles (~2,150) to a read.
 *   E trial t lo1 hi1 lo2 hi2   the trial's start t; each step's read before at lo and first read after at hi (cycles from t)
 * THE RULE: run here first: `node hwtests/run.js stamp-template --frames=2000 --quiet=1000`.
 */
#include "stamp.h"

#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u

static void trial(int id) {
    idle(2000u + rnd(1536)); scatter(rnd(48));                     /* a scattered start */
    const uint16_t before = read16(T1_COUNT);
    const uint32_t t0 = now();
    STAMP_LOOP(250, T1_COUNT);                                      /* nothing else inside the timed stretch */
    uint32_t lo[2] = { 0, 0 }, hi[2] = { 0, 0 }; int n = 0;
    stamp_scan(t0, before, [&](int i, uint32_t t, uint16_t v, uint32_t turn) { if (n < 2) { lo[n] = t - turn - t0; hi[n] = t - t0; n++; } });
    pr_s("E"); pr_d(id); pr_u(t0);                                  /* print only after the scan (stamp.h's trap 1b) */
    for (int k = 0; k < n; k++) { pr_u(lo[k]); pr_u(hi[k]); }
    pr_nl();
}

int main() {
    printf("\nmisc/stamp-template\n");
    printf("Timer 1 counting hblanks, each step placed between two reads: E trial t lo1 hi1 lo2 hi2. No assertions.\n\n");
    EnterCriticalSection();
    stamp_begin();
    const uint16_t m1 = read16(T1_MODE);
    write16(T1_MODE, 0x0100);                                       /* Timer 1 counts hblanks */
    for (int id = 0; id < 40; id++) trial(id);
    write16(T1_MODE, m1);
    stamp_end();
    ExitCriticalSection();
    printf("\nmisc/stamp-template: Done.\n");
    for (;;) VSync(0);
}
