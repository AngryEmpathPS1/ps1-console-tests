/*
 * cdrom/cd-sled-swing - the sled's landing against the count of edges it was driven, 1 to 10, out and in, at three places on the
 * disc, with the drive's own count ($7B, the 19h,60h peek) and its edge times beside every landing - the Fable CD session, 2026-11-25.
 *
 * WHY. hwtests/cd-seek-coast (2026-10-21/22, the SCPH-9001 x12 and 7502 x14 a cell; experiments/sled/coastextra.js with the count N
 * from experiments/sled/countof.js): from LBA 1,000 outward the tracks the sled moves past its count (the landing less N x 256) go
 * 632 / 911 / 826 / 731 / 533 / 595 / 716 at N = 1 / 2 / 3 / 4 / 6 / 7 / 10 on the 9001 and 770 / 924 / 805 / 792 / 673 / 612 / 808
 * on the 7502 - a swing with the count, up at 2, down at 6-7, up again at 10 (~50 ms a turn, ~19 Hz), and the first Q after a move
 * of 1-2 edges comes 15-25 ms later than src/cdservo.js's where 6+ edges come level. Read as the pickup's LENS on its suspension:
 * tracking is off through a sled move, the sled's start throws the lens back, COUT counts the lens's crossings, and the landing is
 * where the lens is caught. The servo has no lens (its floor - crossings under 20,000 tracks/s uncounted, FITTED v0.377.152 - stood
 * in). But cd-seek-coast's inward cells (from 130,462) swing otherwise, each console its own way, it has four counts there, no
 * count read from the drive and no edge times. This sweeps the count at three places, both ways, with them.
 *
 * WHAT IT DOES. cd-sled-edges' seekEdges (its file included, its main left out): a SeekL with $7B peeked from its INT3 until the
 * count has run out, then GetlocP every quarter sector to 2 s from the command. Three places - 01:00, 30:00 and FAR (40 s short of
 * GetTD's lead-out) - and sixteen distances each (8,000 + k x 1,900 sectors at 01:00, 13,000 + k x 3,000 at 30:00, 16,000 + k x
 * 3,700 at FAR: about 0 to 10 edges): out from 01:00 and 30:00 and back, in from FAR and back, every move one row. A move past
 * FAR or the disc's start is left out. Single speed. No unlock; SAFE FOR THE DRIVE. About 3.5 minutes.
 *
 * READ IT LIKE THIS. "<from> -> <to> by <sectors> : edges peeks <n> in <k>k: t=vv ... : then time(M):sector-less-target ...", t in
 * thousands of CPU cycles from the SeekL's command write; a row with no count (a move the program made by jumps alone) keeps only
 * its path. experiments/sled/swing-score.js: per place and direction, N against the tracks moved less N x 256 and the first Q's time.
 *
 * Run it here first: `node hwtests/run.js cd-sled-swing --disc=xfiles --settle=600 --frames=15000 --quiet=14000` (PSX_MODEL and
 * PSX_CD_FIRMWARE the console's).
 */
#define CDSLEDEDGES_NO_MAIN
#include "../cd-sled-edges/main.cpp"

static void row(int32_t o, int32_t t) {
    seekEdges((uint32_t)t, 112896u);
    const uint32_t a = (uint32_t)o + 150, b = (uint32_t)t + 150;
    printf("%02d:%02d:%02d -> %02d:%02d:%02d by %d : edges peeks %d in %uk:", (int)(a / 4500), (int)((a / 75) % 60), (int)(a % 75), (int)(b / 4500), (int)((b / 75) % 60), (int)(b % 75),
           (int)(t - o), peeks, (unsigned)(peekEnd / 1000u));
    for (int k = 0; k < ne; k++) printf(" %u=%02X", (unsigned)(evT[k] / 1000u), evV[k]);
    printf(" : then");
    printPath(t, 8);
    printf("\n");
}

int main() {
    printf("\ncdrom/cd-sled-swing\n");
    printf("The sled's landing against the count of edges it was driven: sixteen distances out and back at 01:00, 30:00 and FAR, single\n");
    printf("speed, $7B (COUT edges to go) peeked: every change t=vv, t in k cycles from the SeekL's command write, then the path (time(M):\n");
    printf("sector less target where GetlocP jumped). FAR is 40 s short of GetTD's lead-out. No assertions.\n\n");
    EnterCriticalSection();
    idx(1); *CD2 = 0;
    ackAll();
    uint16_t prev = initTimer(2, 2);
    lastT = readTimer(2); clk8 = 0;
    command(0x0A); waitInt(3); drain(); ackAll(); waitInt(2); drain(); ackAll();   /* Init */
    simple(0x0C);                                                                   /* Demute */
    param(0); command(0x14);                                                        /* GetTD(0): the lead-out, MM SS */
    int32_t leadout = 0;
    if (waitInt(3) == 3) { uint8_t r[4]; int n = 0; while (hasResp() && n < 4) r[n++] = resp(); if (n >= 3) leadout = (unbcd(r[1]) * 60 + unbcd(r[2])) * 75 - 150; }
    drain(); ackAll();
    const int32_t far = leadout - 40 * 75;
    printf("lead-out %d (GetTD), FAR %d\n", (int)leadout, (int)far);
    setmode(0x00);
    const int32_t ORG[3] = { MIN(1, 0), MIN(30, 0), far }, D0[3] = { 8000, 13000, -16000 }, DS[3] = { 1900, 3000, -3700 };
    for (int p = 0; p < 3; p++) {
        const int32_t o = ORG[p];
        if (o < 0 || o > far) continue;   /* not this disc */
        seekTo((uint32_t)o); getlocP();
        for (int k = 0; k < 16; k++) {
            const int32_t t = o + D0[p] + k * DS[p];
            if (t < 0 || t > far) continue;
            row(o, t);
            row(t, o);
        }
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-sled-swing: Done.\n");
    for (;;) VSync(0);
}
