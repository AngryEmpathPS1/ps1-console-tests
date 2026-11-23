/*
 * cdrom/cd-scex-locd - cd-scex-locr's D row with the ReadN at 0, 10, 20, 30 and 40 ms after Test04 (2026-11-19).
 * WHY (this test): cd-scex-locr on the SCPH-1001 (console-logs/2026-11-19-scexlocr-README.txt): a ReadN sent at once after
 * Test04 makes a second one-track jump back before reading (28 of 30 rows) and matches to 00:02:64; one sent 40 ms later
 * takes none (15 of 15) and stops at 56. The resume's own jump lands at ~30 ms. Which delay still gets the second jump
 * brackets the switch: before the resume's jump starts, before it lands, or before the first fresh Q after it (the CD
 * session's question). Rows "D<k>": Test04, GetlocP every 10 ms k times, ReadN ("|"), GetlocP to 0.8 s, Test05 at 2 s,
 * at parks 00:02:44, 60 and 64 (60 and 64 tell 56's edge from 64's). Everything below is cd-scex-locr's own text.
 *
 * cdrom/cd-scex-locr - where the head goes when cd-scex-mode's R and F rows start reading, by GetlocP (2026-11-19).
 * WHY: issue AngryEmpathPS1/ps1-console-tests#3. On the SCPH-1001 cd-scex-mode's R and F sweeps (an explicit ReadN beside
 * Test04) match up to 00:02:64, where Test04 alone (cd-scex-edge1w) stops at 56 and the drive's own program (SC430916, the
 * 5000's, standing in for the 1001's) on our drive model at 56 too (CD_SCEX_LINE, experiments/land_scexline.js). In those
 * rows the console's read ENDS 6-8 sectors before the model's in the same 2 s (34 of 36 rows), so its read starts about a
 * track further back. On SC430916 Test04's resume makes one track jump back (66 -> 57 at ~13 ms at park 64) and the ReadN
 * finds the head before its target and reads on, no second seek. This shows the console's head path through that start.
 * WHAT IT DOES: cd-scex itself (its helpers, its B row, Init, Demute, Setmode 20h), then for each park 00:02:44, 52, 56,
 * 60, 64, three rows, each after cd-scex's park (Setloc, ReadN, two sectors, Pause):
 *   R: Test04, ReadN at once (cd-scex-mode's R), then GetlocP every 10 ms for 0.8 s.
 *   D: Test04, GetlocP every 10 ms for 40 ms, ReadN, then GetlocP every 10 ms for 0.76 s (the ReadN 40 ms late).
 *   F: ReadN, Test04 (cd-scex-mode's F), then GetlocP every 10 ms for 0.8 s.
 * then the rest of 2 s, Test05 (begun matched, as cd-scex-mode prints it) and Pause. Sectors that arrive meanwhile are
 * taken (INT1 acknowledged, header read). Everything is recorded in RAM and printed after the row's Pause, so the serial
 * line costs no time inside the row.
 * READ IT LIKE THIS: after the row's head line come the events in order: "ff" a GetlocP's absolute frame (s:ff when the
 * second is not 02), "#ff" a sector's INT1 and its header frame (#s:ff likewise), "|" where the ReadN went (D rows), "."
 * a GetlocP answered otherwise. One sample per 10 ms, so a jump back is a drop in the ff run. SC430916's prediction at
 * park 64 (R): 66, then 57 at ~16 ms, then 58, 59 ... one a frame, INT1s from 65. A second drop (to ~47-48) after the
 * ReadN would be the console's extra track.
 * No unlock, so run it with no earlier test in the same power-on having sent 50h-56h. SAFE FOR THE DRIVE: reads end
 * before 00:05:00, about 50 s.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

/* Events: kind (0 loc, 1 sector, 2 other answer, 3 ReadN sent), second, frame. */
static uint8_t evK[400], evS[400], evF[400]; static int ne;
static void ev(uint8_t k, uint8_t s, uint8_t f) { if (ne < 400) { evK[ne] = k; evS[ne] = s; evF[ne] = f; ne++; } }
static void sector() { takeSector(); ev(1, ss1, ff1); }

static void locP() {
    ackAll(); command(0x11);
    for (int n = 0; n < 8; n++) {
        const uint8_t f = waitAny();
        if (f == 1) { sector(); continue; }
        if (f != 3) { drain(); ackAll(); ev(2, 0, 0); return; }
        uint8_t b[8]; int k = 0;
        while (k < 8 && hasResp()) b[k++] = resp();
        drain(); ackAll();
        if (k < 8) { ev(2, 0, 0); return; }
        ev(0, b[6], b[7]);
        return;
    }
    ev(2, 0, 0);
}

/* count GetlocPs, one per `every` cycles; sectors taken between them. */
static void samples(int count, uint32_t every) {
    for (int i = 0; i < count; i++) {
        const uint32_t t0 = now(); locP();
        while (now() - t0 < every) { if (irqFlag() == 1) sector(); }
    }
}

static void readN() { ackAll(); command(0x06); for (int n = 0; n < 8; n++) { const uint8_t f = waitAny(); if (f == 1) { sector(); continue; } drain(); ackAll(); return; } }

static void printEvents() {
    for (int i = 0; i < ne; i++) {
        if (lp > 180) flush();
        if (lp == 0) out("  ");
        out(" ");
        if (evK[i] == 2) { out("."); continue; }
        if (evK[i] == 3) { out("|"); continue; }
        if (evK[i] == 1) out("#");
        if (evS[i] != 0x02) { outHex(evS[i]); out(":"); }
        outHex(evF[i]);
    }
    if (lp) flush();
}

static void row(int k, uint8_t f) {
    const uint32_t S = 33868800, MS10 = S / 100;
    park(0x00, 0x02, f);
    ne = 0;
    out("D"); line[lp++] = '0' + k; out(" 00:02:"); outHex(f);
    const uint32_t t0 = now();
    param(0x04); cmd("Test04", 0x19, nullptr);
    samples(k, MS10); ev(3, 0, 0); readN(); samples(80 - k, MS10);
    while (now() - t0 < 2 * S) { if (irqFlag() == 1) sector(); }
    int n = 0;
    param(0x05); cmd("Test05", 0x19, &n);
    if (n) { out(" +"); outDec(n); out("x<1>"); outHex(st1); out(" to "); outHex(ss1); out(":"); outHex(ff1); }
    pause(); flush();
    printEvents();
}

#ifndef LOCD_NO_MAIN   /* cd-scex-locd2 includes this file for its row helpers */
int main() {
    printf("\ncdrom/cd-scex-locd\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    static const uint8_t parks[] = { 0x44, 0x60, 0x64 };   /* BCD frames of 00:02:ff */
    for (int i = 0; i < 3; i++)
        for (int k = 0; k <= 4; k++) row(k, parks[i]);

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
#endif
