/*
 * cdrom/cd-scex-edge1 - cd-scex-edge in 1-frame steps from 00:02:40 to 00:02:55 (2026-11-18). Positions are MSF, 75
 * frames a second: one step is one sector.
 * WHY: issue AngryEmpathPS1/ps1-console-tests#3. cd-scex-edge on The X-Files (USA) D1, six runs each: the last SCEx match
 * sat at 00:02:50 on the SCPH-7001 and 1001 and at 00:02:40 on the 9001, in every run (console-logs/2026-11-17-scexedge-
 * README.txt). Rix (rixnobis) asked whether the two drives stop on the same sector or 10 sectors apart; this answers it.
 * WHAT IT DOES: cd-scex itself (its helpers, its B row, Init, Demute, Setmode 20h), then P rows of 2 s each at 00:00:10
 * and 00:02:00 (cd-scex-edge's approach, so the head comes in the same way), then 00:02:40, 00:02:41 ... 00:02:55. No
 * unlock, so run it with no earlier test in the same power-on having sent 50h-56h (cd-scex-short's rule).
 * SAFE FOR THE DRIVE: nothing past 00:02:55, about 55 s.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

int main() {
#if EDGE1_WIDE
    printf("\ncdrom/cd-scex-edge1w\n");
#else
    printf("\ncdrom/cd-scex-edge1\n");
#endif
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    const uint32_t S = 33868800;
    row("P", 0x00, 0x00, 0x10, 2 * S, "2s");
    row("P", 0x00, 0x02, 0x00, 2 * S, "2s");
#ifndef EDGE1_LAST
#define EDGE1_LAST 55   /* cd-scex-edge1w: 69 */
#endif
    for (int f = 40; f <= EDGE1_LAST; f++) row("P", 0x00, 0x02, (uint8_t)(((f / 10) << 4) | (f % 10)), 2 * S, "2s");   /* BCD */

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
