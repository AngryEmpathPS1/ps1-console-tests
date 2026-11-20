THE DISC IN THIS FOLDER IS FINAL FANTASY VII (JAPAN) DISC 3 (SLPS-00702), every log. Not Disc 1.
(The Fable CD session, 2026-11-19, from AngryEmpath the same day: the FF7 case's Disc 1 slot holds Disc 3, it is the disc that went
into the SCPH-5000 every time, and it is the only FF7 Japan disc the 5000 has ever run. The run scripts - experiments/run5000cd.ps1,
run5000cd2.ps1, run5000cd3.ps1 - and the run sheet planned on Disc 1; they are corrected to Disc 3 / --disc=ff7jp3.)

The logs said so before anyone knew: cd-leadout and cd-pastend read the lead-out at 65:09 (293,175 sectors) - Disc 3's, 293,045
sectors; Disc 1's is 70:25 (316,741). The CD geometry session found it on 2026-11-12 (STATUS "v0.377.333") and set disc.json to
ff7jp3, so every reference run since grades these logs on Disc 3. Grades before v0.377.333 ran the model on Disc 1.

cd-xa-switch2x-ff7 (six logs here) is Disc 1's recipe: RCKTFAIL.MOV at 54:20:32 (244382), and plain data 805 sectors before it
(54:09:52, 243577) for the seek back, as The X-Files' FOXLOGO.STR -> LOADING.BS. RCKTFAIL.MOV is on Disc 1 only. On Disc 3 both
places are inside ENDING02.MOV (43:37:64 to 61:25:67, 80,104 sectors; experiments/ff7_ls.js with FF7_DISC= the Disc 3 image), and
the logs show it: the disc check reads "54:20:32 file 1 channel 1 submode 48" (Disc 1's video sectors there are submode 42h, its
audio coding 41h; Disc 3's 48h / 01h), and the PAUSE rows' sectors at 54:09:52 are "1:1 48", movie sectors, where Disc 1 has plain
data (0:0 08). The test's check (file 1 channel 1 only) passed on both discs. So these six measured leaving ENDING02.MOV at double
speed and seeking back into the same movie: a valid measurement on Disc 3 (the references run the same sectors), not the recipe's
movie-to-data switch, which still needs Disc 1 in the 5000 (read the label: SLPS-00700). The 80,104-sector movie leaves Disc 3 no
plain data near that radius, so the recipe cannot move to Disc 3.
