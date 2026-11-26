/*
 * cdrom/cd-sled-edges - the sled's own speed curve, read from INSIDE the drive: the program's count of COUT edges still to go ($7B,
 * the 19h,60h peek) through a long sled move, each edge timed - the Fable CD session, 2026-11-25.
 *
 * WHY. hwtests/cd-seek-far (the SCPH-9001 on Final Fantasy VII (USA), the 7502 and 1001 on The X-Files): past 50:00 the consoles'
 * sleds land farther from the target than src/cdservo.js's, outward and inward (00:02 -> 69:59 13.4k sectors short against 9.8k,
 * 1.02 s against 0.86), and a hop 60:00 -> 69:59 lands 15-24k sectors short where the servo lands 2-9k. The drive's program stops a
 * long move on a COUNT: it works out the tracks from its own table, keeps two or three COUT edges (256 tracks each) in hand, drives
 * the sled until $7B edges have come, reverses it for 34 ms and turns tracking on (docs/DRIVE-PROGRAM.md 6.2; $7B is the same
 * byte on SC430916 / 930 / 934 / 940 / 944: experiments/sled/longmove.js). The servo's sled (42,500 tracks/s after 25 ms, crossings
 * under 20,000 tracks/s uncounted) was fitted on LANDINGS to 50:00 alone. The landing mixes four things - the count the program
 * chose, the ground before the first edge, the sled's speed, the coast after the stop. $7B separates them: its value before the
 * first edge is the count, each decrement is 256 tracks more (the speed, edge by edge and by radius), and the landing less the
 * count is the start and the coast.
 *
 * WHAT IT DOES. cd-seek-far's framework (its file included, its main left out). Per cell (speed, from, to), three SeekLs from the
 * origin: the CONTROL (timed to its INT2, nothing beside it); the PATH (GetlocP every quarter sector, cd-seek-long's row: the
 * landing with no peeks); the EDGES - 19h,60h,7Bh,00h in a loop from the SeekL's INT3 until $7B has read non-zero and then zero
 * (or 1.5 s), every change kept with its time, then GetlocP every quarter sector to 2 s (the landing under peeks, against the
 * PATH row's). Cells: full strokes and the fitted region (00:02 <-> FAR, 50:00, 30:00), hops of 2:30 / 5 / 10 minutes out from
 * 00:02 and 5 / 10 / 15 minutes up to FAR, the same inward, 25:00 <-> 35:00; seven of them again at double speed. FAR is 40 s
 * short of GetTD's lead-out; a cell past FAR is left out (a short disc runs the hops). No unlock; SAFE FOR THE DRIVE. ~3.5 min.
 *
 * READ IT LIKE THIS. Per cell: "ctl" the control's M cycles; "path" time(M):sector-less-target where GetlocP's sector jumped (the
 * first far one the sled's landing); "edges peeks <n> in <k>k:" then every change of $7B as t=vv, t in thousands of CPU cycles
 * from the SeekL's command write (a peek takes ~1.5 ms = ~50k, so a change is seen up to that late); "then" the path after the
 * move. The count the program chose is the value before the decrements start (the few rising values before it are its arithmetic).
 *
 * Run it here first: `node hwtests/run.js cd-sled-edges --disc=xfiles --settle=600 --frames=16000 --quiet=15000` (PSX_MODEL and
 * PSX_CD_FIRMWARE the console's; --settle so the disc is read in before GetTD).
 */
#define CDSEEKFAR_NO_MAIN
#include "../cd-seek-far/main.cpp"

#define NE 160
static uint32_t evT[NE]; static uint8_t evV[NE]; static int ne, peeks; static uint32_t peekEnd;

/* SeekL to lba; $7B peeked from its INT3 until it has been non-zero and reads zero again (or 1.5 s, or the seek's INT2); then
 * GetlocP every `gap` cycles to LIMIT, the path kept where the sector jumps (seekPath's rule). */
static void seekEdges(uint32_t lba, uint32_t gap) {
    ne = 0; np = 0; peeks = 0; peekEnd = 0;
    setloc(lba);
    const uint32_t t0 = now();
    command(0x15);
    if (waitInt(3) != 3) { drain(); ackAll(); return; }
    drain(); ackAll();
    uint32_t t2 = 0; int last = -1, seen = 0, over = 0;
    while (!over && !t2 && now() - t0 < 50803200u) {
        param(0x60); param(0x7B); param(0x00); command(0x19);
        const uint32_t ts = now();
        for (;;) {
            const uint8_t f = irqFlag() & 7;
            if (!f) { if (now() - ts > 1693440u) { over = 1; break; } continue; }   /* 50 ms without an answer: stop peeking */
            if (f == 2) { const uint8_t g = irqFlag() & 7; if (g == 2) { t2 = now() - t0; drain(); ackAll(); } continue; }
            if (f == 3) {
                const int v = hasResp() ? resp() : -1; drain(); ackAll(); peeks++;
                if (v >= 0 && v != last && ne < NE) { evT[ne] = now() - t0; evV[ne] = (uint8_t)v; ne++; last = v; }
                if (v > 0) seen = 1; else if (v == 0 && seen) over = 1;
                break;
            }
            drain(); ackAll(); if (f == 5) { over = 1; break; }
        }
    }
    peekEnd = now() - t0;
    int32_t lastS = -99999;
    while (now() - t0 < LIMIT) {
        const uint32_t tw = now();
        while (now() - tw < gap) { const uint8_t f = irqFlag() & 7; if (f) { drain(); ackAll(); } }
        command(0x11);
        int kind = 0; const int32_t sec = answer(&kind, t0, &t2);
        if (!kind) break;
        if ((lastS == -99999 || sec < 0 || sec - lastS > 2 || lastS - sec > 2) && np < NP) { pathT[np] = now() - t0; pathS[np] = sec; np++; }
        if (sec >= 0) lastS = sec;
        endT = now() - t0; endS = sec;
    }
    if (np < NP) { pathT[np] = endT; pathS[np] = endS; np++; }
}
static void printPath(int32_t target, int lim) {
    int errs = 0;
    for (int i = 0; i < np && i < lim; i++) {
        if (pathS[i] < 0) { errs++; continue; }   /* the hold's error answers near the hub: counted, not listed */
        printf(" %u.%02u:%d", (unsigned)(pathT[i] / 1000000u), (unsigned)((pathT[i] % 1000000u) / 10000u), (int)(pathS[i] - target));
    }
    if (errs) printf(" E%d", errs);
}

#define Z 0
#define MIN(m, s) ((int32_t)(((m) * 60 + (s)) * 75 - 150))
#define FARm(m) (-10 - (m))   /* FAR less m minutes */
#define NCE 25
static int32_t CE[NCE][3] = {
    { 0, Z, FARm(0) }, { 0, FARm(0), Z }, { 0, Z, MIN(50, 0) }, { 0, MIN(50, 0), Z }, { 0, Z, MIN(30, 0) }, { 0, MIN(30, 0), Z },
    { 0, Z, MIN(2, 30) }, { 0, Z, MIN(5, 0) }, { 0, Z, MIN(10, 0) }, { 0, MIN(5, 0), Z }, { 0, MIN(10, 0), Z },
    { 0, MIN(25, 0), MIN(35, 0) }, { 0, MIN(35, 0), MIN(25, 0) },
    { 0, FARm(5), FARm(0) }, { 0, FARm(10), FARm(0) }, { 0, FARm(15), FARm(0) }, { 0, FARm(0), FARm(10) }, { 0, FARm(0), FARm(15) },
    { 1, Z, FARm(0) }, { 1, FARm(0), Z }, { 1, Z, MIN(50, 0) }, { 1, MIN(50, 0), Z }, { 1, Z, MIN(5, 0) }, { 1, FARm(10), FARm(0) }, { 1, FARm(0), FARm(10) } };

#ifndef CDSLEDEDGES_NO_MAIN   /* hwtests/cd-sled-phase includes this file for seekEdges */
int main() {
    printf("\ncdrom/cd-sled-edges\n");
    printf("The sled's speed curve from inside the drive: per cell the control SeekL (M cycles), the polled SeekL's path (time(M):sector\n");
    printf("less target where GetlocP jumped), then the same SeekL with $7B (COUT edges to go) peeked: every change t=vv, t in k cycles\n");
    printf("from the command write, and the path after. FAR is 40 s short of GetTD's lead-out. No assertions.\n\n");
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
    if (far < 16 * 60 * 75) { printf("disc too short (needs 17 minutes)\n"); restoreTimer(2, prev); ExitCriticalSection(); printf("\ncdrom/cd-sled-edges: Done.\n"); for (;;) VSync(0); }
    int speedNow = -1;
    for (int c = 0; c < NCE; c++) {
        int32_t o = CE[c][1], t = CE[c][2];
        if (o <= -10) o = far - (-10 - o) * 4500;
        if (t <= -10) t = far - (-10 - t) * 4500;
        if (o > far || t > far || o < 0 || t < 0) continue;   /* past FAR: not this disc */
        const int speed = CE[c][0]; const uint32_t origin = (uint32_t)o, target = (uint32_t)t;
        if (speed != speedNow) { setmode(speed ? 0x80 : 0x00); speedNow = speed; }
        const uint32_t half = speed ? 112896u : 225792u;   /* half a sector at that speed */
        seekTo(origin); getlocP();
        ctlT = seekTo(target);
        const uint32_t a = origin + 150, b = target + 150;
        printf("%s %02d:%02d -> %02d:%02d : ctl %u.%02u : path", speed ? "2x" : "1x", (int)(a / 4500), (int)((a / 75) % 60), (int)(b / 4500), (int)((b / 75) % 60),
               (unsigned)(ctlT / 1000000u), (unsigned)((ctlT % 1000000u) / 10000u));
        seekTo(origin); getlocP();
        seekPath(target, half / 2);
        printPath((int32_t)target, 24);
        seekTo(origin); getlocP();
        seekEdges(target, half / 2);
        printf(" : edges peeks %d in %uk:", peeks, (unsigned)(peekEnd / 1000u));
        for (int i = 0; i < ne; i++) printf(" %u=%02X", (unsigned)(evT[i] / 1000u), evV[i]);
        printf(" : then");
        printPath((int32_t)target, 24);
        printf("\n");
    }
    setmode(0x00);
    restoreTimer(2, prev);
    ExitCriticalSection();
    printf("\nwaits that ran out: %d\n", timeouts);
    printf("\ncdrom/cd-sled-edges: Done.\n");
    for (;;) VSync(0);
}
#endif
