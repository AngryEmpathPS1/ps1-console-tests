/*
 * spu/spu-capfrac - DOES A CAPTURE RING'S IRQ DEPEND ON WHAT A REP-TYPE HOLD LEFT IN THE RING'S COUNTER? (2026-11-25, the SPU session)
 *
 * WHY. hwtests/spu-capring4: on four consoles (the first 1001, PSone, 7001, 9001) no capture ring raised IRQ9 though every ring was
 * being written and SPUSTAT bit 11 turned; on six (the 1000, second 1001, both other 5501s, second 7501, 7502) all four rings did.
 * hwtests/spu-regdump: no readable register splits that way. The six had been rebooted since hwtests/spu-capstep ran (2026-11-24);
 * the four had not. spu-capstep holds the Rep transfer types, under which the ring's counter steps once in 2 / 4 / 8 samples: one
 * count of eighths (SPU_CAP_FRAC). A hold that ends between carries leaves a fraction that Normal samples (8 eighths each) never clear.
 * IS THE CAPTURE WRITE'S IRQ RAISED ONLY AT ONE FRACTION?
 *
 * WHAT IT DOES. Interrupts off, voices keyed off, the reverb out of the way (registers zero, mBASE FFFEh), Timer 2 the system clock / 8.
 *   M mbase spucnt spustat xfer
 *   F ty k fired t1 ph        after the k-th hold of transfer type ty (5 Rep8, 4 Rep4, 3 Rep2; k = 0: before any) of about ONE sample:
 *                             IRQ9 armed at 0808h (voice 1's ring) - fired 1 / 0 in 600 samples, t1 samples to it; ph: the T2 time
 *                             (units of 8 cycles, mod 256 samples = 24,576) of SPUSTAT bit 11's next turn - a sample held without a
 *                             step moves it 96 later, so the holds that slipped to two samples or none can be told
 *   G n fired t1              then: 1 = SPUCNT bit 15 cleared for 8 samples and set again; 2 = 1F801DACh 0000h for 8 samples then
 *                             0004h (a Fill hold); each followed by the same arming
 *   Z k fired                 last: Rep8 holds of one sample until the ring fires again (8 at most), to leave the console firing
 * It MOVES THE RING (a boot's draw is not left as it was). Every wait is bounded. No assertions. A few seconds.
 * THE RULE: run here first: `node hwtests/run.js spu-capfrac --ntsc --frames=1200 --quiet=600`.
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
static char tag[120]; static int32_t rv[120][5]; static int nr = 0;
static void put(char t, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e) { if (nr < 120) { tag[nr] = t; rv[nr][0] = a; rv[nr][1] = b; rv[nr][2] = c; rv[nr][3] = d; rv[nr][4] = e; nr++; } }

static int32_t armed() {   /* samples to IRQ9 at 0808h, -1: none in 600 */
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(4 * SAMPLE);
    write16(SPU(0x1A4), 0x0808 / 8);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const uint32_t t0 = now(); int32_t r = -1;
    while (now() - t0 < 600u * SAMPLE) { if (read16(SPU(0x1AE)) & 0x0040) { r = (int32_t)((now() - t0) / SAMPLE); break; } }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    return r;
}
static int32_t phase() {   /* T2 time of bit 11's next turn, mod 256 samples */
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

    { const int32_t f = armed(); put('F', 5, 0, f >= 0, f, phase()); }
    for (int k = 1; k <= 20; k++) { hold(0x000A, SAMPLE - 4); const int32_t f = armed(); put('F', 5, k, f >= 0, f, phase()); }
    for (int k = 1; k <= 8; k++) { hold(0x0008, SAMPLE - 4); const int32_t f = armed(); put('F', 4, k, f >= 0, f, phase()); }
    for (int k = 1; k <= 4; k++) { hold(0x0006, SAMPLE - 4); const int32_t f = armed(); put('F', 3, k, f >= 0, f, phase()); }
    {
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x8000)); idle(8 * SAMPLE); write16(SPU(0x1AA), cnt); idle(8 * SAMPLE);
        int32_t f = armed(); put('G', 1, f >= 0, f, phase(), 0);
        hold(0x0000, 8 * SAMPLE);
        f = armed(); put('G', 2, f >= 0, f, phase(), 0);
    }
    for (int k = 0; k <= 8; k++) {
        const int32_t f = armed(); put('Z', k, f >= 0, f, 0, 0);
        if (f >= 0) break;
        hold(0x000A, SAMPLE - 4);
    }

    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), rvb[i]);
    write16(SPU(0x1A2), mbase);
    write16(SPU(0x1A4), irqa);
    write16(SPU(0x1AA), cnt0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-capfrac\nDoes a capture ring's IRQ depend on what a Rep-type hold left in the ring's counter? F ty k fired t1 ph; G n fired t1 ph; Z k fired. No assertions.\n\n");
    printf("M %04x %04x %04x %04x\n", mbase, cnt0, stat0, dac);
    for (int k = 0; k < nr; k++) {
        if (tag[k] == 'F') printf("F %d %d %d %d %d\n", (int)rv[k][0], (int)rv[k][1], (int)rv[k][2], (int)rv[k][3], (int)rv[k][4]);
        else if (tag[k] == 'G') printf("G %d %d %d %d\n", (int)rv[k][0], (int)rv[k][1], (int)rv[k][2], (int)rv[k][3]);
        else printf("Z %d %d\n", (int)rv[k][0], (int)rv[k][1]);
    }
    printf("\nspu/spu-capfrac: Done.\n");
    for (;;) VSync(0);
}
