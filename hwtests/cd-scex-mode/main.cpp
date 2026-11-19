/*
 * cdrom/cd-scex-mode - cd-scex-edge1w with the drive's waiting mode swapped (2026-11-19). Positions are MSF, 75 frames a
 * second: one step is one sector.
 * WHY: issue AngryEmpathPS1/ps1-console-tests#3. cd-scex-edge1w on The X-Files (USA) D1, six runs each: the last SCEx match
 * is at 00:02:45 on the SCPH-9001 and 00:02:56 on the 1001, every run (console-logs/2026-11-18-scexedge1-README.txt). During
 * the 2 s after Test04 the 1001 reads forward at 1x (stat 22) and the 9001 stays paused (stat 02). Rix (rixnobis): swap the
 * mode on each drive - if an edge moves toward the other's, the 11 sectors come from the mode; if neither moves, from the drives.
 * WHAT IT DOES: cd-scex itself (its helpers, its B row, Init, Demute, Setmode 20h), then three sweeps. H rows: park, Test04,
 * Pause (the drive holds, as the 9001 does), wait 2 s, Test05, Pause. R rows: park, Test04, ReadN (the drive reads forward, as
 * the 1001 does), wait 2 s, Test05 (its INT1s counted), Pause. F rows: park, ReadN, then Test04 with the drive already
 * reading, wait 2 s, Test05, Pause - added after r1 on the SCPH-9001 (console-logs/2026-11-19-SCPH-9001-scexmode-r1, the
 * two-sweep exe of fc2336d7e): there ReadN after Test04 answered INT3 02 and no sector came, so R did not make it read.
 * Each sweep: 00:00:10 and 00:02:00 (the head comes in the way
 * cd-scex-edge brings it), then every frame from 00:02:36 to 00:02:66. Each drive's own mode is its control: the 9001's H rows
 * and the 1001's R rows should repeat edge1w's edges. The Pause or ReadN reply comes before Test05, so read the
 * "begun matched" after the word Test05, not by field number. No unlock, so run it with no earlier test in the same power-on having sent 50h-56h.
 * SAFE FOR THE DRIVE: nothing read past about 00:04:70, about 5 minutes.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

/* One row with the waiting mode set: 'H' Pause after Test04, 'R' ReadN after Test04, 'F' ReadN before Test04. */
static void rowMode(char mode, uint8_t m, uint8_t s, uint8_t f, uint32_t waitC) {
    park(m, s, f);
    line[lp++] = mode; out(" "); outHex(m); out(":"); outHex(s); out(":"); outHex(f); out(" 2s");
    if (mode == 'F') cmd("ReadN", 0x06, nullptr);
    param(0x04); cmd("Test04", 0x19, nullptr);
    if (mode == 'H') pause(); else if (mode == 'R') cmd("ReadN", 0x06, nullptr);
    waitCycles(waitC);
    int n = 0;
    param(0x05); cmd("Test05", 0x19, &n);
    if (n) { out(" +"); outDec(n); out("x<1>"); outHex(st1); out(" to "); outHex(ss1); out(":"); outHex(ff1); }
    pause();
    flush();
}

int main() {
    printf("\ncdrom/cd-scex-mode\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    const uint32_t S = 33868800;
    static const char modes[] = { 'H', 'R', 'F' };
    for (int k = 0; k < 3; k++) {
        rowMode(modes[k], 0x00, 0x00, 0x10, 2 * S);
        rowMode(modes[k], 0x00, 0x02, 0x00, 2 * S);
        for (int f = 36; f <= 66; f++) rowMode(modes[k], 0x00, 0x02, (uint8_t)(((f / 10) << 4) | (f % 10)), 2 * S);   /* BCD */
    }

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
