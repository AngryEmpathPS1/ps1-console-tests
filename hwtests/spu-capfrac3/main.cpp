/*
 * spu/spu-capfrac3 - THE SCPH-1000 / 1001'S SECOND DEAD STATE: AFTER A TYPE-0 HOLD NO CAPTURE RING FIRES AT ANY EIGHTH (2026-11-25, the
 * SPU session). For the old SPU only (the others run it harmlessly: types 0 and 1 stop their ring).
 *
 * WHY. hwtests/spu-capfrac / -capfrac2: a capture ring's write raises IRQ9 only at fraction zero of the ring's counter, on ten consoles.
 * But the SCPH-1000, left at fraction 4/8 and then held at transfer type 0 for 8 samples, did not fire after any of nine more Rep8
 * samples (both 1001s did after four, as counted), and the first 1001 AS FOUND (after the day's type-0 / 1 holds) did not in nine
 * either - then did, five Rep8 samples after four one-sample type-1 holds. On these two models types 0 and 1 put the four captures at
 * 0000h / 1000h / 2000h / 3000h and 0000h / 0800h / 1000h / 1800h + the counter (SPU_CAP_OLD). Does the IRQ's compare stay in that
 * geometry? What sets the state, and what clears it?
 *
 * WHAT IT DOES. Interrupts off, voices keyed off, the reverb out of the way, Timer 2 the system clock / 8. A SWEEP is nine steps
 * (s = 0 .. 8, a one-sample Rep8 hold before each but the first); at each step IRQ9 is armed in turn at 08F8h, 10F8h, 20F8h (voice
 * 1's ring by the Normal, the type-1 and the type-0 geometry) for 2,200 samples each:
 *   S stage s t(08F8) d t(10F8) d t(20F8) d      samples to the first showing and from there to the next (-1 none): 512 or 2,048
 *                                                apart is a ring's write
 * STAGES: 0 as found; 1 after ONE one-sample type-1 hold; 2 after four Rep8 samples and a type-0 hold of 8 samples; 3 after a type-1
 * hold of 8 samples; 4 after a type-0 hold of ONE sample at fraction 4.
 *   Z k fired          last: Rep8 holds until 0808h fires (9 at most; a one-sample type-1 hold first if the nine fail, then nine more)
 * It MOVES THE RING. Every wait is bounded. No assertions. About 25 s.
 * THE RULE: run here first: `node hwtests/run.js spu-capfrac3 --ntsc --frames=3000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define SAMPLE 96u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t u) { const uint32_t t0 = now(); while (now() - t0 < u) {} }

static uint16_t cnt;
static int16_t res[60][8]; static int nr = 0;
static int16_t zr[24][2]; static int nz = 0;

static int32_t waitIrq(uint32_t limit) {
    const uint32_t t0 = now();
    while (now() - t0 < limit) { if (read16(SPU(0x1AE)) & 0x0040) return (int32_t)((now() - t0 + 48) / SAMPLE); }
    return -1;
}
static void arm2(uint32_t addr, int16_t* out) {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(4 * SAMPLE);
    write16(SPU(0x1A4), (uint16_t)(addr / 8));
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const int32_t t1 = waitIrq(2200u * SAMPLE); int32_t d = -1;
    if (t1 >= 0) {
        const uint32_t a = now();
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(16 * SAMPLE); write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
        if (waitIrq(2200u * SAMPLE) >= 0) d = (int32_t)((now() - a + 48) / SAMPLE);
    }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    out[0] = (int16_t)t1; out[1] = (int16_t)d;
}
static void hold(uint16_t value, uint32_t units) {
    write16(SPU(0x1AC), value);
    idle(units);
    write16(SPU(0x1AC), 0x0004);
    idle(8 * SAMPLE);
}
static void sweep(int stage) {
    for (int s = 0; s <= 8; s++) {
        if (s) hold(0x000A, SAMPLE - 4);
        int16_t* r = res[nr++];
        r[0] = (int16_t)stage; r[1] = (int16_t)s;
        arm2(0x08F8, r + 2); arm2(0x10F8, r + 4); arm2(0x20F8, r + 6);
    }
}
static int fires() { int16_t o[2]; write16(SPU(0x1A4), 0); arm2(0x0808, o); return o[0] >= 0; }

int main() {
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200); lastT = read16(T2_COUNT); clk = 0;
    const uint16_t mbase = read16(SPU(0x1A2)), cnt0 = read16(SPU(0x1AA)), stat0 = read16(SPU(0x1AE)), irqa = read16(SPU(0x1A4)), dac = read16(SPU(0x1AC));
    static uint16_t rvb[32];
    for (int i = 0; i < 32; i++) rvb[i] = read16(SPU(0x1C0 + 2 * (uint32_t)i));
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), 0);
    write16(SPU(0x1A2), 0xFFFE);
    cnt = (uint16_t)(cnt0 & ~0x0070);
    write16(SPU(0x1AA), cnt);

    sweep(0);
    hold(0x0002, SAMPLE - 4); sweep(1);
    for (int k = 0; k < 4; k++) hold(0x000A, SAMPLE - 4);
    hold(0x0000, 8 * SAMPLE); sweep(2);
    hold(0x0002, 8 * SAMPLE); sweep(3);
    for (int k = 0; k < 4; k++) hold(0x000A, SAMPLE - 4);
    hold(0x0000, SAMPLE - 4); sweep(4);
    for (int pass = 0; pass < 2; pass++) {
        int ok = 0;
        for (int k = 0; k <= 9; k++) {
            const int f = fires();
            if (nz < 24) { zr[nz][0] = (int16_t)(pass * 100 + k); zr[nz][1] = (int16_t)f; nz++; }
            if (f) { ok = 1; break; }
            hold(0x000A, SAMPLE - 4);
        }
        if (ok) break;
        hold(0x0002, SAMPLE - 4);
    }

    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), rvb[i]);
    write16(SPU(0x1A2), mbase);
    write16(SPU(0x1A4), irqa);
    write16(SPU(0x1AA), cnt0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-capfrac3\nThe old SPU's second dead state: S stage s t d (08F8h) t d (10F8h) t d (20F8h), samples; Z k fired. No assertions.\n\n");
    printf("M %04x %04x %04x %04x\n", mbase, cnt0, stat0, dac);
    for (int k = 0; k < nr; k++) printf("S %d %d %d %d %d %d %d %d\n", res[k][0], res[k][1], res[k][2], res[k][3], res[k][4], res[k][5], res[k][6], res[k][7]);
    for (int k = 0; k < nz; k++) printf("Z %d %d\n", zr[k][0], zr[k][1]);
    printf("\nspu/spu-capfrac3: Done.\n");
    for (;;) VSync(0);
}
