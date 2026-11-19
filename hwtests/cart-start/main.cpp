/*
 * hwtests/cart-start - CART START: the runner's own locked start ('S' / 'U'), its arguments from the cart and the test from the cart's
 * flash, no PC in it (cart ROM v4; owner AngryEmpath, 2026-11-17; the requirements are discriminate-v2's, side-session-notes
 * cart-start-requirements.md). Not a test: cartrom/rom.c brk_main copies it to RAM where the runner goes (80180000h) at the shell break
 * and runs it, and the runner is copied over it and started after, as ever.
 *
 * WHY THE RUNNER'S CODE, LINE FOR LINE. The emulator does not copy a list of registers: it runs this cart on the emulated BIOS. For
 * that to reproduce a console's start, every input of the start has to be written, LOCKED (waited for) or MEASURED and printed - never
 * "so many cycles after the break" (the drive moves the break, and the emulator reaches Pre-Boot 3,962 counts sooner than both consoles).
 * run() is that start already: it restores the kernel and the GPU / SPU / timers / memory system as the runner found them, waits for a
 * VBlank, locks Timer 1 to V = 24,000 cycles after it and the refresh (or the SPU's sample clock) to a phase, and starts the test with
 * the same instructions whatever the lock found. So this includes the runner's source whole (tools/cartstart_gen.js: only its get() and
 * put() are changed, so no cable is read or waited for), and calls run(true, spu) with the arguments an 'S' / 'U' would have carried:
 *   delay (video) 24,000, target phase 0, tolerance 0, phi any, no line change, no GP1(06h), the end the test's own "Done.", no stop.
 *
 * THE START, as the requirements list it: the test's header and text copied from the cart before the lock (flash waits and the copy's
 * length then do not matter), putchar / puts hooked to the runner's (branchless, no SIO1 look: abortOk 0), T2 restarted, FlushCache,
 * interrupts off from there on, the refresh (probeRefresh) and the SPU (probeSpu) found, then run()'s lock. I_STAT is acknowledged
 * (a CD bit may be pending at a time the drive chose); SR, I_MASK, I_STAT, k1, the boot count and WARM are printed.
 *
 * WHAT COMES BACK: the test's printout stays where the runner keeps it, 80140000h (its length in CSR word 1), and ONE LINE IS ITS FIRST:
 *   CARTSTART v1 exe=<sha1 8> delay=24000 target=0 tol=0 spu=<0/1> frames=<waited> phase=<refresh> spuphase=<x> spuS0=<x> refreshR0=<r>
 *   phi=<lo..hi or -> sr=<x> imask=<x> istat=<x> k0=<x> boot=<n> warm=<w> line=<n> lineused=<n> lflags=<x> gpustat=<x> k1=<x> epc=<x> cause=<x> dcic=<x> cdstat=<x>
 * (line = the line the board measured over eight frames, lineused = the one the vernier was scheduled and decoded on, both in 1250ths of a cycle;
 * lflags 1 two crystals, 2 one oscillator, 4 matches none, 8 not measured, 10h SCPH-7502 NTSC, 20h PAL; gpustat = GPUSTAT as the start began)
 * (the fields in discriminate-v2's order, then ours: k0 is the cart's break stub's, BF000000h; EPC the shell's entry; DCIC 0; cdstat the CD's
 * status byte read passively, the lid and disc state). It is written after the test and turned to the front, since the frames waited and the
 * phases are known only after the lock.  * With it discriminate --start=cart and consolegrade set the reference run's refresh and SPU phase and phi to the console's. The words,
 * at 80190F00h (params, the cart's) and 80190F80h (results, ours): see rom.c start_run and the code below. The runner's DONE answer
 * also goes out SIO1 if a bridge is there (put() is bounded), in the 'S' format, so a capture holds it.
 */
#define runner_main runner_main_orig
#include "runner_cs.inc"
#undef runner_main
#ifndef CART_LINE_BIAS
#define CART_LINE_BIAS 0
#endif

/* THE BOARD'S LINE, MEASURED (v5; busValue's finding of 2026-11-17 and his asks after the first console frames): the vernier's schedule and its board decode
 * need the machine's line, in 1250ths of a cycle, and a cart start has no host to name it. v4 timed eight frames and SNAPPED the result to the nearest nominal line -
 * but a board's crystals are a few ppm off nominal (2 ppm over a 120-line run is half a cycle): the SCPH-1001's line sat +1.4..+4.2 ppm off the two crystals' and
 * the 5501-u2's -10.7..-8.6, the 5501-u2's measure (13-19 units low) matched the one oscillator's, and its tick ran 3.5 cycles from the schedule - after ~80 lines
 * every read said "not yet" and carried nothing. So now: 128 frames, VBlank edge to VBlank edge (I_STAT bit 0), the cycles by Timer 2's extended count and the LINES
 * between the first and last edge by Timer 1 (hblanks, free running) - counted, not assumed (263 or 314 a frame by GPUSTAT bit 20 is the sanity check: within two
 * lines of N times that it is taken as exact, since an edge a few cycles from a tick reads a line over or under; otherwise - an interlaced mode's 262.5 - the count is
 * used as it is). line = cycles * 1250 / lines, rounded: an edge's few cycles over 33,664 lines is a fraction of a unit. The vernier is scheduled and decoded on THAT
 * line (never a snap), and follows the tick besides (tools/cartstart_gen.js VERN). lflags: the nearest nominal line within 150 units: 1 two crystals, 2 one
 * oscillator, 10h SCPH-7502 NTSC, 20h PAL; 4 none near; 8 no measure (no VBlank in 2,000,000 cycles: the runner's default line); 40h always when it was measured:
 * the line used is the measured one. */
struct LineMeas { uint32_t total, meas, used, flags, frames, lines; };
static LineMeas measureLine() {
    const uint32_t N = 128;
    LineMeas m = { 0, 0, 2690688u, 0, N, 0 };
    const uint32_t gst = GPUSTAT, pal = (gst >> 20) & 1, L = pal ? 314u : 263u;
    uint32_t c0 = 0, t0 = 0; bool ok = true;
    TMODE(1) = 0x0100;                   /* Timer 1 counts hblanks, free running (the write starts it at 0): the lines between the first and last edge are COUNTED */
    I_STAT = ~1u;
    for (uint32_t e = 0; e <= N && ok; e++) {
        const uint32_t w0 = cycles();
        while (!(I_STAT & 1) && cycles() - w0 < 2000000u) {}
        if (!(I_STAT & 1)) { ok = false; break; }
        const uint32_t c = cycles(), t = T1_COUNT;
        if (e == 0) { c0 = c; t0 = t; } else if (e == N) { m.total = c - c0; m.lines = (t - t0) & 0xFFFFu; }
        I_STAT = ~1u;
    }
    if (!ok) { m.flags = 8; return m; }
    uint32_t lines = m.lines;
    const uint32_t want = N * L, dl = lines > want ? lines - want : want - lines;
    if (dl <= 2 || lines < 1000) lines = want;   /* an edge a few cycles from a tick reads one line over or under; a Timer 1 that did not count: the assumption (an interlaced mode's 262.5 is read as it is) */
    m.lines = lines;
    const uint32_t q = m.total / lines, r = m.total % lines;
    m.meas = q * 1250u + (r * 1250u + lines / 2) / lines;
    static const uint32_t CAND[4] = { 2690688u, 2690625u, 2715386u, 2709489u };
    static const uint32_t FLAG[4] = { 1, 2, 0x10, 0x20 };
    uint32_t best = 0, bd = 0xFFFFFFFFu;
    for (int i = 0; i < 4; i++) {
        if ((i == 3) != (pal != 0)) continue;
        const uint32_t d = m.meas > CAND[i] ? m.meas - CAND[i] : CAND[i] - m.meas;
        if (d < bd) { bd = d; best = (uint32_t)i; }
    }
    m.used = m.meas;                     /* the measured line itself - never a snap to a nominal one */
    m.flags = (bd <= 150 ? FLAG[best] : (4u | (pal ? 0x20u : 0u))) | 0x40u;
    return m;
}

#define CSP ((volatile uint32_t *)0x80190F00)   /* params, the cart's: 0 'CSP1', 1 the test's cart address, 2 its size, 3 flags (bit 0: the SPU's lock), 4 SR, 5 I_MASK, 6 I_STAT, 7 k1, 8 boot count, 9 WARM, 10 sha1 head */
#define CSR ((volatile uint32_t *)0x80190F80)   /* results, ours: 0 'CSR1', 1 logLen, 2 lockFrames, 3 lockPhase, 4 lockSpu, 5 spuS0, 6 refreshR0, 7 flags, 8 vernLo, 9 vernHi, 10 done, 11 the address of the vernier reads (120 bytes vernC, 120 int16 vernD) */

static uint8_t csStream[40];
static uint32_t csPos;
uint8_t csParam() { return csStream[csPos < sizeof csStream ? csPos++ : 0]; }
static uint32_t csN;
static void csB(uint8_t b) { csStream[csN++] = b; }
static void csW(uint32_t w) { csB(w); csB(w >> 8); csB(w >> 16); csB(w >> 24); }

static void lp(const char *s) { while (*s) { if (logLen < LOG_SIZE) logBuf[logLen] = (uint8_t)*s; logLen++; s++; } }
static void lh(uint32_t v) { char b[9]; for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[8] = 0; lp(b); }
static void ld(int32_t v) { char b[12]; int n = 0; uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v; if (v < 0) lp("-"); do { b[n++] = '0' + u % 10; u /= 10; } while (u); while (n) { char c[2] = { b[--n], 0 }; lp(c); } }

static void rev(uint8_t *a, uint32_t n) { for (uint32_t i = 0; i < n / 2; i++) { const uint8_t t = a[i]; a[i] = a[n - 1 - i]; a[n - 1 - i] = t; } }

extern "C" void runner_main() {
    /* what the start found, read before anything here changes it: k0 / k1 (a NULL jump lands at k0 + C80h: MLB 99), EPC, CAUSE, DCIC (0: the cart's break handler
     * turned it off first), the CD's status byte (a passive read: the lid and disc state, the drive being the one thing no lock fixes) */
    uint32_t k0, k1, epc, cause, dcic;
    __asm__ volatile("move %0, $26\n move %1, $27\n mfc0 %2, $14\n mfc0 %3, $13\n mfc0 %4, $7\n nop" : "=r"(k0), "=r"(k1), "=r"(epc), "=r"(cause), "=r"(dcic));
    const uint32_t cdstat = *(volatile uint8_t *)0x1F801800;
    const bool spu = CSP[3] & 1;
    entrySr = START_SR;                  /* the Unirom path's SR for the test, as the runner's runner_main (2026-11-18: the cart's break leaves 0400h; the CARTSTART line's sr= is still the one the cart found, CSP[4]); snapshot() takes START_DPCR / START_DICR the same way */
    snapshot();                          /* the kernel's 64 KB and the machine, as the runner takes them at its start */
    setSr(entrySr & ~1u);
    T2_MODE = 0;
    for (int k = 0; k < 4; k++) { const uint16_t t0 = T2_COUNT; while ((uint16_t)(T2_COUNT - t0) < 60000) {} }
    probeRefresh();                      /* the table 'F' sends before any test has run */
    lastT2 = T2_COUNT;
    probeSpu();
    /* the test: header and text from the cart, to the load address (run() reads the header from `header`, as load() leaves it) */
    const volatile uint8_t *x = (const volatile uint8_t *)CSP[1];
    for (int i = 0; i < 2048; i++) header[i] = x[i];
    const uint32_t tAddr = *(uint32_t *)(header + 0x18), tSize = *(uint32_t *)(header + 0x1C);
    volatile uint8_t *dst = (volatile uint8_t *)tAddr;
    for (uint32_t i = 0; i < tSize; i++) dst[i] = x[0x800 + i];
    /* the arguments an 'S' / 'U' carries: video delay, target, tolerance, phi any (7FFFFFFFh) and its tolerance, the line, the GP1(06h) range, the end mode (0: Done., no stop) */
    const LineMeas lm = measureLine();   /* eight frames timed: which line this board has (a one-oscillator console differs from the two crystals by 13 cycles a frame) */
    const uint32_t gst0 = GPUSTAT;
    csN = 0; csPos = 0;
    csW(24000); csW(0); csB(0); csW(0x7FFFFFFFu); csW(0); csW(lm.used + CART_LINE_BIAS); csW(0); csB(0);   /* the line: the board's own, measured above (the runner's default if it could not) */
    I_STAT = 0;                          /* a CD bit pending at a time the drive chose: printed (the cart's), then acknowledged */
    run(true, spu);
    /* back: the end of the test ("Done.") and the runner's DONE answer are out; keep what the log header names */
    const uint32_t mid = (uint32_t)((vernLo >> 1) + (vernHi >> 1));
    /* the line is written after the test's text and then turned to the front of the log (three reversals, in place): discriminate --start=cart reads
     * the FIRST line, and its values (the frames waited, the phases) are not known until the lock has been made */
    const uint32_t n0 = logLen;
    lp("CARTSTART v1 exe="); lh(CSP[10]); lp(" delay=24000 target=0 tol=0 spu="); ld(spu); lp(" frames="); ld((int32_t)lockFrames);
    lp(" phase="); ld((int32_t)lockPhase); lp(" spuphase="); lh(lockSpu); lp(" spuS0="); lh(spuS0); lp(" refreshR0="); ld((int32_t)refreshR0);
    lp(" phi="); if (vernLo < vernHi && vernLo > -0x7FFFFFFF && vernHi < 0x7FFFFFFF) { ld((int32_t)mid); lp("/1250"); } else lp("-");
    lp(" sr="); lh(CSP[4]); lp(" imask="); lh(CSP[5]); lp(" istat="); lh(CSP[6]); lp(" k0="); lh(k0); lp(" boot="); ld((int32_t)CSP[8]); lp(" warm="); ld((int32_t)CSP[9]);
    lp(" line="); ld((int32_t)lm.meas); lp(" lineused="); ld((int32_t)lm.used); lp(" lflags="); lh(lm.flags); lp(" gpustat="); lh(gst0);
    lp(" k1="); lh(k1); lp(" epc="); lh(epc); lp(" cause="); lh(cause); lp(" dcic="); lh(dcic); lp(" cdstat="); lh(cdstat); lp("\n");
    const uint32_t n1 = logLen;
    if (n1 <= LOG_SIZE) { rev(logBuf, n0); rev(logBuf + n0, n1 - n0); rev(logBuf, n1); }
    CSR[0] = 0x31525343u; CSR[1] = logLen; CSR[2] = lockFrames; CSR[3] = lockPhase; CSR[4] = lockSpu; CSR[5] = spuS0; CSR[6] = refreshR0;
    /* the vernier's raw reads for tools/vernphi.js, together: 120 bytes of Timer 1 counts (vernC), then 120 signed halfwords of the offsets they were read at (vernD) -
     * 360 bytes at 80191800h, which CSR word 11 points to (0: this build kept none) */
    { volatile uint8_t *r = (volatile uint8_t *)0x80191800; for (int k = 0; k < VERN_N; k++) r[k] = vernC[k]; for (int k = 0; k < VERN_N; k++) ((volatile int16_t *)(r + VERN_N))[k] = vernD[k]; CSR[11] = 0x80191800u; }
    CSR[12] = lm.meas; CSR[13] = lm.used; CSR[14] = lm.flags; CSR[15] = gst0; CSR[16] = lm.total; CSR[17] = lm.frames; CSR[18] = lm.lines;
    CSR[7] = runnerFlags; CSR[8] = (uint32_t)vernLo; CSR[9] = (uint32_t)vernHi; CSR[10] = 1;
}
