/*
 * cdrom/cd-scex-edge - cd-scex's P row in 10-frame steps from 00:02:00 to 00:03:00 (2026-11-17). Positions are MSF
 * (75 frames a second): 00:02:00 is the data track's first sector and 00:03:00 is 75 sectors on, so this is the first second of data.
 * WHY: issue AngryEmpathPS1/ps1-console-tests#3. With The X-Files (USA) D1, cd-scex matched SCEx strings at 00:02:00 and
 * at no row from 00:03:00 on, on the SCPH-1001, 7001 and 9001 alike, while the counts at 00:02:00 differed by drive
 * (04 04, 04 04, 03 02). Rix (rixnobis): if the pressing sets how far the wobble carrier runs into the data track, the
 * last matching position is the same on every drive, and the laser and head only change how many match before it.
 * This finds that position on each drive. (Rix proposed 10-second steps, reading the MSF as minutes:seconds.)
 * WHAT IT DOES: cd-scex itself (its helpers, its B row, Init, Demute, Setmode 20h), then P rows of 2 s each at 00:00:10
 * (so the head comes to 00:02:00 from where cd-scex brought it), 00:02:00, 00:02:10 ... 00:02:70, 00:03:00. No unlock, so
 * run it with no earlier test in the same power-on having sent 50h-56h (cd-scex-short's rule).
 * SAFE FOR THE DRIVE: nothing past 00:03:00, about 35 s.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

int main() {
    printf("\ncdrom/cd-scex-edge\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    const uint32_t S = 33868800;
    row("P", 0x00, 0x00, 0x10, 2 * S, "2s");
    static const uint8_t sec[] = { 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03 };   /* BCD */
    static const uint8_t frm[] = { 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x00 };
    for (int i = 0; i < 9; i++) row("P", 0x00, sec[i], frm[i], 2 * S, "2s");

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
