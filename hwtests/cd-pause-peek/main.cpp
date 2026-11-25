/*
 * cdrom/cd-pause-peek - a Pause right after a read's first INT1 at double speed, watched from INSIDE the drive (the 19h,60h peek),
 * right after Setmode 80h and 3 s later (the Fable CD session, 2026-11-23).
 * WHY: hwtests/cd-pause-settle - on the SCPH-1001 (every run) and the second SCPH-5501 the first Pauses after the change take
 * +3-6% (~1.5-2 ms) and then settle; the model's do not, and the disc's sector clock is flat (hwtests/cd-spinramp), so the
 * stretch is inside the Pause. On the drive's program in the model (SC430916, the 1001's family: experiments/scex/pauseram.js) a
 * Pause runs: $6B 01 -> 0C at ~3.7 ms (the jump back), $62 counting 0C -> 00 from ~4 to 13 ms (a settle), the first Q after the
 * landing at ~18.4 ms ($55, the last Q's frame, 18 -> 11), $6B 0D at ~19.5 (tracking by Q), then a Q every sector (12, 13, 14 at
 * ~6.7 ms), INT2 at ~40.8 ms. Which stage stretches on the console - the landing's first Q late (a relock) or the Qs' spacing
 * (the disc after the jump) - is what this reads.
 * WHAT IT DOES: Init, Demute; then nine blocks, each: Setmode 00h, 3 s, Setmode 80h, at once one Pause row, 3 s, one Pause row of
 * the same kind. Kinds in turn: P (peek $55 in a loop from the Pause's INT3 to its INT2), Q (peek $6B), N (no peeks: what a
 * Pause takes with no commands beside it). A Pause row: Setloc 00:02:16, ReadN, its first INT1, Pause; timed from the Pause's
 * INT3 acknowledge to its INT2, as cd-pause.
 * READ IT LIKE THIS: "<kind> once|late pause <cycles> peeks <n>:" then each change of the peeked byte "t=vv", t in tenths of a ms
 * from the INT3 acknowledge. A peek takes ~1.5 ms, so a change is seen up to ~1.5 ms late; the rows average that. Under peeks the
 * Pause's INT2 does not reach the test in the model (a peek sent while it is pending takes its place: "pause 0"), so P / Q rows time
 * the stages from the RAM ($6B 01 is the Pause done) and the N rows carry the INT2. No unlock;
 * SAFE FOR THE DRIVE. About 80 s.
 */
#define CDPAUSE_NO_MAIN
#include "../cd-pause/main.cpp"

static uint32_t evT[64]; static uint8_t evV[64]; static int ne;
static void waitc(uint32_t cycles) { const uint32_t t0 = now(); while (now() - t0 < cycles) {} }

static void pauseRow(char kind, const char* when) {
    const uint32_t S = 33868800;
    param(0x00); param(0x02); param(0x16); simple(0x02);                 /* Setloc 00:02:16 */
    command(0x06); if (waitInt(3) != 3) { drain(); ackAll(); printf("%c %s ReadN refused\n", kind, when); return; }
    drain(); ackAll();
    if (waitInt(1) != 1) { drain(); ackAll(); printf("%c %s no INT1\n", kind, when); return; }
    drain(); ackAll();
    command(0x09);                                                       /* Pause */
    if (waitInt(3) != 3) { drain(); ackAll(); printf("%c %s Pause refused\n", kind, when); return; }
    drain(); ackAll();
    const uint32_t tack = now();
    ne = 0; int peeks = 0, last = -1; uint32_t done = 0;
    const uint16_t addr = kind == 'P' ? 0x55 : 0x6B;
    while (!done && now() - tack < S / 5) {
        if (kind == 'N') { tick(); const uint8_t f = irqFlag() & 7; if (f == 2) { done = now() - tack; drain(); ackAll(); } else if (f) { drain(); ackAll(); } continue; }
        param(0x60); param(addr & 0xFF); param(addr >> 8); command(0x19);
        const uint32_t ts = now();
        for (;;) {
            tick();
            const uint8_t f = irqFlag() & 7;
            if (!f) { if (now() - ts > S / 20) break; continue; }
            if (f == 2) { done = now() - tack; drain(); ackAll(); continue; }   /* the Pause's INT2; its peek's INT3 still to come */
            if (f == 3) {
                const int v = hasResp() ? resp() : -1; drain(); ackAll(); peeks++;
                if (v >= 0 && v != last && ne < 64) { evT[ne] = now() - tack; evV[ne] = (uint8_t)v; ne++; last = v; }
                break;
            }
            drain(); ackAll(); if (f == 5) break;
        }
    }
    printf("%c %s pause %d peeks %d:", kind, when, (int)done, peeks);
    for (int i = 0; i < ne; i++) printf(" %d=%02X", (int)(evT[i] / 3387), evV[i]);
    printf("\n");
}

#ifndef CDPAUSEPEEK_NO_MAIN   /* hwtests/cd-pause-stages includes this file for pauseRow */
int main() {
    printf("\ncdrom/cd-pause-peek\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                  /* Demute */
    if (discSectors() == 0) { printf("disc: none (GetTD gave 0) - nothing to time\n"); restoreTimer(2, prev); ExitCriticalSection(); printf("\nDone.\n"); for (;;) {} }
    const uint32_t S = 33868800;
    static const char kinds[3] = { 'P', 'Q', 'N' };
    for (int b = 0; b < 9; b++) {
        const char k = kinds[b % 3];
        setmode(0x00); waitc(3 * S);
        setmode(0x80); pauseRow(k, "once");
        waitc(3 * S);  pauseRow(k, "late");
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
#endif
