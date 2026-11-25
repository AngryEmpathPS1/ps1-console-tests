/*
 * spu/spu-capfrac2 - THE RING COUNTER'S FRACTION AS FOUND, WHAT THE OTHER TRANSFER TYPES DO TO IT, AND THE IRQ INSIDE A HOLD
 * (2026-11-25, the SPU session). It LEAVES THE CONSOLE'S CAPTURE RINGS FIRING (fraction zero): run it to put a console right.
 *
 * WHY. hwtests/spu-capfrac on the SCPH-9001 and the third 5501: a capture ring's write raises IRQ9 only while the ring counter's
 * fraction - the eighths a Rep-type hold leaves under it (SPU_CAP_FRAC) - is zero. After k one-sample Rep8 holds the 5501 fired at
 * k = 0, 8, 16 only; the 9001, silent since hwtests/spu-capstep the day before, at k = 7, 15. Rep4 moved it two, Rep2 four; SPUCNT
 * bit 15 off and on and a Fill hold did not move it. Open after that: the fraction each console was left at; whether types 0 / 1 /
 * 6 / 7 move it (on the SCPH-1000 and 1001 type 1 steps the ring 4 every second sample); and which sample raises the IRQ INSIDE a hold.
 *
 * WHAT IT DOES. Interrupts off, voices keyed off, the reverb out of the way, Timer 2 the system clock / 8 (96 a sample).
 *   M mbase spucnt spustat xfer
 *   Z s k fired               stage s (0 as found; 1-4 after the T stage of that number; 5 after D): one-sample Rep8 holds until IRQ9
 *                             armed at 0808h fires (9 at most): k = 0 it fired as found; the fraction found is (8 - k) mod 8 eighths
 *   T ty k fired ph           from fraction zero: the k-th one-sample hold of type ty (1, 0, 6, 7), fired 1 / 0, and the T2 time of
 *                             SPUSTAT bit 11's next turn mod 256 samples (units of 8 cycles: a ring that stood a sample is 96 later)
 *   D n t1 t2 ...             a Rep8 hold of 4,400 samples (the ring once round at an eighth of its pace), IRQ9 armed at 0808h all
 *                             through, each showing acknowledged over one sample: n showings, the first twelve in T2 units from the
 *                             hold's start (8 in a row 192 apart: every sample the ring stands there; one: one fraction's)
 * It MOVES THE RING (a boot's draw is not left as it was). Every wait is bounded. No assertions. About 5 s.
 * THE RULE: run here first: `node hwtests/run.js spu-capfrac2 --ntsc --frames=1500 --quiet=900`.
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
static char tag[100]; static int32_t rv[100][4]; static int nr = 0;
static void put(char t, int32_t a, int32_t b, int32_t c, int32_t d) { if (nr < 100) { tag[nr] = t; rv[nr][0] = a; rv[nr][1] = b; rv[nr][2] = c; rv[nr][3] = d; nr++; } }

static int32_t armed() {   /* samples to IRQ9 at 0808h, -1: none in 600 */
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(4 * SAMPLE);
    write16(SPU(0x1A4), 0x0808 / 8);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const uint32_t t0 = now(); int32_t r = -1;
    while (now() - t0 < 600u * SAMPLE) { if (read16(SPU(0x1AE)) & 0x0040) { r = (int32_t)((now() - t0) / SAMPLE); break; } }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    return r;
}
static int32_t phase() {
    uint16_t b = read16(SPU(0x1AE)) & 0x0800; const uint32_t t0 = now();
    while (now() - t0 < 600u * SAMPLE) { const uint16_t x = read16(SPU(0x1AE)) & 0x0800; if (x != b) return (int32_t)(now() % (256u * SAMPLE)); }
    return -1;
}
static void hold(uint16_t value, uint32_t units) {
    write16(SPU(0x1AC), value);
    idle(units);
    write16(SPU(0x1AC), 0x0004);
    idle(8 * SAMPLE);
}
static void toZero(int s) {
    for (int k = 0; k <= 9; k++) {
        const int f = armed() >= 0;
        if (f || k == 9) { put('Z', s, k, f, 0); return; }
        hold(0x000A, SAMPLE - 4);
    }
}
static void typeStage(int s, int ty, int n) {
    for (int k = 1; k <= n; k++) { hold((uint16_t)(ty << 1), SAMPLE - 4); const int32_t f = armed(); put('T', ty, k, f >= 0, phase()); }
    toZero(s);
}

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

    toZero(0);
    typeStage(1, 1, 4);
    typeStage(2, 0, 2);
    typeStage(3, 6, 2);
    typeStage(4, 7, 2);

    static int32_t dt[12]; int dn = 0;
    {
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(4 * SAMPLE);
        write16(SPU(0x1A4), 0x0808 / 8);
        write16(SPU(0x1AC), 0x000A);
        const uint32_t t0 = now();
        write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
        while (now() - t0 < 4400u * SAMPLE) {
            if (read16(SPU(0x1AE)) & 0x0040) {
                if (dn < 12) dt[dn] = (int32_t)(now() - t0);
                dn++;
                write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(SAMPLE); write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
            }
        }
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
        write16(SPU(0x1AC), 0x0004);
        idle(8 * SAMPLE);
    }
    toZero(5);

    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), rvb[i]);
    write16(SPU(0x1A2), mbase);
    write16(SPU(0x1A4), irqa);
    write16(SPU(0x1AA), cnt0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-capfrac2\nThe ring counter's fraction as found, what types 1 / 0 / 6 / 7 do to it, the IRQ inside a Rep8 hold: Z s k fired; T ty k fired ph; D n t.. . No assertions.\n\n");
    printf("M %04x %04x %04x %04x\n", mbase, cnt0, stat0, dac);
    for (int k = 0; k < nr; k++) {
        if (tag[k] == 'Z') printf("Z %d %d %d\n", (int)rv[k][0], (int)rv[k][1], (int)rv[k][2]);
        else printf("T %d %d %d %d\n", (int)rv[k][0], (int)rv[k][1], (int)rv[k][2], (int)rv[k][3]);
    }
    printf("D %d", dn);
    for (int i = 0; i < dn && i < 12; i++) printf(" %d", (int)dt[i]);
    printf("\n\nspu/spu-capfrac2: Done.\n");
    for (;;) VSync(0);
}
