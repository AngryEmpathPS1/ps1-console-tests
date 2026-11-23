/*
 * cdrom/cd-hc05-clock - the CD drive's own CPU clock (the HC05's) against the console's, to a few ppm (the Fable CD session, 2026-11-23).
 * WHY: AngryEmpath (through main, 2026-11-23): "i want console perfect" - on the PU-7 / PU-8 boards (SCPH-1000, early SCPH-1001) the
 * drive's HC05 runs from its own 4.00 MHz crystal, not from the system clock, and the emulator starts it at a fixed ratio, phase 0,
 * no drift (CDROMLLE.powerOn; the MDEC session, 56711df2a). hwtests/cd-hc05-tick counts the program's own tick (RAM 60h), but that
 * counter loses ticks while the program is busy (0.7% apart run to run on one console, less again while reading), so it is no clock.
 * WHAT IT DOES: Init (the drive idle afterwards), then for three segments of ~3.3 s each, the 19h,60h peek of HC05 port 18h - TCNTH,
 * Timer 1's free-running counter's high byte (psx-spx "CDROM Internal HC05 On-Chip I/O Ports", Timer 1; it counts the HC05's E clock
 * / 4: ~500 kHz from a 4.00 MHz crystal, 529.2 kHz if the HC05 runs at 33.8688 MHz / 8) - in a tight loop, each sample's console time
 * (Timer 2 at sysclk / 8, extended), the counter unwrapped (a step of 256 counts every ~0.5 ms, a peek every ~1.5 ms, a wrap every
 * ~131 ms). Everything kept in RAM, printed after.
 * READ IT LIKE THIS: per segment "S<n>" then lines of "t:c" - t the console's clock in cycles (33,868,800 a second) from the segment's
 * start, c the unwrapped TCNTH count (each count 256 timer counts) - every 16th sample. The host fits c against t (the rate, ppm from
 * 500 kHz) per segment; segments apart show drift within a power-on, runs apart the spread. No disc needed. About 12 s.
 * The emulator does not model Timer 1 (its ports read back what was written): there every sample is the same count.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

static uint32_t sT[2300]; static uint32_t sC[2300]; static int ns;

static int peek(uint16_t addr) {
    param(0x60); param(addr & 0xFF); param(addr >> 8);
    command(0x19);
    const uint32_t ts = now();
    for (;;) {
        const uint8_t f = irqFlag();
        if (f) { const uint8_t b0 = hasResp() ? resp() : 0xFF; drain(); ackAll(); return f == 3 ? b0 : -1; }
        if (now() - ts > 33868800 / 20) return -1;
    }
}

static void segment(int k) {
    ns = 0;
    const uint32_t t0 = now();
    int last = -1; uint32_t c = 0;
    while (now() - t0 < 112000000 && ns < 2300) {         /* ~3.3 s */
        const int v = peek(0x0018);
        const uint32_t t = now() - t0;
        if (v < 0) continue;
        if (last >= 0) c += (uint32_t)((v - last) & 0xFF);
        last = v;
        sT[ns] = t; sC[ns] = c; ns++;
    }
    out("S"); outDec(k); out(" samples "); outDec(ns); flush();
    for (int i = 0; i < ns; i += 16) {
        if (lp > 170) flush();
        out(" "); outDec((int)sT[i]); out(":"); outDec((int)sC[i]);
    }
    if (lp) flush();
    out("  last "); outDec((int)sT[ns - 1]); out(":"); outDec((int)sC[ns - 1]); flush();
}

int main() {
    printf("\ncdrom/cd-hc05-clock\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    waitCycles(33868800);                                                         /* 1 s settle */
    for (int k = 1; k <= 3; k++) segment(k);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
