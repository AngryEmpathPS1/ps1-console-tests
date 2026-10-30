/*
 * spu/spu-busy - SPUSTAT bit 10 after a manual write, every FIFO length, against the sample clock (2026-10-07).
 *
 * WHY (STATUS M372, "Not modelled"). spu-xfer's BUSY rows on all three consoles: N halfwords put in the FIFO, then
 * SPUCNT mode 1 - the mode applied 376-920 cycles after the write (once a sample, M372), bit 10 set at the apply for
 * N = 16 and 32 and clearing ~620 / ~1,430 cycles after it, and N <= 8 clear at the apply. Neither "the FIFO drains from
 * the first write" nor "from the apply" fits all three, and six lengths at one unknown sample phase cannot say what
 * does. This core never raises bit 10 for a manual write. Games that wait on it (the BIOS waits on the mode, not bit 10;
 * a game's own driver may) wait for nothing here.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, unwrapped (1 cycle). Each trial: Stop (mode 0 applied), the
 * transfer address 4000h, N halfwords into the FIFO, then the CPU waits for SPUSTAT bit 11 to fall (a sample boundary
 * it can see) and D cycles more, writes SPUCNT mode 1, and reads SPUSTAT back to back: the cycle the mode bits show (the
 * apply), whether bit 10 is up then, and the cycle it clears (both from the SPUCNT write).
 *   LENGTH   N = 1..32, D = 0, twice each.
 *   PHASE    N = 8, 16, 24, 32 at D = 0, 96 ... 672 (a sample is 768), twice each.
 *   WAIT     N = 32, the SPUCNT write 0, 1,000, 4,000 and 16,000 cycles after the last FIFO write (D = 0 after the edge
 *            that follows), twice each: does the FIFO drain before mode 1?
 *
 * READ IT LIKE THIS. A drain at a fixed rate from the apply gives "clear" growing by that rate a halfword, the same at
 * every phase; one in the SPU's own RAM slots (24 a sample, M372) grows in steps of a sample and moves with D; one from
 * the first FIFO write shortens as WAIT grows. N where bit 10 first shows at the apply is the FIFO's free depth.
 *
 * ON THIS CORE AT v0.377.14 (2026-10-07): bit 10 never up, at any N, phase or wait (busy at the apply 0, clear = the
 * apply) - as STATUS M372 says. The apply once a sample at a fixed phase: at D = 0 82-154 cycles after the write (the
 * write lands before SPU_STAT_PHASE in its sample), then from D = 96 on 807 / 732 / 658 / 586 / 442 / 298-379 / 226-307
 * as D grows - write + apply ~ 900 cycles after the edge, the next sample's phase point. A console's LENGTH rows say
 * where bit 10 first shows and how its clear grows with N; its PHASE rows whether that moves with the sample clock.
 * THE RULE: run here first: `node hwtests/run.js spu-busy --frames=4000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }

static uint16_t cnt = 0xC000;
static void stop() {
    cnt = (uint16_t)(cnt & ~0x30);
    w16(SPUCNT, cnt);
    const uint32_t t0 = now();
    while (((r16(SPUSTAT) & 0x3F) != (cnt & 0x3F) || (r16(SPUSTAT) & 0x400)) && now() - t0 < 200000) {}
}
/* SPUSTAT bit 11 falling: returns the time it was seen. */
static uint32_t fall() {
    while (!(r16(SPUSTAT) & 0x0800)) now();
    while (r16(SPUSTAT) & 0x0800) now();
    return now();
}

struct Row { uint16_t apply, clear; uint8_t busyAtApply; };
/* One trial: N halfwords, mode 1 written D cycles after the falling edge (W cycles at least after the last FIFO write). */
static Row trial(int n, uint32_t D, uint32_t W) {
    stop();
    w16(0x1A6, 0x4000 / 8);
    for (int i = 0; i < n; i++) w16(0x1A8, (uint16_t)(0x1234 + i));
    const uint32_t tl = now();
    if (W) while (now() - tl < W) {}
    const uint32_t te = fall();
    while (now() - te < D) {}
    const uint32_t t0 = now();
    cnt = (uint16_t)((cnt & ~0x30) | 0x10);
    w16(SPUCNT, cnt);                                           /* mode 1: manual write */
    Row r = { 0xFFFF, 0xFFFF, 0 };
    bool applied = false;
    for (;;) {
        const uint16_t s = r16(SPUSTAT);
        const uint32_t t = now() - t0;
        if (!applied && (s & 0x3F) == (cnt & 0x3F)) { applied = true; r.apply = (uint16_t)t; r.busyAtApply = (s & 0x400) ? 1 : 0; }
        if (applied && !(s & 0x400)) { r.clear = (uint16_t)t; break; }
        if (t > 60000) break;
    }
    return r;
}

static Row lenRows[32][2], phaseRows[4][8][2], waitRows[4][2];

int main() {
    printf("\nspu/spu-busy\n");
    printf("SPUSTAT bit 10 after a manual write: N halfwords in the FIFO, SPUCNT mode 1 written D cycles after SPUSTAT bit 11\n");
    printf("falls; the cycles from that write to the mode's apply and to bit 10 clearing. No assertions.\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    w16(0x1AC, 0x0004);                                         /* transfer type Normal */
    w16(SPUCNT, cnt); stop();

    for (int rep = 0; rep < 2; rep++)
        for (int n = 1; n <= 32; n++) lenRows[n - 1][rep] = trial(n, 0, 0);
    static const int PN[4] = { 8, 16, 24, 32 };
    for (int rep = 0; rep < 2; rep++)
        for (int p = 0; p < 4; p++)
            for (int k = 0; k < 8; k++) phaseRows[p][k][rep] = trial(PN[p], (uint32_t)k * 96, 0);
    static const uint32_t WW[4] = { 0, 1000, 4000, 16000 };
    for (int rep = 0; rep < 2; rep++)
        for (int k = 0; k < 4; k++) waitRows[k][rep] = trial(32, 0, WW[k]);
    stop();

    printf("\nLENGTH (D = 0): N  apply  busy at it  clear   (two runs)\n");
    for (int n = 1; n <= 32; n++) {
        const Row* r = lenRows[n - 1];
        printf("  N=%2d  %4d %d %5d   %4d %d %5d\n", n, r[0].apply, r[0].busyAtApply, r[0].clear, r[1].apply, r[1].busyAtApply, r[1].clear);
    }
    printf("\nPHASE: N, D  apply  busy at it  clear   (two runs)\n");
    for (int p = 0; p < 4; p++)
        for (int k = 0; k < 8; k++) {
            const Row* r = phaseRows[p][k];
            printf("  N=%2d D=%3d  %4d %d %5d   %4d %d %5d\n", PN[p], k * 96, r[0].apply, r[0].busyAtApply, r[0].clear,
                   r[1].apply, r[1].busyAtApply, r[1].clear);
        }
    printf("\nWAIT (N = 32): cycles from the last FIFO write to the next edge, then mode 1: apply  busy at it  clear\n");
    for (int k = 0; k < 4; k++) {
        const Row* r = waitRows[k];
        printf("  W=%5d  %4d %d %5d   %4d %d %5d\n", (int)WW[k], r[0].apply, r[0].busyAtApply, r[0].clear, r[1].apply, r[1].busyAtApply, r[1].clear);
    }

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-busy: Done.\n");
    for (;;) VSync(0);
}
