/*
 * hwtests/cd-discid/discset.h - THE LEAD-OUT CHECK, for a test that needs ONE disc of a set (2026-11-19, the Fable CD session; main,
 * for AngryEmpath: "so a wrong disc stops the run with a clear message ... wherever a test needs one specific disc of a set").
 * WHY: a disc check by one sector's file and channel passes on the wrong disc of every set we use (scratch/discsets.js): the four
 * X-Files (USA) discs all have file 1 channel 1 at FOXLOGO.STR's place (51:27:47), FF7 (Japan) Discs 1-3 and FF7 (Europe) Discs 1
 * and 3 all have it at RCKTFAIL.MOV's - yet the sectors differ (scratch/xfilescmp.js, scratch/ff7jp13cmp.js: 0 of 805 / 1500 alike).
 * The SCPH-5000's cd-xa-switch2x-ff7 ran on FF7 (Japan) Disc 3 for Disc 1 so (console-logs/2026-10-27-SCPH-5000-ff7/README.txt: the
 * case's Disc 1 slot holds Disc 3). The lead-out tells every disc of these sets apart: GetTD 0 (14h, 00h -> INT3 stat, mm, ss in BCD;
 * psx-spx "GetTD"), the drive's own table of contents, within a second (mm:ss, frames dropped: cd-discid on the 5000 read 65:09
 * for Disc 3's 65:09:20). A drive that answers INT5 because it has not read its table of contents yet (the PSone's SC430949 after
 * Init, cd-discid's "discid-wait") is asked again, up to 20 times 500 ms apart.
 * Include it after the test's own quiet(), idle(cycles, log) and lastResp / lastN (issue()'s first answer).
 * FOR A NEW TEST. The three that need one disc of a set today (cd-xa-switch2x, -ff7, -ff7eu) keep their exes: built with this check,
 * cd-xa-switch2x-ff7's Disc 1 rows moved by up to 82,288 cycles (commands written tens of cycles later cross sector phases;
 * scratch/leadout/rowdiff.js), and the graders' references for their 65 console logs would no longer be the programs those logs came
 * from (the 5000's six Disc 3 logs none at all). tools/consolerun.js checks their discs instead (NEED_DISC, before the program).
 * Checked on the emulator in all three (scratch/leadout/): the right disc runs, X-Files Disc 2 / FF7 Japan Disc 3 / FF7 Europe Disc 3
 * / Doom stop with WRONG DISC naming the disc that is in.
 */
struct SetDisc { uint8_t mm, ss; const char *name; };

static int leadOutNear(const SetDisc &d, int got) { const int w = d.mm * 60 + d.ss; return got >= w - 1 && got <= w + 1; }

/* True when the drive's lead-out is set[want]'s. Otherwise prints WRONG DISC: what GetTD read, which disc of the set that is (when
 * it is one), what this test needs and the label to look for, and the test runs no row. */
static bool leadOutCheck(const SetDisc *set, int n, int want, const char *label) {
    const uint8_t t0[1] = { 0x00 };
    uint8_t ty = 0; int tries = 0;
    for (; tries < 20; tries++) {
        ty = quiet(0x14, t0, 1);
        if (ty == 3 && lastN >= 3) break;
        idle(16934400, false);                            /* 500 ms */
    }
    const SetDisc &w = set[want];
    if (ty != 3 || lastN < 3) {
        printf("WRONG DISC, OR NONE: GetTD gave no lead-out (INT%d, %d bytes, %d tries). This test needs %s, its lead-out %02d:%02d.\n",
               ty, lastN, tries, w.name, w.mm, w.ss);
        printf("Put in the disc labelled %s, close the lid and run again. No row is run.\n", label);
        return false;
    }
    const int mm = (lastResp[1] >> 4) * 10 + (lastResp[1] & 15), ss = (lastResp[2] >> 4) * 10 + (lastResp[2] & 15), got = mm * 60 + ss;
    if (leadOutNear(w, got)) { printf("lead-out check: %02d:%02d (GetTD), %s's\n", mm, ss, w.name); return true; }
    int is = -1;
    for (int i = 0; i < n; i++) if (i != want && leadOutNear(set[i], got)) is = i;
    printf("WRONG DISC: the drive's lead-out is %02d:%02d (GetTD) - %s%s - and this test needs %s, which ends at %02d:%02d.\n",
           mm, ss, is >= 0 ? "that is " : "not a disc of this set", is >= 0 ? set[is].name : "", w.name, w.mm, w.ss);
    printf("Put in the disc labelled %s, close the lid and run again. No row is run.\n", label);
    return false;
}
