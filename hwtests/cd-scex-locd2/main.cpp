/*
 * cdrom/cd-scex-locd2 - cd-scex-locd's D row with the ReadN at 20, 22, 24, 26, 27, 28, 29 and 30 ms after Test04 (the Fable CD session,
 * 2026-11-23). WHY: Rix on AngryEmpathPS1/ps1-console-tests#3 asked whether land_inflight's INFLIGHT_ANNOUNCE = 1.25 sectors (the
 * sector in flight at the hold's kick announced 1.25 sectors after the landing) was read off cd-scex-locp's header times or picked to
 * put the switch between 20 and 30 ms. It was picked by a sweep (scratch/scex/ksweep.sh: cd-scex-locd's 9/9/9/0/0 and 8/8/8/1/0 at
 * 10 ms steps hold for ~1.2-1.35); locp's header 61 at 34-41 ms agrees with it (the model 36-40 ms) without choosing it. On the
 * drive probe (experiments/scex/stale2.js, MODE=R, whose delay counts from another origin) the switch moves under 1 ms for K
 * 1.2-1.35 on both discs at parks 44, 60, 64 (1.4 moves it ~2 ms later, 1.1 takes none). THE MODEL'S PREDICTION, this test on
 * next + land_inflight: a second jump back with the ReadN at 20-28 ms, split at 29, none at 30, both setups, every park
 * (PREDICTED.txt). So the row brackets the console's switch to a millisecond: it tests where the model puts the switch, and K
 * only beside ~1.1 or ~1.4.
 * WHAT IT DOES: cd-scex-locd's (and through it cd-scex's) setup and row - park, Test04, GetlocP every 10 ms, ReadN, GetlocP every
 * 10 ms to 0.8 s, the rest of 2 s, Test05, Pause - except the ReadN goes at a set time in ms after Test04's answer (a GetlocP is
 * not started within 3 ms of it, so the ReadN is not held behind one). Parks 00:02:44, 60, 64, one row per delay.
 * READ IT LIKE THIS: as cd-scex-locd, but a row is "M<ms> 00:02:ff ..." and the ReadN mark is "|<cycles>": the console clock
 * (33,868,800 a second) from Test04's answer to the ReadN's send, so each row says when its ReadN really went.
 * No unlock; SAFE FOR THE DRIVE: reads end before 00:05:00, about 50 s.
 */
#define LOCD_NO_MAIN
#include "../cd-scex-locd/main.cpp"

static uint32_t readAt;

static void rowMs(int ms, uint8_t f) {
    const uint32_t S = 33868800, MS = S / 1000, MS10 = S / 100;
    park(0x00, 0x02, f);
    ne = 0;
    out("M"); outDec(ms); out(" 00:02:"); outHex(f);
    const uint32_t t0 = now();
    param(0x04); cmd("Test04", 0x19, nullptr);
    const uint32_t t1 = now(), at = (uint32_t)ms * MS;
    for (uint32_t next = 0; next + 3 * MS <= at; next += MS10) {      /* GetlocP every 10 ms up to 3 ms before the ReadN */
        while (now() - t1 < next) { if (irqFlag() == 1) sector(); }
        locP();
    }
    while (now() - t1 < at) { if (irqFlag() == 1) sector(); }
    readAt = now() - t1;
    ev(3, 0, 0); readN();
    samples((800 - ms) / 10, MS10);
    while (now() - t0 < 2 * S) { if (irqFlag() == 1) sector(); }
    int n = 0;
    param(0x05); cmd("Test05", 0x19, &n);
    if (n) { out(" +"); outDec(n); out("x<1>"); outHex(st1); out(" to "); outHex(ss1); out(":"); outHex(ff1); }
    pause(); flush();
    for (int i = 0; i < ne; i++) {                                    /* printEvents, with the ReadN's time */
        if (lp > 180) flush();
        if (lp == 0) out("  ");
        out(" ");
        if (evK[i] == 2) { out("."); continue; }
        if (evK[i] == 3) { out("|"); outDec((int)readAt); continue; }
        if (evK[i] == 1) out("#");
        if (evS[i] != 0x02) { outHex(evS[i]); out(":"); }
        outHex(evF[i]);
    }
    if (lp) flush();
}

int main() {
    printf("\ncdrom/cd-scex-locd2\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0; ackAll();
    uint16_t prev = initTimer(2, 2); lastT = readTimer(2); clk8 = 0;

    out("B"); param(0x05); cmd("Test05", 0x19, nullptr); flush();
    command(0x0A); waitAny(); drain(); ackAll(); waitAny(); drain(); ackAll();   /* Init */
    command(0x0C); waitAny(); drain(); ackAll();                                 /* Demute */
    param(0x20); command(0x0E); waitAny(); drain(); ackAll();                    /* Setmode 20h: whole sectors, 1x */

    static const uint8_t parks[] = { 0x44, 0x60, 0x64 };   /* BCD frames of 00:02:ff */
    static const int delays[] = { 20, 22, 24, 26, 27, 28, 29, 30 };
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 8; k++) rowMs(delays[k], parks[i]);

    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
