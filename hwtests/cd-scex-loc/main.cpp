/*
 * cdrom/cd-scex-loc - where the head is during cd-scex's park and Test04 hold, by GetlocP (2026-11-19).
 * WHY: issue AngryEmpathPS1/ps1-console-tests#3. cd-scex-mode on the SCPH-1001 (console-logs/2026-11-19-scexmode-README.txt):
 * held after Test04 + Pause, it begins SCEx strings at parks 51-56 and none from 57, where the drive's own program
 * (SC430916) on our drive model begins them at parks up to 52 - the same window 4 sectors later on the console. The CD
 * session's hypothesis: the Pause aims at the Setloc target or at the last subcode + 1, about 4 sectors apart, and the console
 * takes the other aim. GetlocP shows where the head actually circles.
 * WHAT IT DOES: cd-scex itself (its helpers, its B row, Init, Demute, Setmode 20h), then for each park position 00:02:44,
 * 48, 51, 52, 53, 56, 57, 60:
 *   S row: park (Setloc, ReadN, two sectors, Pause - cd-scex's park), then GetlocP every 40 ms for 1 s.
 *   H row: park, Test04, Pause, then GetlocP every 40 ms for 2 s, then Test05 (begun matched) and Pause.
 * Each GetlocP prints the absolute frame (ff, or s:ff when the second is not 02); "." for any other answer.
 * READ IT LIKE THIS: a hold that circles one track shows about nine climbing frames and a jump back; where the climb runs
 * from and to, against the park, is the answer. No unlock, so run it with no earlier test in the same power-on having sent
 * 50h-56h. SAFE FOR THE DRIVE: nothing past 00:03:00, about 35 s.
 */
#define main scex_main_unused
#include "../cd-scex/main.cpp"
#undef main

/* GetlocP's absolute position, appended as "ff" or "s:ff"; "." for anything else. */
static void locP() {
    ackAll(); command(0x11);
    for (int n = 0; n < 8; n++) {
        const uint8_t f = waitAny();
        if (f == 1) { takeSector(); continue; }
        if (f != 3) { drain(); ackAll(); out(" ."); return; }
        uint8_t b[8]; int k = 0;
        while (k < 8 && hasResp()) b[k++] = resp();
        drain(); ackAll();
        if (k < 8) { out(" ."); return; }
        out(" ");
        if (b[6] != 0x02) { line[lp++] = '0' + (b[6] & 15); out(":"); }
        outHex(b[7]);
        return;
    }
    out(" .");
}

static void samples(int count, uint32_t every) {
    for (int i = 0; i < count; i++) { const uint32_t t0 = now(); locP(); while (now() - t0 < every) {} }
}

int main() {
    printf("\ncdrom/cd-scex-loc\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    const uint32_t MS40 = 33868800 / 25;
    static const uint8_t parks[] = { 0x44, 0x48, 0x51, 0x52, 0x53, 0x56, 0x57, 0x60 };   /* BCD frames of 00:02:ff */
    for (int i = 0; i < 8; i++) {
        park(0x00, 0x02, parks[i]);
        out("S 00:02:"); outHex(parks[i]); samples(25, MS40); flush();

        park(0x00, 0x02, parks[i]);
        out("H 00:02:"); outHex(parks[i]);
        param(0x04); cmd("Test04", 0x19, nullptr); pause(); flush();
        out("  loc"); samples(25, MS40); flush();   /* two lines of 25: the line holds 190 characters */
        out("  loc"); samples(25, MS40); flush();
        int n = 0;
        out("  "); param(0x05); cmd("Test05", 0x19, &n); pause(); flush();
    }

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
