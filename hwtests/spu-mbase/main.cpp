/*
 * spu/spu-mbase - WHERE IS THE REVERB'S WORK AREA AS A TEST FINDS IT, AND DOES ITS WALK MEET AN IRQ ADDRESS? (2026-11-25, the SPU
 * session; SPU_XFER_SEEN's KNOWN-OPEN "pitch 0001h's 1008h row")
 *
 * WHY. With a voice at pitch 0001h an IRQ armed at 1008h fires 3,167 samples in on this emulator and not in 16,000 on the consoles
 * (hwtests/spu-irqarm2's row). Here it is the reverb's own accesses: the work area as the boot left it (mBASE) starts below 1008h
 * and its moving address walks over it. The consoles' mBASE was never printed, nor whether their walk meets an armed address at all.
 *
 * WHAT IT DOES. Interrupts off, no voice keyed (all keyed off first), nothing of the reverb's settings written in stage A.
 *   M mbase spucnt spustat irqaddr xfer     the registers as found (1F801DA2h, 1DAAh, 1DAEh, 1DA4h, 1DACh)
 *   V r0 .. r31                             the 32 reverb registers as found (1F801DC0h-1DFEh)
 *   A addr t1 t2                            stage A, as found: the IRQ armed at addr (SPUCNT bit 6), SPUSTAT bit 6 watched - t1 the
 *                                           first time it shows and t2 the next after an acknowledge, in samples from the test's
 *                                           start (-1: not in 13 s, a whole walk of the 512 KB at one halfword a sample)
 *   B addr t1 t2                            stage B: mBASE written FE00h (7F000h: a 4 KB area) first - 1008h must stay silent
 *   C addr t1 t2                            stage C: mBASE written 0200h (1000h) - 1008h must be met, and again a walk later
 * t2 - t1 is the walk's length: (80000h - mBASE) / 2 samples if the area is walked a halfword a sample.
 * mBASE is put back as found at the end (a write restarts the walk at it). Every wait is bounded. No assertions. Up to ~2 minutes.
 * THE RULE: run here first: `node hwtests/run.js spu-mbase --ntsc --frames=9000 --quiet=7000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }   /* units of 8 cycles */
#define SAMPLES(u) ((u) / 96u)
#define LIMIT (13u * 44100u * 96u)                                 /* 13 s */

static uint16_t cnt;
static int32_t res[16][3]; static char tag[16]; static int nres = 0;
static int32_t waitIrq() {
    const uint32_t t0 = now();
    while (now() - t0 < LIMIT) { if (read16(SPU(0x1AE)) & 0x0040) return (int32_t)SAMPLES(now()); }
    return -1;
}
static void ack() { write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); for (int i = 0; i < 200; i++) now(); write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040)); }
static void row(char stage, uint32_t addr) {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); for (int i = 0; i < 200; i++) now();
    write16(SPU(0x1A4), (uint16_t)(addr / 8));
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const int32_t t1 = waitIrq();
    int32_t t2 = -1;
    if (t1 >= 0) { ack(); const uint32_t t0 = now(); while (now() - t0 < 96u * 1024u) {} ack(); t2 = waitIrq(); }   /* past the address first: every tap of a step (they sit up to a few hundred samples apart) */
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    tag[nres] = stage; res[nres][0] = (int32_t)addr; res[nres][1] = t1; res[nres][2] = t2; nres++;
}

int main() {
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200); lastT = read16(T2_COUNT); clk = 0;   /* the system clock / 8 */
    const uint16_t mbase = read16(SPU(0x1A2)), cnt0 = read16(SPU(0x1AA)), stat0 = read16(SPU(0x1AE)), irqa = read16(SPU(0x1A4)), dac = read16(SPU(0x1AC));
    static uint16_t rv[32];
    for (int i = 0; i < 32; i++) rv[i] = read16(SPU(0x1C0 + 2 * (uint32_t)i));
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);       /* every voice keyed off */
    cnt = (uint16_t)(cnt0 & ~0x0040);
    row('A', 0x01008); row('A', 0x40008); row('A', 0x7F008);
    write16(SPU(0x1A2), 0xFE00);
    row('B', 0x01008); row('B', 0x7F008);
    write16(SPU(0x1A2), 0x0200);
    row('C', 0x01008);
    /* D, E: the reverb's registers set to a preset whose taps sit farther apart than a 4 KB area is long (the values the SCPH-9001
     * had as found on 2026-11-25, when its walk met 1008h from an area at 7F000h), mBASE FE00h: where do the taps land? D with the
     * reverb's master enable (SPUCNT bit 7) clear, E with it set. */
    static const uint16_t preset[32] = { 0x033d, 0x0231, 0x7e00, 0x5000, 0xb400, 0xb000, 0x4c00, 0xb000, 0x6000, 0x5400, 0x1ed6, 0x1a31, 0x1d14, 0x183b, 0x1bc2, 0x16b2,
                                         0x1a32, 0x15ef, 0x15ee, 0x1055, 0x1334, 0x0f2d, 0x11f6, 0x0c5d, 0x1056, 0x0ae1, 0x0ae0, 0x07a2, 0x0464, 0x0232, 0x8000, 0x8000 };
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), preset[i]);
    cnt = (uint16_t)(cnt & ~0x0080);
    write16(SPU(0x1AA), cnt); write16(SPU(0x1A2), 0xFE00);
    row('D', 0x01008); row('D', 0x00408); row('D', 0x7F008);
    cnt = (uint16_t)(cnt | 0x0080);
    write16(SPU(0x1AA), cnt); write16(SPU(0x1A2), 0xFE00);
    row('E', 0x01008); row('E', 0x00408); row('E', 0x7F008);
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), rv[i]);
    write16(SPU(0x1A2), mbase);
    write16(SPU(0x1A4), irqa);
    write16(SPU(0x1AA), cnt0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-mbase\nThe reverb's work area as found, and whether its walk meets an armed IRQ address: M / V registers, A B C addr t1 t2 (samples). No assertions.\n\n");
    printf("M %04x %04x %04x %04x %04x\n", mbase, cnt0, stat0, irqa, dac);
    printf("V");
    for (int i = 0; i < 32; i++) printf(" %04x", rv[i]);
    printf("\n");
    for (int k = 0; k < nres; k++) printf("%c %05x %d %d\n", tag[k], (unsigned)res[k][0], (int)res[k][1], (int)res[k][2]);
    printf("\nspu/spu-mbase: Done.\n");
    for (;;) VSync(0);
}
