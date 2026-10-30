# CD-ROM drive

What the drive does as the console sees it: command and acknowledge times, sector delivery, the sector buffer, Pause, seeks, spin-up, speed changes, the lid, and the SCEx counters. Everything here was read from logs of real consoles. Each finding names the test programs (`hwtests/<name>`) and logs (`consoles/<model>/<session>/<name>-<model>[-n].txt`) behind it. Where psx-spx or another reference says something different, the finding says so in one line.

## Conventions

- Times are CPU cycles at 33,868,800 Hz unless a unit is given. 1 ms = 33,869 cycles. One sector lasts 451,584 cycles at single speed (1x) and 225,792 at double speed (2x); "sectors" below means that sector time.
- Disc: The X-Files (USA) Disc 1 (pressed, one data track, ends at 57:59) for nearly every log. The SCPH-5000 (Japan) ran Final Fantasy VII (Japan) Disc 1. The SCPH-7502 (Europe) read the American disc after the unlock commands of psx-spx ("Secret Unlock Commands") were sent first.
- Consoles: SCPH-1001, 5000, 5501 (two units), 7001, 7501, 7502 (PAL), 9001 (one main unit; a second unit in a few runs). The first SCPH-5501's drive appears worn: its seek, spin-down and speed-change times show outliers the second unit does not. Units of one model are listed apart only where they differ.
- Evidence lines list runs as `model ×count`. A run is one complete execution of the test. Most tests ran 2 to 13 times per console. Nearly all runs were started through the resident `hwtests/runner`, which gives every test the same start; the two earliest sessions loaded each test on its own without it. Only one finding (the N5 row, finding 20) depends on that.
- Interrupt names are psx-spx's: INT1 sector, INT2 second response, INT3 first response, INT5 error.

## Drive identity

**1. The controller reports a version date, a region and three chip names, and they separate the models.**

| Console | BIOS | Controller (Test 19h,20h) | Region (19h,22h) | Servo / signal / decoder (19h,23h-25h) |
|---|---|---|---|---|
| SCPH-1001 | 2.1 07/17/95 A | 95/07/24 C1 | for U/C | CXD1782BR / CXD2510Q / CXD1199BQ |
| SCPH-5000 | 2.2 12/04/95 J | 95/07/24 C1 | for Japan | same as the 1001 |
| SCPH-5501 | 3.0 11/18/96 A | 97/01/10 C2 | for U/C | CXD2545Q / CXD2545Q / CXD1815Q |
| SCPH-7001 | 4.1 12/16/97 A | 97/08/14 C2 | for U/C | CXD1817Q (all three) |
| SCPH-7501 | 4.1 12/16/97 A | 98/06/10 C3 | for U/C | CXD2940Q (all three) |
| SCPH-7502 | 4.1 12/16/97 E | 98/06/10 C3 | for Europe | CXD2940Q (all three) |
| SCPH-9001 | 4.1 12/16/97 A | 99/02/01 C3 | for U/C | CXD2940Q (all three) |

psx-spx notes that some of these strings are not the real chip names (CXD1782BR is a CXA1782BR); they are what the drive answers. The strings never changed between sessions. Many of the differences below follow the families: C1 (1001, 5000), C2 (5501, 7001), C3 (7501, 7502, 9001).
Evidence: machine-id; every model, 1 to 6 sessions each.

## Command acknowledge times

**2. A command's first response arrives 0.5 to 6 ms after the write, and the spread is wide.** The drive answers from its main loop, so each command has a floor and a long tail. Pooled over all seven models, 3,968 Getstats: 53% answered in 16,384 to 32,767 cycles, 80% within 49,152, 96% within 98,304, and 0.7% took 180,224 or more. Range of the per-console figures:

| Command | Fastest answer | Mean |
|---|---|---|
| Pause (already paused) | 18,967 - 20,207 | 38,803 - 42,529 |
| Getstat | 18,392 - 19,474 | 38,293 - 42,857 |
| GetID | 18,959 - 20,379 | 39,806 - 42,475 |
| Setmode (no speed change) | 20,204 - 21,783 | 39,971 - 42,086 |
| Mute / Demute | 20,866 - 22,555 | 40,413 - 44,842 |
| GetTN | 21,666 - 23,640 | 40,691 - 43,625 |
| SeekL to where the head is | 21,114 - 22,903 | 31,420 - 38,602 |
| ReadN | 22,096 - 23,571 | 34,575 - 42,936 |
| Setfilter | 22,341 - 23,756 | 41,581 - 45,461 |
| GetTD | 23,874 - 25,174 | 42,244 - 46,458 |
| Test 20h | 25,645 - 27,313 | 44,880 - 48,657 |
| Getparam | 25,964 - 27,870 | 45,262 - 49,454 |
| Setloc | 27,636 - 29,648 | 47,150 - 52,167 |
| GetlocL, GetlocP | 32,469 - 34,815 | 53,311 - 58,017 |

The slowest single answer seen was 218,022 cycles (6.4 ms). psx-spx (nocash, PAL PSone) lists Getstat at 19,059 fastest, 200,027 slowest and 50,401 mean: the fastest and slowest agree, the mean is higher than on any console here.
Cross-check with ps1-tests `cdrom/timing` (Jakub Czekanski) on the same consoles: Nop fastest 18,952 - 23,856, mean 41,972 - 46,004; Init's first response fastest 60,104 - 67,808, mean 72,201 - 81,265.
Evidence: cd-ack-times (64 trials per command per run, 0 to 2 ms apart), 1001 ×7, 5000 ×7, 5501 ×10 (+3 on unit 2), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×10 (+3 on the second unit); cdrom_timing (ps1-tests), 1001 ×7, 5000 ×4, 5501 ×11, 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×13.

**3. Waits that count drive-CPU cycles run about 5% longer on the SCPH-1001 and SCPH-5000.** Against the other five consoles: Getstat's fastest answer 19,328 / 19,474 against 18,392 - 18,593 (ratio 1.05); GetID's INT2 20,916 / 21,036 against 19,846 - 19,895 (1.05, 1.06; the 7502's 20,068 aside); Pause-when-paused INT2 8,568 / 8,645 against 8,137 - 8,183 (1.05, 1.06); a sector held behind an INT3 follows it by 16,656 / 16,768 or more against 15,872 or more (1.05). psx-spx gives the drive oscillator as 4.0 MHz on early boards and 4.2336 MHz from PU-18 on, a ratio of 1.058.
Open: the drive program's own tick counter (read with Test 19h,60h) runs at about 900 a second idle on both families (finding 42), so the oscillator was not read directly.
Evidence: cd-ack-times, cd-init, cd-race, as findings 2, 4 and 12.

**4. Second responses come at fixed delays after the first.**

| Second response | 5501, 7001, 7501, 9001 | 7502 | 1001 | 5000 | psx-spx |
|---|---|---|---|---|---|
| GetID INT2 after its INT3 (mean; fastest - slowest) | 19,846 - 19,895 (19,498 - 20,600) | 20,068 | 20,916 | 21,036 | 18,944 |
| Pause on a paused drive | 8,137 - 8,183 (7,989 - 9,077) | 8,152 | 8,568 | 8,645 | 7,666 |
| Stop on a stopped drive | 8,036 - 8,318 | 8,435 | 8,655 | 8,687 | 7,547 |

The consoles run 3 to 8% above nocash's PAL PSone averages.
Evidence: cd-ack-times (GetID, Pause), 1001 ×7, 5000 ×7, 5501 ×13, 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×13; cd-init (Stop again), 1001 ×4, 5000 ×6, 5501 ×8, 7001 ×4, 7501 ×8, 7502 ×4, 9001 ×8.

## Commands in unusual states

**5. Init written with four parameters is always refused: INT5, error byte 20h.** 15,872 of 15,872 trials on all eight units. The INT5 came 26,624 to 28,440 cycles after the write at fastest and 186,440 to 234,264 at slowest. 1.8 to 2.6% of the trials (random phase against the drive's loop) took longer than 3.5 ms (118,538 cycles): 1001 1.8%, 5000 2.3%, 5501 2.5% (unit 2 2.3%), 7001 2.3%, 7501 2.6%, 7502 1.8%, 9001 2.5%. Redux's nugget test `cdlInitWithArgs` expects under 3.5 ms, so about 2% of single attempts on a real console miss it.
Evidence: cd-init-args, 1001 ×7, 5000 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×13; 256 trials per run.

**6. MotorOn sent during a read is refused and the read goes on.** The response is INT5, stat 23h, error 20h. Every following INT1 carries stat 23h (the error bit stays set) until the next command; Getstat then reads 22h. In 11 of the hand-loaded early runs the stat was 33h / 32h instead (the shell-open bit still set). The sequence is rama's, from a bus-sniffer log on a console (MiSTer issue 169); the logs here reproduce it on all models.
Evidence: cd-cmds (row R), 68 of 68 runs: 1001 ×7, 5000 ×7, 5501 ×13 (+3), 7001 ×7, 7501 ×16, 7502 ×2, 9001 ×10 (+3).

**7. Any command written during a SeekL deletes the SeekL's INT2.** The command gets its own INT3 (a refused Pause gets INT5) and the seek's INT2 never arrives, although the head reaches the target and holds there. Tried: Getstat, Setloc, Mute, Demute, Setmode, Setfilter, GetlocL, GetlocP, GetTN, GetTD, Test 20h and Pause, 0.2M and 2M cycles into a 29:01 to 50:00 seek and a 29:01 to +6 seek; none ever saw the INT2. GetID is the one exception: its INT3 and its own INT2 arrive, the seek's does not. Control rows with no command always saw the INT2. psx-spx says only that GetlocP "does work during Seek": it does, but the seek then never answers.
Evidence: cd-seek-cmd, 1001 ×3, 5000 ×2, 5501 ×3 (+3), 7001 ×3, 7501 ×7, 7502 ×7, 9001 ×7 (4 cells per command per run); cd-seek-pause, same runs, Pause refused with INT5 and no INT2 in every row (e.g. 7501: 56 of 56).

**8. A Pause written soon after a ReadN or SeekL is refused: INT5, error 80h.** From 0 to 80 ms after the INT3 of a ReadN or SeekL, every Pause was refused (0 of 186 accepted at each delay). At 100 and 150 ms, 14 of 186 (ReadN) and 33 / 28 of 186 (SeekL) were accepted, all on the SCPH-7001, 7501, 7502 and 9001; the 1001, 5000 and 5501 refused every one (the test stops at 150 ms).
Evidence: cd-ack-times (PAUSE rows, 3 trials per delay per run), 1001 ×7, 5000 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×10 (+3).

**9. A command written behind an unacknowledged interrupt waits for the acknowledge, then answers as a fresh command.** Getparam written behind a Getstat INT3 that was acknowledged 0.1, 1 or 5 ms later: its INT3 came 20,483 to 36,571 cycles after the acknowledge at fastest, 37,052 to 49,324 on average, up to 191,100 at slowest. This is psx-spx's rule (the command "won't be executed yet").
Evidence: cd-ack-times (PENDING rows, 56 to 104 trials per acknowledge delay), 1001 ×7, 5501 ×13, 7501 ×13, 9001 ×13 tabulated; all seven consoles ran it.

## Sector delivery

**10. Sectors arrive on an exact clock with up to about ±32,000 cycles of jitter per interval, at both speeds.** Means over 300 intervals per run: 1x 451,496 - 451,642 (nominal 451,584); 2x 225,699 - 225,842 (nominal 225,792, which is also psx-spx's formula). Standard deviation 6.6k - 10.8k cycles. Extremes over all runs: 1x 419,896 - 483,680; 2x 194,392 - 257,376 (-14% to +14% of a 2x sector). One SCPH-5000 run (left out of these ranges) had a single 6,788,328-cycle gap, a lost sector. psx-spx (nocash) lists means of 451,021 and 224,466, a little under nominal, and narrower extremes.
Evidence: cd-int1-spread, 1001 ×7, 5000 ×7, 5501 ×9 at 1x and ×11 at 2x (both units), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×13; 300 intervals per speed per run, each sector taken as a game takes it (acknowledged about 1,900 cycles after the interrupt shows).

**11. A command written just before a sector is due beats it only if it is early enough.** D is how many cycles before the sector's due time the Getstat was written. Share of trials where the command's INT3 came before the sector's INT1, over all seven consoles:

| D (cycles) | ≤20k | 30k | 35k | 40k | 45k | 50k | 55k | 60k | 65k | 70k | ≥100k |
|---|---|---|---|---|---|---|---|---|---|---|---|
| at 2x | 0% | 2% | 10% | 28% | 45% | 68% | 84% | 87% | 96% | 99% | 100% |
| at 1x | 0% | 3% | 14% | 28% | 37% | 60% | 85% | 93% | 96% | 99% | 100% |

The switch is half-way near D = 47,000 (1.4 ms) and finished by 70,000. A fixed order in either direction is wrong on one side of this. No console stood apart.
Evidence: cd-race, 1001 ×7, 5000 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×10 (+3); 3 trials per D per run, 183 to 186 trials per cell.

**12. A sector that waited behind a command's INT3 is announced 15,900 to 16,500 cycles later.** Measured from the INT3, which was acknowledged at once. Two clusters about 460 cycles apart (15,872 / 15,928 and about 16,430). The SCPH-1001 and 5000 read 16,656 to 17,432.
Evidence: cd-race rows with the INT3 first and the sector already due, 10 to 87 trials per console and speed (e.g. 7501: 65 at 1x, 79 at 2x); runs as finding 11.

**13. The waiting sector is announced in the same pass only if the INT3 is acknowledged within about 2,800 to 3,200 cycles.** The last acknowledge delay with any same-pass announce (INT1 15,000 - 17,000 after the INT3) was 2,800 - 3,200 on every console. At 3,000 or more, 0 to 3% of trials were announced that way (0 to 9 of 100 - 518); later announces came 33k to 72k cycles after the INT3. Acknowledged within 2,000 cycles, Getstat was announced in 50 to 65% of trials on all models; Setloc and Setmode in 71 to 77% on the 1001 and 5000 and 16 to 33% on the others.
Evidence: cd-ack-window, 1001 ×7, 5000 ×7, 5501 ×7 (+3), 7001 ×7, 7501 ×10, 7502 ×2, 9001 ×7 (+3); 8 trials per delay and command.

**14. The buffer holds eight sectors; INT1 shows the oldest first, then jumps to the newest.** This is nocash's queueing test (psx-spx "Response/Data Queueing"), and the consoles agree with his text. With the host not acknowledging for about 5.3 sectors, the second INT1 is the oldest sector (00:02:01 after 00:02:00) in 186 of 186 trials and the third is the newest (00:02:05 in one example), skipping the sectors between. At 10.6 sectors the second INT1 shows header 00:02:09 in 62 of 62 trials: the first slot has been rewritten by sector 9. At 8.6 sectors it did in 55 of 62 (7 still showed sector 1); at 12.6 sectors in 51 of 62 (11 showed the one-slot pattern 00:02:01, 00:02:04, 00:02:05). Open: why those 18 trials differ.
Evidence: cd-queue (cases A, B, C at three delays), 1001 ×7, 5000 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×10 (+3).

**15. GetlocL after a delay reports the newest sector, not the one whose INT1 is pending.** In 186 of 186 trials it returned a header newer than the oldest; in 174 it equalled the sector of the INT1 delivered just before or just after. This matches psx-spx (GetlocL returns the "<newest>" buffered sector).
Evidence: cd-queue case E; runs as finding 14.

**16. A sector read in part and acknowledged with the data request still raised does not lose the waiting sector.** The sector was read for 3 bytes, or 2,060 of 2,340 bytes, and acknowledged. The next INT1 showed the same newest sector as in the case where the first sector was read whole, in 172 of 186 trials (the other 14 differ by timing); never one sector later.
Evidence: cd-queue cases O and P against B; runs as finding 14.

**17. After a long-held INT1 is acknowledged, the next one follows 13,000 to 213,000 cycles later (median 30,000 - 36,000).** An INT1 left unacknowledged for 500 ms, acknowledged, then a Pause at once. The Pause's INT3 came 44,808 to 139,784 cycles after the write and its INT2 2.4M to 3.5M after that. This is nugget's `simpleReadingWithoutAck`.
Evidence: cd-reann, 24 trials per run, 1001 ×7, 5000 ×7, 5501 ×7 (+3), 7001 ×7, 7501 ×10, 7502 ×2, 9001 ×7 (+3).

**18. The interrupt flag can read half-set on the oldest boards.** Polling the flag in a tight loop, the first read was 1 before settling to 3 (and 4 before 5) on the SCPH-1001 (1 of 3,000 Getstats, in its one run) and on the SCPH-5000 (2 of 3,000 Getstats and 1 of 3,000 invalid commands, in its one run). The 5501 (4 runs, both units), 7001 ×2, 7501 ×4, 7502 ×1 and 9001 ×4 saw none in 3,000 + 3,000 each. psx-spx ("Unstable IRQ Flag polling") gives about 0.1% for INT3 and 1% for INT5; the rates here are 0.03 - 0.07%.
Evidence: cd-irq-glitch, runs as above.

## Pause and reads after it

**19. A Pause takes about a turn of the disc, and a turn is longer farther out.** Time from the Pause's INT3 to its INT2, in sectors of the current speed. The count is the same at 1x and 2x, so the time scales with the sector period. Range over 1001, 5501, 7001, 7501, 7502 and 9001:

| Head position | Pause right after a read's first INT1 | Pause later in the read (0, 1/4, 1/2, 3/4 sector after an INT1) |
|---|---|---|
| LBA 16 (near the start) | 5.94 - 5.98 (1x), 5.98 - 6.26 (2x) | 7.4 - 8.0 (7501: 7.75 - 8.44 over the four phases) |
| LBA 130,462 (29:01) | 12.28 - 13.41 (1x), 12.38 - 13.19 (2x) | 13.9 - 14.7 (7501: 14.13 - 14.68) |
| LBA 208,740 (46:23) | 14.96 - 15.89 (1x), 15.38 - 15.74 (2x) | 16.8 - 17.0 (7501: 16.74 - 17.45) |

A Pause right after the first INT1 finishes 1 to 2 sectors sooner than one later in the read, and the answer grows with position. psx-spx lists 4.8 sectors on a PAL PSone (2.17M cycles at 1x, 1.10M at 2x), about one sector under the start-of-disc figures here; it does not say the time grows with position.
Evidence: cd-pause, X-Files runs only: 1001 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×10 (+3); 5 trials per cell per run.

**20. A ReadN after a Pause resumes where nocash's console did, except one row that depends on the start.** nocash's DIAG recorded seven steps: Setloc 00:02:11 + ReadN; a second ReadN while reading; Setloc 00:03:01 memorised; Pause + ReadN; Pause + ReadN with nothing memorised; Setloc changed; two Setlocs. Reading the next two sector frames after each step, six rows agree with his record in all 68 runs: 11,12 / 13,14 / 15,16 / 01,02 / 11,12 / 22,23. The fifth row (Pause, then ReadN with nothing memorised) is his "last sector again", 02,03. The logs read it two ways:
- 02,03, as nocash recorded, in 15 runs: every run of the two earliest sessions, which did not use the runner (5501 ×6, 7501 ×6, 9001 ×3).
- 01,02 (one sector before the last one received) in 53 runs: every run started through the runner, on all seven models (1001, 5000, 5501 both units, 7001, 7501, 7502, 9001).
Open: what in the drive differs between the two kinds of start is not identified. The first command's stat byte (shell-open bit) does not separate the groups.
Evidence: cd-cmds (N rows), 68 runs.

**21. A new read started k sectors ahead of a reading drive arrives in about k sectors, unless the target is the next sector or was just missed.** Reading at 2x, a Setloc to the sector k after the one just delivered, then ReadN: median time to the target's first sector, in 2x sectors. k = 1: 11.4 - 11.8 on every console (a whole turn: the target had passed). k = 2: 1.2 - 1.7. k = 3: 2.3 - 2.6. k = 6: 5.2 - 5.6. k = 12: 11.3 - 11.6. Exceptions: the SCPH-7001 at k = 2 and 3 (12.1 and 13.0) and the SCPH-1001 at k = 2 (12.0) went a turn round.
Evidence: cd-small-reads (AHEAD), 1001 ×7, 5000 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×13; 2 phases per k per run.

## Seeks and the head

**22. A SeekL a few sectors ahead of an idle head waits for the sector: INT2 comes about n - 0.2 sectors after the command.** Head idle at LBA 1,000, 1x, median over runs. n = 1: 0.77 - 0.79 sectors; 2: 1.78 - 1.82; 3: 2.77 - 2.79; 4: 3.70 - 3.84; 5: 4.7 - 4.8; 8: 7.8; 9: 8.8; the same on all seven consoles. At n = 6 the C3 drives (7501, 7502, 9001) answer early, at 3.8; the others at 5.7 - 5.8. At n = 7 the 5000 and 5501 (both units) answer at 7.8, the others at 6.8. From n = 10 the 1001 and 7001 still wait (9.8, and 10.8 at n = 11); the 5000, 5501, 7501, 7502 and 9001 jump back and take 19.3 (n = 10), 20.3 (11) and 21.3 (12) sectors; the 1001 and 7001 also take 21.3 at n = 12.
Evidence: cd-seek-map (1x, from LBA 1,000), 1001 ×7, 5000 ×7, 5501 ×7, 7001 ×7, 7501 ×10, 7502 ×2, 9001 ×10; two seeks per cell per run.

**23. Seek and spin-up times come in steps of one sector and one disc turn.** The same 640-sector seek from LBA 19 completed at 6.0M (51 of 112 trials), 10.3M (31), 5.1M (11), 5.6M (10), 9.9M (4), 2.6 - 2.7M (3) or 6.9M (2) cycles: steps of about one sector (0.45M) and of one turn at that radius (4.3M). Long seeks repeat the same way: three identical 00:02 to 30:00 seeks in one 7501 run took 20.3M, 34.8M and 20.4M cycles.
Evidence: cd-seek-path6 (control column), 8 runs each on 1001, 5501 (both units), 7001, 7501, 7502, 9001 (the 5000 left out); cd-sled.

**24. The idle head circles a short stretch and jumps back, on every console.** After a seek, Init or Pause the drive keeps re-reading the same few sectors: GetlocP (read every 20 ms) climbs, then falls back.
- After Init: climbs 00:01:66/67 to 00:01:74 (sometimes 00:02:00), 6 - 7 reads a lap, about 8 sectors.
- After SeekL 00:02:16: climbs 00:02:02/03 to 00:02:18, 11 - 12 reads, about 16 sectors, with one read at 00:02:10 between laps.
- After SeekL 00:30:00: climbs 29:59:63/64 to 30:00:02, 10 - 11 reads, about 13 sectors.
- After a read to 00:02:31 and a Pause: climbs 00:02:24/25 to 00:02:32, 6 - 7 reads (3 - 4 at 2x), about 8 sectors.
The SCPH-5000's laps start 1 sector later (its disc). Open: why the lap after SeekL 00:02:16 is two turns long where the others are one.
Evidence: cd-head (IDLE), 1001 ×7, 5000 ×7, 5501 ×10 (+3), 7001 ×7, 7501 ×13, 7502 ×2, 9001 ×10 (+3).

**25. Where the first jump of a medium seek lands depends on the unit and on the seek before it.** A 640-sector SeekL from LBA 19 with GetlocP polled: after about 1.3M cycles the first sector shown is not the target. That sector relative to the target, in sectors, median (range), when the previous seek was far (from 44:26) or near (19 to 639):

| Console | after a far seek | after a near seek |
|---|---|---|
| 1001 | -21 (-68 to +17) | -30 (-40 to -30) |
| 5501 unit 1 | +10 (-30 to +56) | +8 (-40 to +46) |
| 5501 unit 2 | +46 (+27 to +74) | +27 (+17 to +27) |
| 7001 | +103 (+36 to +132) | +36 (-2 to +46) |
| 7501 | -2 (-57 to +17) | -66 (-67 to -48) |
| 7502 | -59 (-78 to -49) | -59 (-77 to -30) |
| 9001 | +10 (-49 to +36) | -10 (-30 to +9) |

No single value fits all units, and on the 7501, 7001 and 5501 unit 2 the previous seek moves it. Open: the cause (a lens position in the sled that depends on the last sled move is a hypothesis, not a measurement).
Evidence: cd-seek-path6 (1x, wait 0), 8 runs each; the SCPH-5000 (different disc) is left out.

**26. Long seeks take 0.3 to 1.4 s and repeat only to whole turns.** SeekL from 00:02 to a position and back, single speed. Median seconds, out / in:

| Console | 05:00 | 30:00 | 55:00 |
|---|---|---|---|
| 1001 (9 each) | 0.34 / 0.29 | 0.82 / 0.77 | 1.01 / 1.05 |
| 5000 (6) | 0.53 / 0.34 | 0.78 / 0.89 | 1.32 / 1.06 |
| 5501 unit 2 (9) | 0.36 / 0.40 | 0.60 / 0.77 | 1.04 / 1.07 |
| 7001 (6) | 0.32 / 0.40 | 0.61 / 0.88 | 1.06 / 1.17 |
| 7501 (21) | 0.34 / 0.39 | 0.63 / 0.74 | 1.31 / 0.88 |
| 7502 (21) | 0.34 / 0.40 | 0.62 / 0.61 | 0.86 / 1.00 |
| 9001 (21) | 0.33 / 0.40 | 0.62 / 0.64 | 1.07 / 0.91 |

The spread in one cell is large (7501, 55:00 out: 0.79 to 1.32 s). The 5501 unit 1 added outliers from 0.00 to 2.69 s.
Evidence: cd-sled, 1001 ×3, 5000 ×2, 5501 ×3 (+3), 7001 ×2, 7501 ×7, 7502 ×7, 9001 ×7; 3 seeks per cell per run.

**27. ReadTOC takes 0.6 s from the disc start and up to 8 s from the outer edge, and leaves the head in the lead-in.** INT2 after INT3, seconds (fastest - slowest, mean):

| Start position | 1001 | 5501 | 7501 | 9001 |
|---|---|---|---|---|
| LBA 0 | 0.61 - 1.08 (0.81) | 0.60 - 1.35 (0.80) | 0.60 - 1.35 (0.88) | 0.60 - 1.09 (0.84) |
| 65,231 (25%) | 2.28 - 2.93 (2.53) | 2.38 - 5.72 (3.51) | 1.89 - 2.55 (2.17) | 2.19 - 2.97 (2.50) |
| 130,462 (50%) | 3.39 - 4.11 (3.79) | 3.98 - 7.25 (4.88) | 3.02 - 3.96 (3.37) | 3.23 - 4.44 (3.73) |
| 234,832 (90%) | 4.99 - 5.63 (5.30) | 5.95 - 8.08 (6.83) | 4.38 - 5.61 (4.75) | 4.86 - 5.89 (5.30) |
| again, from the lead-in | 0.62 - 1.00 | 0.62 - 1.00 | 0.64 - 1.14 | 0.62 - 1.25 |

GetlocP afterwards read 00:01:72 to 00:01:74 in every row. psx-spx says "about 1 second", which holds only from the inner area. The 5501 column mixes both units; unit 1 is the slow one.
Evidence: cd-readtoc (X-Files), 1001 ×7, 5501 ×13, 7501 ×16, 9001 ×13; two per cell per run. The 7001 and 5000 ran it too (not tabulated).

**28. The lead-out of a pressed disc is readable far past the end the table of contents gives.** On the X-Files disc (table of contents ends 57:59), SeekP to 58:29:00 up to 72:59:00 completed with INT2 in 9 of 9 steps on every console (0.72 to 1.57 s each), and GetlocP reported track AAh, index 01, with a relative time counting from about 57:59. SeekL to 60:29:00 completed in 0.85 - 1.11 s and GetlocL then returned a mode-2 header (60:28:73). On a mixed disc (Moto Racer World Tour, 23 tracks, lead-out 62:56), SeekP into the lead-out also completed (1.0 - 1.6 s) but SeekL past the end or into the lead-out failed after 8.4 - 8.6 s with INT5 (error 04h) on the 1001, 5501, 7501 and 9001 (one run each, all alike). No seek was tried past 74:00.
Evidence: cd-leadout and cd-pastend; X-Files: 1001 ×1, 5501 ×2 (+1 unit 2), 7001 ×1, 7501 ×2, 7502 ×1, 9001 ×2; Moto Racer: 4 consoles ×1.

**29. GetlocL during a SeekL is answered by some units and refused by others.** Written 0.1M, 0.5M or 2M cycles into a SeekL: INT3 with the header of the position the head left (e.g. 29:01:36), or INT5 (stat 43h, error 80h). INT3: 1001 18 of 18, 7501 42 of 42, 9001 36 of 42, 5501 unit 1 12 of 18, 7001 6 of 18. INT5 every time: 5000 (12 of 12), 7502 (42 of 42), 5501 unit 2 (18 of 18). During a SeekP every console refused it. psx-spx says GetlocL "fails (error 80h)" in the seek phase: true for SeekP and for some units' SeekL, not for the 1001, 7501 or 9001. Open: what separates the units (the 5000 and 7502 also ran other discs).
Evidence: cd-getlocl-seek, 1001 ×3, 5000 ×2, 5501 ×3 (+3), 7001 ×3, 7501 ×7, 7502 ×7, 9001 ×7; 6 SeekL cells per run.

## Spin-up, Stop and Init

**30. Spin-up time differs by unit, from 1.2 s to 2.2 s.** MotorOn from a stopped disc, to its INT2; 24 spin-ups per run. Stop on a spinning disc is the spin-down.

| Console | Spin-up mean ± sd (fastest - slowest), s | Stop from idle 1x, s | Stop from idle 2x, s | Init from a 1x stop, s | Init from a 2x stop, s |
|---|---|---|---|---|---|
| SCPH-1001 | 1.245 ± 0.062 (1.12 - 1.45) | 0.44 | 0.82 | 1.22 | 2.25 |
| SCPH-5000 | 1.547 ± 0.084 (1.46 - 1.82) | 0.44 | 0.83 | 1.59 | 2.72 |
| SCPH-5501 unit 1 | 2.227 ± 0.120 (2.01 - 2.60) | 0.43 | 0.79 | 2.18 | 2.16 |
| SCPH-5501 unit 2 | 2.200 ± 0.108 (1.99 - 2.39) | 0.43 | 0.78 | 2.05 | 2.24 |
| SCPH-7001 | 2.028 ± 0.092 (1.84 - 2.24) | 0.40 | 0.74 | 1.92 | 2.05 |
| SCPH-7501 | 2.156 ± 0.087 (1.97 - 2.39) | 0.49 | 0.94 | 2.11 | 2.13 |
| SCPH-7502 | 2.048 ± 0.117 (1.83 - 2.27) | 0.37 | 0.71 | 1.92 | 1.93 |
| SCPH-9001 | 2.009 ± 0.122 (1.66 - 2.27) | 0.39 | 0.72 | 1.99 | 2.01 |

In 7 of 9 sessions both later runs averaged 0.2 to 4.5% above the first run; in the other two they straddled it. The Stop and Init columns are medians of 4 to 18 trials from cd-init.
Evidence: cd-spinup-trend, 3 runs each (7501 ×5) of 24 spin-ups; cd-init (Stop, Init after Stop), 1001 ×4, 5000 ×6, 5501 ×6 (+2), 7001 ×4, 7501 ×8, 7502 ×4, 9001 ×6 (+2).

**31. After a Stop made at double speed, the SCPH-1001 and 5000 take about 1.8 times as long to spin up again.** Init from a stop made at 2x: 2.25 s (1001) and 2.72 s (5000), against 1.22 s and 1.59 s from a stop made at 1x. The other units differ between the two by -1% to +9%. Stop itself takes 0.37 - 0.49 s from idle at 1x and 0.71 - 0.94 s at 2x on all units.
Evidence: cd-init (STOPPED, STOPPED AT DOUBLE SPEED); runs as finding 30.

**32. Spin-up times fall in clusters one disc turn apart.** The SCPH-9001's 72 spin-ups: 20 within 63.0 - 64.5M cycles, 23 within 67.0 - 69.5M, 17 within 71.5 - 73.0M, 12 elsewhere (56.1 - 77M). The clusters sit about 4.1 - 4.3M apart (0.12 - 0.13 s), one turn near the hub. The 7501's 120 spin-ups show peaks near 70.5M, 74.5M and 78.5M. A spin-up is a base plus a whole number of turns.
Evidence: cd-spinup-trend; 9001 ×3, 7501 ×5.

**33. Init takes about the same time from every state on a spinning disc, longer after reading, and the spin-up time after a Stop.**

| State when Init is written | Init INT3 (mean) | INT2 after the INT3 |
|---|---|---|
| spinning, straight after another Init | 70,560 - 77,174 | 4.15M - 4.24M (about 124 ms) on all units |
| after reading 8 sectors | 79,080 - 86,130 | 4.58M (5000), 5.14M - 5.63M (others) |
| after a Stop (either speed) | 92,593 - 104,201 | the spin-up of finding 30 |

The fastest INT3 per unit was 60,271 to 64,356 cycles. psx-spx lists Init's first response as 81,102 mean, 63,520 fastest. Each state: 12 to 96 trials per unit.
Evidence: cd-init (BACK TO BACK, READING, STOPPED), runs as finding 30.

**34. Init after one second idle takes either 5 - 9 ms or 110 - 160 ms, depending on the unit.** Fast = 172k - 297k cycles, slow = 3.6M - 5.5M. Fast in every trial: 7501 (48 of 48), 9001 (48 of 48), 7502 (24 of 24), 5000 (36 of 36). The 1001: 19 of 24. The 5501 unit 1: 10 of 36. The 7001: 4 of 24. The 5501 unit 2: 0 of 12. Open: what puts the head where an Init is slow.
Evidence: cd-init (SETTLED), runs as finding 30.

## Speed changes

**35. A head move written soon after Setmode changes speed completes at a fixed time after the Setmode's INT3, not after its usual time.** A SeekL (or ReadN, to its first INT1) written 50 to 300 ms after the Setmode's INT3 completed at these medians, in seconds after the Setmode's INT3 (seek / read):

| Console | 1x to 2x | 2x to 1x |
|---|---|---|
| 1001 | 0.72 / 0.79 | 0.67 / 0.68 |
| 5000 | 0.68 / 0.68 | 0.76 / 0.79 |
| 5501 unit 1 | 0.80 / 0.80 | 0.78 / 0.80 |
| 5501 unit 2 | 0.68 / 0.73 | 0.54 / 0.55 |
| 7001 | 0.63 / 0.65 | 0.54 / 0.55 |
| 7501 | 0.81 / 0.82 | 0.62 / 0.63 |
| 7502 | 0.58 / 0.60 | 0.54 / 0.56 |
| 9001 | 0.70 / 0.71 | 0.55 / 0.56 |

On the 7501 (1x to 2x) the completion stays at 0.76 - 0.81 s for delays of 50 to 600 ms, then follows the delay: 0.87 s at 700 ms, 1.07 s at 1 s, 1.57 s at 1.5 s. Getstat was answered 24k to 270k cycles after its write throughout the first second after every Setmode (stat 02), so commands are not held off; two 5501 unit 1 runs read stat 00 (the motor was already off). Open: how much of the delay is the spindle changing speed and how much is the move's own seek.
Evidence: cd-speed, 1001 ×4, 5000 ×6, 5501 ×6 (+2), 7001 ×4, 7501 ×8, 7502 ×4, 9001 ×6 (+2); 2 trials per delay per run.

## Lid and focus

**36. Lid open and shut: the motor stops within half a second, and the drive is ready 6.4 to 6.6 s after the lid shuts.** SCPH-7501, GetlocP polled every 10 ms. On opening: INT5 stat 13h, error 80h at once, stat 11h (motor off) from 0.474 - 0.496 s. After shutting: INT5 stat 11h for 1.636 - 1.651 s, then INT5 stat 13h (motor on, disc not yet read) until GetlocP answered INT3 at 6.368, 6.417 and 6.582 s. ReadN gave 11h / 40h, then 13h / 40h, and its INT3 at 6.434 - 6.555 s. Opening the lid also raises one unasked INT5 (stat 16h; psx-spx lists its second byte as 08h).
Evidence: cd-lid, 7501 ×3 (two lid cycles per run).

**37. After the lid shuts, a focus search runs from about 2.2 s to 4.4 - 5.9 s, and the disc is ready 6.1 to 7.7 s.** Seen by polling the drive's RAM (Test 19h,60h) for the search flag. The window and the first GetlocP position answer, in seconds after the shut:

| Console | Test | Search starts | Search ends | First position answer |
|---|---|---|---|---|
| 7501 | cd-lid-sweep (×3) | 2.22 - 2.24 | 5.28 - 5.32 | 7.03 - 7.13 |
| 7501 | cd-lid-sweep2 (×3) | 2.26 - 2.28 | 5.90 - 5.92 | 7.63 - 7.75 |
| 7501 | cd-lid-sweep3 (×3) | 2.26 - 2.28 | 5.66 - 5.90 | 7.31 - 7.70 |
| 7001 | cd-lid-sweep3 (×3) | 2.18 | 4.82 - 4.84 | 6.46 - 6.59 |
| 7001 | cd-lid-sweep4 (×3) | 2.14 - 2.36 | 4.38 - 4.50 | 6.10 - 6.34 |
| 9001 | cd-lid-sweep3 (×3) | 2.12 | 5.06 - 5.24 | 6.73 - 7.07 |

The same 7501 reads 3.1 to 3.6 s for the search depending on how much RAM the test reads while it waits, and the plain `cd-lid` poll (finding 36) reads 6.4 - 6.6 s: the polling itself moves the figures by up to a second.

**38. A European console reading an American disc took 14 to 15 s to answer.** SCPH-7502, cd-lid-sweep3 ×3: search from 2.16 s to 4.94 - 5.88 s, first position answer at 14.283, 15.190 and 15.373 s, against 6.1 - 7.7 s on the others. Open: the cause. The unlock commands had been sent; a licence or region re-check of the American disc is a guess.
Evidence: cd-lid-sweep3, 7502 ×3.

## SCEx counters and the drive's test commands

**39. SCEx strings are counted only with the head at the very start of the disc.** Test 19h,04h starts receiving SCEx; 2 s later Test 19h,05h returns (total, success). Success above zero with the head parked at 00:02:00: every run on the 7501 (17), 9001 (13), 1001 (11), 5501 (12 + 3) and 7001 (7). At 00:00:10: 15 of 17 (7501), 13 of 13 (9001), 10 of 11 (1001), 11 of 12 (5501 unit 1), 3 of 3 (unit 2), 7 of 7 (7001). From 00:03:00 on (00:03, 00:04, 00:06, 00:10, 00:30, 01:00, 05:00, 20:00, 40:00) the success count was 0 in every run on every console, except 6 of 13 runs on the 9001 at 40:00:00, which began while the drive was still seeking (stat 42h) and counted 5 - 7 strings, all or most matching. psx-spx says the strings exist "only in the Lead-In area"; here they are found with the head at 00:02:00, the first minute of the data area, and not at 00:03:00.
Evidence: cd-scex, 1001 ×11, 5000 ×7, 5501 ×12 (+3), 7001 ×7, 7501 ×17, 7502 ×2, 9001 ×10 (+3); 11 head positions per run.

**40. Test 19h,04h / 05h behave two ways, by drive family.**
- 7501, 7502, 9001 (C3): the totals stay small and stop growing. At 00:00:10: total 6 - 7, success 3 - 5. At 00:02:00: 3 / 2 (7501 13 of 17; 9001 3 / 2 in 9 and 3 / 1 in 4 of 13). Waiting longer changes nothing: after 0.5 s 1 / 1 (7501 17 of 17); after 8 s 3 / 2 (7501 14 of 17). Test 19h,04h does not itself produce INT1s: sectors follow Test 05's INT3 (1,000 counted, in all 31 runs of cd-cmds).
- 1001, 5000, 5501, 7001 (C1, C2): Test 19h,04h also starts a data read. INT1s (stat 22h, first header 04:06) arrive before Test 05's INT3 in every run (7 + 7 + 16 + 7). The total keeps counting wherever the head is (at 00:06:00 up to 0Dh) while successes stop at 00:03:00. After 8 s at 00:02:00: totals 4 - 45 (04h - 2Dh), successes 1 - 4 on the 1001, 5501 and 7001.
Evidence: cd-cmds (row S2) and cd-scex (T rows), runs as finding 39.

**41. A European drive counts the strings of an American disc but matches none.** SCPH-7502: the totals appear as on the 7501 (6 at 00:00:10 in one of two runs, 3 at 00:02:00 in both, none from 00:03:00), and success is 0 at every position in both runs. The 5000 reading its own Japanese disc also never counted a success in 7 runs. Open: the 5000's zero.
Evidence: cd-scex, 7502 ×2, 5000 ×7.

**42. The drive program's tick counter runs at 840 to 910 a second.** Read with Test 19h,60h against the console's timer. Idle: 886.8 (5000), 894.6 (7501), 895.0 (7502), 895.1 (9001), 897.6 (1001), 904.1 (7001), 906.5 / 910.7 (5501 units 1 / 2). Reading: 839.4 (5000), 853.8 (1001), 864.7 / 864.4 (5501), 866.5 (7502), 867.9 - 868.0 (7001, 7501, 9001). Reading costs the program 3 to 5% of its ticks on every unit.
Evidence: cd-hc05-tick, 1001 ×3, 5000 ×2, 5501 ×4 (+3), 7001 ×4, 7501 ×5, 7502 ×4, 9001 ×3; 5 s idle then 5 s reading per run.

## Per-console summary

| Console | Family | Spin-up | Waits 5% longer | Half-set flag | INT1s come before Test 05's INT3 | GetlocL answered in a SeekL | SCEx successes |
|---|---|---|---|---|---|---|---|
| SCPH-1001 | C1 | 1.25 s | yes | seen | yes | yes | at the start only |
| SCPH-5000 | C1 | 1.55 s | yes | seen | yes | no | none (Japanese disc) |
| SCPH-5501 | C2 | 2.2 s | no | none | yes | unit 1 mostly yes, unit 2 no | at the start only |
| SCPH-7001 | C2 | 2.03 s | no | none | yes | mostly no | at the start only |
| SCPH-7501 | C3 | 2.16 s | no | none | no | yes | at the start only |
| SCPH-7502 | C3 | 2.05 s | no | none | no | no | none (American disc) |
| SCPH-9001 | C3 | 2.01 s | no | none | no | yes | at the start only |
