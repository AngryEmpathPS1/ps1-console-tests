/*
 * cdrom/cd-scex-locp - cd-scex-locd's D0 and D3 rows at park 00:02:60, watched from INSIDE the drive: its own RAM, read by the
 * 19h,60h peek (hwtests/cd-hc05-tick) in a tight loop for 90 ms after Test04 (the Fable CD session, 2026-11-23).
 * WHY: cd-scex-locd on the SCPH-5000 (its own program SC430916; FF7 (Japan) Discs 1 and 3) and the SCPH-1001 (The X-Files) - a
 * ReadN sent 0-20 ms after Test04 makes a second one-track jump back (9/9/9 of 9 on the 5000), one at 30-40 ms none; the model
 * (SC430916 on our drive body) takes none at any delay (hwtests/cd-scex-locd/PREDICTED-5000.txt; experiments/scexfw/README.txt,
 * "THE 5000 ANSWERS" and after). Read on the drive program (experiments/scex/stale2.js, scratch/scex/sc430916.lst): Test04 resumes
 * reading with $49.0 set (seek by Q); the ReadN clears $49.0, so headers count from then on, and a header past the target in
 * state $6B = 0Dh is a one-track jump back (S_3CA6). But a header counts only if it is the one after the last header accepted
 * ($01DD-$01DF, S_2C88), and in the model the sector in flight at the hold's kick (61) completes in the 2.9 ms after the jump
 * when the DECINT handler is shut ($41.2), so 62 never follows a 61 and no header past the target is ever taken. Which headers
 * the console's program takes after the kick, and when, is what this reads.
 * WHAT IT DOES: cd-scex's preamble (its B row, Init, Demute, Setmode 20h), then six rows at park 00:02:60, the ReadN 0 / 30 ms after
 * Test04, alternately: cd-scex's park (Setloc, ReadN, two sectors, Pause), Test04, then for 90 ms peek $6B (the seek's state),
 * $01DF (the frame of the last header accepted), $49 (bit 0: seek by Q, not by header) and $55 (the frame of the last Q accepted)
 * in turn, the ReadN sent at its moment; every value kept with the console's clock, in RAM. Then Pause, and the row printed.
 * READ IT LIKE THIS: per row, each address's value each time it CHANGES, "t:aa=vv", t in tenths of a ms from Test04's INT3; "|"
 * is the ReadN; "#ff" a sector taken (its header frame). $55 / $01DF are BCD frames of 00:02. SAFE FOR THE DRIVE: reads end
 * before 00:05:00; no unlock (no 50h-56h earlier in the same power-on). About 15 s.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

#ifndef LOCP_MODE              /* Setmode: 20h whole sectors at 1x; hwtests/cd-scex-locp2x sets A0h (2x) */
#define LOCP_MODE 0x20
#define LOCP_TITLE "\ncdrom/cd-scex-locp\n"
#endif
static const uint16_t ADDR[4] = { 0x6B, 0x1DF, 0x49, 0x55 };
static uint32_t evT[600]; static uint8_t evA[600], evV[600]; static int ne;   /* evA: 0-3 an address, 8 the ReadN, 9 a sector */
static void ev(uint32_t t, uint8_t a, uint8_t v) { if (ne < 600) { evT[ne] = t; evA[ne] = a; evV[ne] = v; ne++; } }

/* 19h,60h,lo,hi: a byte of the drive's RAM; sectors that arrive meanwhile are taken; -1 when no answer in 50 ms. */
static int peek(uint16_t addr, uint32_t t0) {
    param(0x60); param(addr & 0xFF); param(addr >> 8);
    command(0x19);
    const uint32_t ts = now();
    for (;;) {
        const uint8_t f = irqFlag();
        if (f == 1) { takeSector(); ev(now() - t0, 9, ff1); continue; }
        if (f) {
            const uint8_t b0 = hasResp() ? resp() : 0xFF;
            drain(); ackAll();
            return f == 3 ? b0 : -1;
        }
        if (now() - ts > 33868800 / 20) return -1;
    }
}

static void readN(uint32_t t0) {
    ackAll(); command(0x06); ev(now() - t0, 8, 0);
    for (int n = 0; n < 8; n++) { const uint8_t f = waitAny(); if (f == 1) { takeSector(); ev(now() - t0, 9, ff1); continue; } drain(); ackAll(); return; }
}

static void row(int k) {
    const uint32_t MS = 33869;
    park(0x00, 0x02, 0x60);
    ne = 0;
    out("D"); line[lp++] = '0' + k; out(" 00:02:60");
    param(0x04); cmd("Test04", 0x19, nullptr);
    const uint32_t t0 = now();
    bool sent = false; int i = 0;
    while (now() - t0 < 90 * MS) {
        if (!sent && now() - t0 >= (uint32_t)k * 10 * MS) { readN(t0); sent = true; continue; }
        const int v = peek(ADDR[i], t0);
        if (v >= 0) ev(now() - t0, (uint8_t)i, (uint8_t)v);
        i = (i + 1) & 3;
    }
    pause(); flush();
    int last[4] = { -1, -1, -1, -1 };
    for (int j = 0; j < ne; j++) {
        if (evA[j] < 4 && last[evA[j]] == evV[j]) continue;
        if (lp > 170) flush();
        if (lp == 0) out("  ");
        out(" "); outDec((int)(evT[j] / 3387)); out(":");
        if (evA[j] == 8) { out("|"); continue; }
        if (evA[j] == 9) { out("#"); outHex(evV[j]); continue; }
        last[evA[j]] = evV[j];
        static const char *NAME[4] = { "6B", "1DF", "49", "55" };
        out(NAME[evA[j]]); out("="); outHex(evV[j]);
    }
    if (lp) flush();
    out("  samples "); outDec(ne); flush();
}

int main() {
    printf(LOCP_TITLE);
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(LOCP_MODE); command(0x0E); waitAny(); drain(); ackAll();               /* Setmode: whole sectors, 1x (or 2x) */

    for (int r = 0; r < 3; r++) { row(0); row(3); }

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
