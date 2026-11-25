/*
 * cdrom/cd-pause-stages - which stage of a Pause carries the SCPH-1001's long first Pauses after an idle: cd-pause-settle's cells of
 * eight Pauses, each watched from inside the drive (the 19h,60h peek of the stage byte 6Bh) - the Fable CD session, 2026-11-25.
 *
 * WHY. cd-pause-settle on the 1001 (The X-Files, every run): a cell's first Pauses (Setloc 00:02:16, ReadN, its first INT1, Pause) run
 * long and settle over ~8 - right after Setmode 80h (1.39-1.41M), and after a 3 s idle at double speed too (1.355M -> 1.330M) - where
 * the model is flat at ~1.33M. hwtests/cd-lagdip: the 1001's data lag dips only ~12k cycles at a read's start after an idle - too
 * little for +50-70k. The program's Pause (SC430916; experiments/scex/pausegate.js): $6B 01 (taken) -> 0C (the jump back, kicked at
 * the first Q after it) -> 0D (landed, tracking by Q) -> 01 (done), its INT2 a millisecond after. Timed per Pause, the stage that
 * grows at a cell's start is the mechanism.
 *
 * WHAT IT DOES. cd-pause-peek's Pause row (its file included, its main left out): Q rows peek 6Bh in a loop from the Pause's INT3
 * acknowledge, every change "t=vv" in tenths of a ms (a peek ~1.5 ms; under peeks the INT2 does not reach the test - "pause 0"); N
 * rows no peeks (the Pause's own time). Init, Demute; Setmode 00h, 1 s, Setmode 80h, cell A (8 Q rows at once); 3 s, cell B (8 Q);
 * 3 s, cell C (8 N); 3 s, cell D (8 Q). Prints each row with its cell and index. No unlock; SAFE FOR THE DRIVE. About 40 s.
 *
 * Run here first: `node hwtests/run.js cd-pause-stages --disc=xfiles --frames=5000 --quiet=4000 --settle=600` (PSX_MODEL and
 * PSX_CD_FIRMWARE the console's).
 */
#define CDPAUSEPEEK_NO_MAIN
#include "../cd-pause-peek/main.cpp"

static const char *LBL[4][8] = { { "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7" }, { "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7" },
                                 { "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7" }, { "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7" } };
int main() {
    printf("\ncdrom/cd-pause-stages\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                  /* Demute */
    if (discSectors() == 0) { printf("disc: none (GetTD gave 0) - nothing to time\n"); restoreTimer(2, prev); ExitCriticalSection(); printf("\nDone.\n"); for (;;) {} }
    const uint32_t S = 33868800;
    setmode(0x00); waitc(S);
    setmode(0x80);
    for (int c = 0; c < 4; c++) {
        if (c) waitc(3 * S);
        for (int i = 0; i < 8; i++) pauseRow(c == 2 ? 'N' : 'Q', LBL[c][i]);
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
