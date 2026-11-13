/*
 * cpu/pmsplit-replay - PS1-DOS's faster 386 core (pmsplit-sp) in its register phase, replayed from a snapshot: its own code at its
 * own addresses, its registers and every data word it touches as they were at one point of its periodic path, then stopped at the
 * M-th visit of a chosen PC. Timed from a cold cache on Timer 2 at system clock / 8.
 *
 * WHY (2026-11-13; STATUS "The PSone back"). pmsplit-sp runs +3.5 % on the second SCPH-5501 against this core in that phase
 * (experiments/pmsplit_race.js: phase 1 1.0349 on v0.377.346), ~1.24 cycles a cache miss; every pattern in its hot lines matches
 * alone (icache-place11 .. -place13), so the gap is in how they combine. The phase's path is exactly periodic - 64,639
 * instructions, 158 cache lines in 36 KB (experiments/pm386_pathtrace.js) - and depends on no I/O but a Timer 2 read whose value
 * only adds into a counter (t2_poll), so it replays exactly from a snapshot (experiments/pm386_snap.js: S = 8001_3504h, 141 data
 * words in 18 runs). Timing to the M-th visit of S for M = 1 .. 16 gives the gap's curve along the path, five points a period.
 *
 * HOW. snap.h (experiments/pm386_replaygen.js) holds the code (8001_3000h-8001_C000h), the data runs and the registers. The end
 * PC E and the one after it are patched to `j STUB ; nop`; the stub (three cache lines no hot line uses) runs the two displaced
 * instructions, counts k1 down (the hot code never touches k0 / k1) and goes back to E+8 or out through rp_exit, which reads
 * Timer 2 before anything else. The driver runs on its own stack (pmsplit-sp's, at 801F_FE54h, moved to 801B_FE54h with every pointer into it: the runner and the monitor live up there). For each M, five runs: data
 * restored, FlushCache, launch. The rows hold min and max in system clocks (Timer 2 x 8).
 *
 * READ IT LIKE THIS. Console / emulator for each M: a ratio that climbs evenly along the path is a gap spread over every miss; a
 * step between two M is where to look next (a finer end PC there). 2026-11-13, Claude (AI) for AngryEmpathPS1.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include "snap.h"

#define I_MASK  (*(volatile uint32_t *)0x1F801074)

extern "C" {
void on_stack(void (*fn)(), void *top);
void bios_flush();
uint32_t rp_launch(const uint32_t *regs, uint32_t entry, uint32_t count);
void rp_exit();
extern uint32_t rp_t1;   /* Timer 1 (hblanks) at the exit, for long runs */
}

static uint32_t stk[2048];

static void body() {
    printf("\ncpu/pmsplit-replay\n");
    printf("S %08x E %08x stub %08x period %u, E %u times a period\n", (unsigned)S_PC, (unsigned)E_PC, (unsigned)STUB_AT, (unsigned)PERIOD, (unsigned)E_VISITS);
    EnterCriticalSection();
    uint32_t mask = I_MASK;
    I_MASK = 0;
    volatile uint32_t *code = (volatile uint32_t *)CODE_AT;
    for (uint32_t i = 0; i < CODE_N; i++) code[i] = code_words[i];
    const uint32_t e0 = code_words[(E_PC - CODE_AT) >> 2], e1 = code_words[((E_PC - CODE_AT) >> 2) + 1];
    code[(E_PC - CODE_AT) >> 2] = 0x08000000u | ((STUB_AT >> 2) & 0x03FFFFFFu);   /* j STUB */
    code[((E_PC - CODE_AT) >> 2) + 1] = 0;                                         /* nop */
    volatile uint32_t *st = (volatile uint32_t *)STUB_AT;
    const uint32_t back = 0x08000000u | (((E_PC + 8) >> 2) & 0x03FFFFFFu);
    const uint32_t out = 0x08000000u | ((((uint32_t)rp_exit) >> 2) & 0x03FFFFFFu);
    st[0] = e0; st[1] = e1;
    st[2] = 0x277BFFFFu;          /* addiu k1, k1, -1 */
    st[3] = 0x13600003u;          /* beq k1, zero, +3 (st[7]) */
    st[4] = 0;
    st[5] = back; st[6] = 0;      /* j E+8 */
    st[7] = out; st[8] = 0;       /* j rp_exit */
    for (uint32_t mi = 0; mi < M_N; mi++) {
        const uint32_t m = m_list[mi];
        uint32_t tmin = ~0u, tmax = 0, hmin = ~0u, hmax = 0;
        for (int r = 0; r < 6; r++) {
            for (uint32_t k = 0; k < RUNS_N; k++) {
                volatile uint32_t *d = (volatile uint32_t *)runs[k].at;
                for (uint32_t i = 0; i < runs[k].n; i++) d[i] = run_words[runs[k].off + i];
            }
            bios_flush();
#ifdef NO_LAUNCH
            uint32_t t = 0; (void)m;
#else
            uint32_t t = rp_launch(snap_regs, S_PC, m) * 8;
#endif
            const uint32_t hb = rp_t1 & 0xFFFF;
            if (r == 0) continue;
            if (hb < hmin) hmin = hb; if (hb > hmax) hmax = hb;
            if (t < tmin) tmin = t; if (t > tmax) tmax = t;
        }
        printf("M %2u  min %u max %u\n", (unsigned)m, (unsigned)tmin, (unsigned)tmax);
    }
    I_MASK = mask;
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}

int main() {
    on_stack(body, stk + 2048 - 16);
    for (;;) {}
}
