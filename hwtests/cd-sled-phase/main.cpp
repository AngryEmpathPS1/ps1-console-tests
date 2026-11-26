/*
 * cdrom/cd-sled-phase - ONE sled move repeated: is the first COUT edge's time the same every time (the divider restarts at the
 * Reg B latch) or spread over one edge period (it runs free), and the start-up curve averaged - the Fable CD session, 2026-11-25.
 *
 * WHY. hwtests/cd-sled-edges reads the drive program's count of COUT edges to go ($7B, the 19h,60h peek) through a sled move. Its
 * first run on the SCPH-9001: the count is the model's in nearly every cell, the sled's top speed 38,700 tracks/s where
 * src/cdservo.js has 42,500, and the landings fall ~300 tracks shorter. The same console's landings on 2026-10-20 (cd-seek-long,
 * 14 runs a cell) spread 270-330 tracks wide in every cell - one COUT period (256 tracks). The servo counts COUT from the Reg B
 * latch (v0.377.152, from Sony's "High when Reg B is latched") and leaves crossings under 20,000 tracks/s uncounted (FITTED on
 * landings): every landing of a cell is then the same. If the divider runs free the first edge comes after 0-256 tracks, a
 * different place each time, and the fitted floor stood in for it. cd-sled-edges runs each cell once; this repeats one.
 *
 * WHAT IT DOES. cd-sled-edges' seekEdges (its file included, its main left out): a SeekL with $7B peeked from its INT3 until the
 * count has run out, then GetlocP every quarter sector to 2 s from the command. Three pairs of places ten minutes apart - 00:02
 * and 10:00, 30:00 and 40:00, FAR less 10 minutes and FAR (40 s short of GetTD's lead-out; a pair past FAR is left out) - each
 * pair crossed out and back ten times at single speed, every crossing one row: the edges and the landing. No unlock; SAFE FOR
 * THE DRIVE. About 2.5 minutes.
 *
 * READ IT LIKE THIS. "<from> -> <to> #n : edges peeks <n> in <k>k: t=vv ... : then time(M):sector-less-target ...", t in thousands of
 * CPU cycles from the SeekL's command write. Per pair and direction: a fixed phase gives every edge the same time within a peek
 * (~1.7 ms = ~58k) over the ten rows and one landing; a free divider moves all a row's edges together, by up to one edge period
 * (~6.6 ms = ~224k at speed), and the landing with them (earlier edges, a shorter landing).
 *
 * Run it here first: `node hwtests/run.js cd-sled-phase --disc=xfiles --settle=600 --frames=11000 --quiet=10000` (PSX_MODEL and
 * PSX_CD_FIRMWARE the console's).
 */
#define CDSLEDEDGES_NO_MAIN
#include "../cd-sled-edges/main.cpp"

int main() {
    printf("\ncdrom/cd-sled-phase\n");
    printf("One sled move repeated: three pairs of places ten minutes apart, each crossed out and back ten times at single speed with\n");
    printf("$7B (COUT edges to go) peeked: every change t=vv, t in k cycles from the SeekL's command write, then the path (time(M):\n");
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
    const int32_t PAIR[3][2] = { { 0, MIN(10, 0) }, { MIN(30, 0), MIN(40, 0) }, { far - 45000, far } };
    for (int p = 0; p < 3; p++) {
        if (PAIR[p][0] < 0 || PAIR[p][1] > far) continue;   /* not this disc */
        seekTo((uint32_t)PAIR[p][0]); getlocP();
        for (int i = 0; i < 20; i++) {
            const int32_t o = PAIR[p][i & 1], t = PAIR[p][(i & 1) ^ 1];
            seekEdges((uint32_t)t, 112896u);
            const uint32_t a = (uint32_t)o + 150, b = (uint32_t)t + 150;
            printf("%02d:%02d -> %02d:%02d #%d : edges peeks %d in %uk:", (int)(a / 4500), (int)((a / 75) % 60), (int)(b / 4500), (int)((b / 75) % 60), i / 2, peeks, (unsigned)(peekEnd / 1000u));
            for (int k = 0; k < ne; k++) printf(" %u=%02X", (unsigned)(evT[k] / 1000u), evV[k]);
            printf(" : then");
            printPath(t, 10);
            printf("\n");
        }
    }
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-sled-phase: Done.\n");
    for (;;) VSync(0);
}
