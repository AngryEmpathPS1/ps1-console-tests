# GPU, video timing and MDEC

What the GPU, the video output and the MDEC do on real PlayStations, read from the programs in `hwtests/` and the logs in `consoles/`. Every number below is read from a log or computed from logs as stated.

## Reading the numbers

- **Cycles** are CPU cycles at 33.8688 MHz. A GPU **tick** is one cycle of the video clock: 1.585 ticks per CPU cycle on SCPH-7001, 7501 and 9001.
- **Old GPU** means the SCPH-1001, one console, BIOS 2.1. **New GPU** means every other model tested: SCPH-5000 (one unit), 5501 (two units), 7001, 7501, 7502 (PAL; one run of most tests), 9001 (two units). Everything said about the old GPU rests on that one console.
- **D** (the `gpu-shape-time` programs): the probe stores a command's last word, waits D cycles in a calibrated loop, then reads GPUSTAT once. D is binary-searched for the first value at which the bit reads set, at eight start points an eighth of a line apart in the vertical blanking. The real latency is D plus a small fixed offset of the probe, so differences between rows are exact. The fastest start point is quoted.
- **k** (`gpu-cmd-ready`, `gpu-a0-ready`, `gpu-c0-*`): the number of NOPs between the last store and one GPUSTAT read. The read lands about k+2 cycles after the store.
- **GPUSTAT bits** (psx-spx names): 26 ready for a command word, 27 ready to send VRAM to the CPU, 28 ready for a DMA block. In the timing programs, bit 26 first reading set means the drawing is finished; bit 28 first reading set means the command words have left the FIFO (for a polygon, its set-up is finished).
- Logs are named `<test>-<model>[-n].txt`. "Evidence" lists models with the number of logs in brackets. Programs that poll GPUSTAT (`gpu-cost-sweep`, `gpu-thin`, `gpu-scanout`) resolve about 45 cycles.

## The two GPUs

**1. GP1(10h) index 07h tells the two apart.** It returns 00000002h on every model except the SCPH-1001, where GPUREAD keeps its previous value (no answer). The model number alone does not decide it: psx-spx's dev text lists an SCPH-1001 with the new GPU, and this one has the old. Open: other early boards (PU-7, early PU-8) were not available; whether they match this SCPH-1001 is not measured.
Evidence: machine-id (70 logs, all seven models), gpu-version (all seven models).

**2. Register-visible differences.** Identical within each GPU type across all logs, apart from the drawing-speed rows and one row on the SCPH-1001: the X-flip rectangle reads 31 31 0 0 0 ... in five of its six logs and 31 31 0 31 31 31 0 31 ... in the first. Otherwise only GPUSTAT bit 31 at entry, a beam position (one SCPH-5000 log) and the 7502's PAL bit differ.

| Probe | SCPH-1001 | Every other model |
|---|---|---|
| GP1(10h) index 0Ah-0Dh, 1Ah, 1Bh, 1Dh | mirror 02h-05h (0Ah returns the 02h value, 0Bh the 03h value, ...) | leave GPUREAD unchanged |
| Draw-area latch read back after E3h/E4h = 3FFh, 3FFh | 8007FFFFh (Y field 9 bits) | 800FFFFFh (10 bits) |
| Rectangle drawn with the area at Y 1008-1023 | nothing drawn | drawn in VRAM row 496 (Y wraps at 512) |
| GPUSTAT bit 13, interlace off | 0 | 1 |
| GPUSTAT bit 14 after GP1(08h) bit 7 | 0 | 1 |
| GPUSTAT bit 15 after GP1(09h)=1 and E1h.11 | 0 | 1 (stays 1 after GP1(09h)=0) |
| Texture modulation, colour 87h (about 1.05x), 5-bit red texels 0..31 | 0..31 unchanged | 0..18, 20..31, 31 (value 19 never appears, 31 twice) |
| Raw rectangle from u = 31 with E1h bit 12 (X flip) | 31 31 0 0 0 ... (no flip) | 31 30 29 ... 0 |
| Fill at X = 38Fh (low four bits 0Fh), 32 wide | 8 odd pixels of 896-911, all of 912-927, 8 even pixels of 928-943 | 896-927 (low four bits ignored) |
| DMA upload 64x4 in blocks of 8, 16, 32, 64 words | no halfword lost | no halfword lost |
| Drawing speed | see findings 21-25 and 27 | |

Disagrees with psx-spx's list of old-GPU differences in one place: it says DMA to VRAM fails above 10h-word blocks on the old GPU; no halfword was lost at 8, 16, 32 or 64 words, and 32- and 64-word block uploads complete in normal time (finding 19).
Evidence: gpu-version; 1001 (6), others (37). Old-GPU DMA rows also in gpu-dmabs2; 1001 (2).

## Video timing

**3. Line and frame lengths, NTSC mode (320x240).**

| Model | CPU cycles per line | Cycles per 263-line frame | Frame rate |
|---|---|---|---|
| 7001, 7501, 9001 | 2,152.50 in almost every run (range 2,152.45-2,152.50) | 566,108.23 in all 22 logs | 59.827 Hz |
| 5501 | 2,152.55 (second unit 2,152.52) | 566,122.9-566,123.1 (second unit 566,115.7) | 59.826 Hz |
| 1001 | 2,152.54 | 566,120.8-566,122.1 | 59.826 Hz |
| 5000 | 2,152.52-2,152.53 | not run | |
| 7502, NTSC mode | 2,172.3 (frame / 263) | 571,317.65 (one run) | 59.28 Hz |

The frame length of the 7001, 7501 and 9001 repeats to the hundredth in every run, consistent with a video clock derived from the CPU oscillator by a fixed ratio: 3,412.5 video ticks (the NTSC standard, 227.5 subcarrier periods x 15) in 2,152.5 cycles is 65/41 ticks per cycle. The 5501, 1001 and 5000 sit about 10-25 ppm away and differ between units and runs, as two separate crystals do.
Evidence: line-split, 1001 (6), 5000 (7), 5501 (9), 7001 (6), 7501 (10), 7502 (1), 9001 (10); pal-line, 1001 (6), 5501 (7), 7001 (6), 7501 (8), 7502 (1), 9001 (8).

**4. Every NTSC line is the same length; there is no 3412/3413 alternation.** Where a run averages something other than a whole half cycle (5501, 5000, 1001, and the 7001/7501/9001 runs at 2,152.45-2,152.49) the per-line means alternate by at most 2.3 hundredths of a cycle, except one SCPH-7501 run (2,152.49) at 6.8. Where a run averages exactly 2,152.50 the probe, which counts whole cycles, sees lines of 2,152 and 2,153 alternately and reads +-0.51 cycle (19 of 20 such logs; one SCPH-7501 log reads +-0.03). A 3412/3413-tick toggle would read +-0.32 in every run.
Evidence: line-split (logs as finding 3).

**5. PAL mode.** The PAL/NTSC frame ratio is 1.191290-1.191293 on all 36 pal-line logs, i.e. 3,405-tick PAL lines (314 lines) against 3,412.5-tick NTSC lines (263 lines). What changes between models is the clock.

| Model | PAL 320x240 frame (cycles) | Rate | Cycles per line |
|---|---|---|---|
| 7001, 7501, 9001 | 674,399.18 in all 22 logs | 50.22 Hz | 2,147.8 |
| 5501 | 674,417.2-674,418.3 (second unit 674,408.6) | 50.22 Hz | |
| 1001 | 674,416.0-674,417.0 | 50.22 Hz | 2,147.8 |
| 7502 | 680,605.45 (one run) | 49.76 Hz | 2,167.5 |

An NTSC-region console in PAL mode therefore runs at 50.22 Hz, not 49.76 Hz: which fits it having no PAL crystal and using the NTSC one. The 7502 uses one video clock for both modes: 3,405 x 314 ticks in 680,605.45 cycles and 3,412.5 x 263 in 571,317.65 are 1.57091 ticks per cycle to 1 ppm (432/275). Open: one 7502 unit, one run.
Evidence: pal-line (logs as finding 3); line-split on the 7502 booted in PAL mode read 2,167.52 cycles per line (1 run).

**6. 480i frames are two fields of different length.** Fields alternate 263 and 262 lines in NTSC (SCPH-7501 frame-pair mean 565,030.9 cycles; fields about 566,1xx and 563,9xx) and 313 and 312 in PAL (7501 in PAL: mean 671,178.3; fields about 672,2xx and 670,1xx).
Evidence: pal-line; 1001 (6), 5501 (7), 7001 (6), 7501 (8), 7502 (1), 9001 (8).

**7. SCPH-5000: PAL mode stops the video clock; drawing carries on.** In PAL mode, at every width, Timer 1 does not step (0 in 3,000 polls against 18 in NTSC), no VBlank arrives in 1.5 million polls (about 29 NTSC frames: an NTSC VBlank took 51,461 polls in the same run), and "lines a frame" reads 0; GPUSTAT bit 20 is set. A 64x64 fill still finishes (50 polls against 47 in NTSC), GP0(E1h) still executes and GP1(10h) index 07h still answers. psx-spx's PAL/NTSC modification note says boards may have the unused oscillator's pad grounded. Open: one SCPH-5000; the second unit was not tested, and `pal-dots` froze this console twice with no log.
Evidence: pal-probe, pal-probe2; 5000 (1 each).

**8. Timer 0 dots per line.** Averaged over 64 lines, identical on all models:

| Width | 256 | 320 | 368 | 512 | 640 |
|---|---|---|---|---|---|
| NTSC dots per line | 340.95-340.98 | 426.00 | 487.00 | 682.00 | 853.00 |
| PAL dots per line | 340.00 | 426.00 | 486.00 | 681.00 | 851.00 |

Each is the line's ticks divided by the dot divider (10, 8, 7, 5, 4) rounded down, from a restart on every line, except PAL 320: 3,405 / 8 = 425.6 reads 426. Agrees with psx-spx (nocash's DIAG reading for PAL 320: 426). Single lines swing by up to 4 dots with the polling loop; use totals.
Evidence: pal-dots; 1001 (6), 5501 (9), 7001 (6), 7501 (10), 7502 (1), 9001 (10).

**9. GPUSTAT bit 31.** In 240-line mode it flips every scanline and equals (display VRAM Y + line) & 1; the sense reverses with the parity of Y. In vertical blanking it is Y's low bit (0 for even Y, 1 for odd, in all 4 Y values tested): psx-spx says it is always zero there, in all three texts. In 480i it alternates each frame and equals bit 13 for odd Y and its inverse for even Y (on the old GPU bit 13 stays 0 and bit 31 alternates the same way). Timer 1's hblank step leads the flip by 5.8% of a line in the first sessions (5501, 7501, 9001) and 8.7% in every later session on all models; the 7502 shows the complement.
Evidence: gpustat31; 1001 (6), 5000 (7), 5501 (9), 7001 (6), 7501 (10), 7502 (1), 9001 (10).

**48. GP1(00h) restarts the video timing, and the horizontal with it.** Written in the vertical blanking (0-23 lines after the VBlank edge) the next edge comes 553,19x cycles later, 257 lines, and the one after 1,119,3xx, 520 lines. Written in the picture (24-262 lines after the edge) the VBlank interrupt arrives at once (64 cycles, the poll) and the next edge 553,2xx cycles later, 257 lines. GPUSTAT.31 reads 0 after the write and first flips about 38,7xx cycles later, 18 lines. New beside psx-spx's lines: the next Timer 1 hblank tick is about 1,988-2,013 cycles after the write at any point of the line. All of it fits the beam jumping to the start of the line before line 0.
Evidence: gp1-reset; SCPH-5501 unit 2 only (3 runs, 54 rows alike).

## GPUSTAT and command timing

**10. Small commands from the CPU: when bits 28 and 26 return** (first k at which the bit reads set). Identical in every run except the textured rectangle's bit 26.

| Command (words) | Bit 28 | Bit 26, new GPU | Bit 26, SCPH-1001 |
|---|---|---|---|
| Fill 16x16 (3) | 13 | 120 | 136 |
| Flat rectangle 16x16 (3) | 13 | 184 | 224 |
| Flat 1x1 rectangle (2) | 13 | 20 | 20 |
| Textured rectangle 16x16, 4-bit (4) | 7 once, then set from 12 | 152 or 192 (differs by session on one console) | 232 |
| Flat line, 16 across (3) | 21 | 56 | 44 |
| Flat triangle, legs of 8 (4) | 31 | 88 | 88 |

The 7502's one run reads 208 for the rectangle and 192 for the textured one.
Evidence: gpu-cmd-ready; 1001 (1), 5000 (6), 5501 (7), 7001 (6), 7501 (4), 7502 (1), 9001 (4).

**11. A polygon holds bit 28 clear until its set-up is done, and the set-up time is a formula of the first edge.** For a flat triangle, bit 28 returns after 24 + max(2, floor(log2 |dx0|)) + floor(log2 |dy0|) cycles, where (dx0, dy0) is vertex 1 minus vertex 0 as sent (log2 of 0 counted as 0). It is exact on all 1,134 triangle rows of `gpu-shape-time5` (42 shapes x 27 logs, all seven models, fastest and slowest of eight starts alike). Examples: legs of 4, 8, 16, 32, 64 with the first edge along X: 26, 27, 28, 29, 30; a 320x16 triangle whose first edge runs (320,0) to (0,16): 36; (0,0) to (0,16): 30. The 42 shapes hold 29 first-edge vectors, so floor and nearest log2 are not told apart. The same value is read on the SCPH-1001, which suggests set-up belongs to the command front end, not the pixel engine. Gouraud, textured and gouraud-textured triangles add a base and a term in the first interpolated attribute's differences:

| Kind (16x16 triangle) | Bit 28 |
|---|---|
| Flat | 28 |
| Gouraud, equal colours | 159 |
| Gouraud, red differing by 2 / 8 / 32 / 64 / 128 at vertex 1 | 159 / 160 / 162 / 163 / 164 |
| Gouraud, red differing by 255 at vertex 2 / at both | 167 / 172 |
| Gouraud, green or blue differing by 255 | 159 (no effect) |
| Textured, U span 0 / 15 / 64 / 255 | 115 / 116 / 119 / 120 |
| Gouraud-textured | 247-258 |
| Semi-transparency | adds nothing |

After the set-up, gouraud, 15-bit textured, 4-bit textured (texels cached) and gouraud-textured triangles of one size take the same time to finish: 63, 179, 603, 2,219 cycles for legs of 8, 16, 32, 64.
Evidence: gpu-shape-time3, -time5, -time7, -time8; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2). Fastest values identical on all six new-GPU models; the slowest column varies.

**12. A GP0(A0h) upload's words are taken late.** After A0h, position, size and one data word stored back to back, GPUSTAT reads bits 26/27/28 = 000 until k = 6, when bit 28 returns (k = 5 after the three header words alone). Bit 26 returns at k = 16 after a 2x1 upload on the new GPU and at k = 11 on the SCPH-1001.
Evidence: gpu-a0-ready; 1001 (6), 5000 (6), 5501 (9), 7001 (6), 7501 (10), 7502 (1), 9001 (10).

**13. GPUSTAT.27 after GP0(C0h) rises later for a bigger copy.** First k with bit 27 set after the size word:

| Copy | 2x1 | 4x1 | 8x1 | 16x1 | 32x1 or larger (16 words or more) | 2x2 |
|---|---|---|---|---|---|---|
| New GPU | 20 (20-22) | 21 (21-22) | 23 | 27 | 32 | 25 |
| SCPH-1001 | 16 | 18 | 22 | 31 | 32 (33 at 64x1 in both logs) | 24 |

That is about one extra cycle per word fetched past the first, up to 12 words, and 4 more for a second row, on the new GPU (two cycles a word on the old); the starting X (0, 8, 14) changes nothing by more than 1. The rise is a fixed time after the GPU takes the size word: with GP0(01h), C0h, position and size back to back it comes at k = 20, without the GP0(01h) at 19, and at 17 when the four stores are spaced by RAM loads, 4 NOPs or 13 NOPs (18 in 4 of 32 logs at 13); with 8 NOPs it reads 10-11 in all 32 new-GPU logs, which the test source puts down to the binary's instruction-cache layout. Open: inside the set run the bit occasionally reads clear again at scattered k.
Evidence: gpu-c0-size; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (4), 7502 (1), 9001 (4). gpu-c0-gaps; 1001 (6), 5000 (6), 5501 (7), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

**14. A GPUREAD before the transfer is ready seems to take nothing.** Reading GPUREAD a few cycles after a 2x1 GP0(C0h) leaves the transfer open: bit 27 reads set with bits 26 and 28 clear (GPUSTAT 0802220Ah) at every k from 0 to 63, and the commands that follow wait behind it. This is inferred from the pattern of the logs (the early read's value was not printed). Open.
Evidence: gpu-c0-ready; 1001 (1), 5000 (7), 5501 (7), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

**15. What GP1(10h) leaves in GPUREAD, new GPU.** An unused index (00h, 06h, 0Fh) keeps the whole previous word; 02h-04h keep bits 20-31 of the previous word (FFFFFFFFh becomes FFF12345h), 05h keeps bits 22-31 (9234D678h becomes 92022923h); 07h replaces the word (2); 08h replaces it with 0. On the SCPH-1001 08h and 07h leave the word alone. Byte-identical on all 33 new-GPU logs. Agrees with nocash's DIAG; amidog's psxtest_gpu model (an unused index keeps only bit 31) does not fit.
Evidence: gpuinfo-latch; 1001 (1), 5000 (7), 5501 (7), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

**16. GP1(01h) stops a drawing part-way.** Written N cycles after a primitive's last word, it halts the draw where it is: pixels already written stay and the rest is never drawn. A 256x256 fill cut at N = 1,024 / 4,096 / 8,192 leaves about 12,400 / 46,000 / 65,536 pixels (rows 0..48 / 0..180 / all); a 256x256 rectangle at N = 16,384 / 32,768 about 32,200 / 61,100; a flat triangle with legs of 256 (32,640 pixels) at N = 16,384 leaves 30,426 (rows 0..188); a gouraud one at N = 32,768 leaves 31,942 (rows 0..218). Counts vary by up to about 8% between consoles (fill at N = 1,024: 11,136-12,864 on the new GPU; SCPH-1001 fill at 4,096: 44,800 in six runs). On the new GPU, right after the reset GPUSTAT bits 26/27/28 read 001 if a draw was cut (1,548 of 1,548 rows) and 101 if it had finished (688 of 688); the SCPH-1001 reads 101 either way (216 of 216 cut draws). Bit 26 returns 16-128 cycles later (99% within 16-80).
Evidence: gpu-abort; 1001 (6), 5000 (7), 5501 (9), 7001 (6), 7501 (10), 7502 (1), 9001 (10).

## GP0 FIFO and DMA

**17. The CPU is not stalled by a full GP0 FIFO; the words are lost.** 128 words (64 one-pixel dots) written as fast as the CPU can store them take 352-368 cycles behind a busy GPU (a 320x240 fill or a 256x128 copy, every console) and 856-864 cycles to an idle one; written one at a time with bit 26 checked they take 3,184-3,272 (1001: 2,672). Behind a 320x240 fill or a 256x128 VRAM copy only 9 of 64 dots are drawn (18 words survive, consistent with psx-spx's 16-word FIFO plus a command in flight). To an idle GPU 46-48 of 64 survive. Written one at a time with GPUSTAT bit 26 checked, all 64 are drawn on every console. Agrees with psx-spx ("GPU FIFO / Overrun"); PeterLemon's GPU tests, which send about 1,000 unpaced words behind a fill, expect every word to be drawn.
Evidence: fifo-burst; 1001 (1), 5000 (7), 5501 (4), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

**18. GP0(03h) slows later drawing on the new GPU only.** With GP0(03h) = 0300000vh written before a flat 3x128 rectangle, the rectangle takes 724 (v = 0), 654, 745, 832, 961, 1,090, 1,223, 1,382, 1,482, 1,616, 1,744, 1,871, 1,994, 2,166, 2,255, 2,382 cycles (v = 15), about 125 more per unit from v = 2; a 64x64 rectangle takes 2,296 at v = 0 and 3,421 at v = 15. On the SCPH-1001 the same rectangles take 956-1,014 and 2,551-2,605 at every v. Agrees with psx-spx (3x128 about 4x slower at 0Fh; measured 3.3-3.6x). Open: the mechanism; psx-spx's 512x128 case was not run.
Evidence: gpu-version TIMING rows; 1001 (6), 5000 (7), 5501 (7), 7001 (6), 7501 (8), 7502 (1), 9001 (8).

**19. DMA2 transfer time by block size** (4,096 words, cycles):

| Block (words) | 8 | 16 | 32 | 64 | 128 |
|---|---|---|---|---|---|
| Upload, new GPU | 10,120-10,129 | 7,244-7,262 | 5,818-5,822 | 4,927-4,942 (7502: 5,175) | |
| Upload, SCPH-1001 | 10,293-10,300 | 7,380-7,402 | 6,038 | 5,240 | |
| Download (VRAM to RAM), new GPU | | 8,829-8,887 | | | 4,519-4,729 |
| Download, SCPH-1001 | | 12,376-13,029 | | | 4,823-4,961 |

An upload is about 1 cycle per word plus 12-13.5 cycles per block; a download with 16-word blocks pays about 18.5 per block. Block shape does not matter on the new GPU (16-word downloads 8,829-8,887); on the SCPH-1001 16-word downloads rise with row count: 12,376 (512x16), 12,473-12,511 (256x32), 12,670-12,696 (128x64), 13,016-13,029 (64x128). The old GPU downloads 16-word blocks about 1.4x slower, and uploads within 2% of the new GPU at 8- and 16-word blocks, 3.7% slower at 32 (6,038 against 5,818-5,822) and about 6% slower at 64 (5,240 against 4,927-4,942).
Evidence: gpu-dmabs2; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2).

**20. Cost of one primitive inside a DMA2 linked list** (cycles per primitive, slope between 16 and 32 in the list):

| Primitive | New GPU | SCPH-1001 |
|---|---|---|
| Flat triangle, legs 8 / 16 | 42.4 / 105.8 | 71.9 / 168 |
| Semi flat triangle, legs 16 | 180.7 | 314.8 |
| Gouraud triangle, legs 16 | 190.9 | 243 |
| Textured triangle, legs 16 | 180.4 | 244 |
| Gouraud-textured triangle, legs 16 | 281.1 | 281.1 |
| Flat quad 16x16 | 219.0 | 339 |
| Flat rectangle 78h 16x16 / semi 7Ah | 174.9 / 298.8 | 239 / 501 |
| Sprite 7Dh 16x16 | 174.4 | 239 |
| Flat line 16 / gouraud line 16 | 35.0 / 97.5 | 34.6 / 97.5 |

A stream of small flat triangles is bounded by set-up plus packet delivery, not drawing: legs 8 costs 42.4 in a list while a lone one takes 78 to finish.
Evidence: gpu-dmastream; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2); the six new-GPU models agree to about 1 cycle on nine of the twelve rows (semi flat triangle 180.0-181.7, sprite 7Dh 173.3-174.4, semi rectangle 7Ah 297.8-300.1).

**49. DMA does not wait for a full GP0 FIFO: a list node overruns it as the CPU's stores do (finding 17).** One linked-list node of one-pixel dots, GP1(04h)=2, behind a 640x480 fill: all lands up to 18 words. From 19 the newest words overwrite the oldest: `#.#######` at 19, `#...########` at 24 (`#` a dot landed). At 33 nothing lands and the GPU never reads idle: the 5-bit count has wrapped to empty with a command waiting for its word. At 34 the first dot lands and the pattern starts over. 18 is 16 in the FIFO, one in the decoder and one at the FIFO's output, which stays in the count. To an idle GPU a node of 48 to 240 words lands the first two dots, then about one in nine (`##........#........#`), because the words arrive at 1 a cycle and a dot takes about 17 of them; each word read has often just been overwritten, and a dot here and there comes out in another's colour. Block mode, GP1(04h)=1, 128 words behind the fill: blocks of 1 to 10 words land all 64 dots, 12 lose one, 16 lose two (`######..####`).
Evidence: gp0-overrun; SCPH-5501 unit 2 only (3 runs).

**50. GPUSTAT.25 with GP1(04h)=1 falls at 9 words counted, as nocash says.** "About 11" is words written. Behind a copy, a fill, a triangle or a rectangle it falls at word 10, one word having left the FIFO, rises again at word 33 when the 5-bit count wraps to 0, and falls at word 42. So F 10, W 33, F2 42: T = 9 both ways. It never falls for NOPs, or for words to an idle GPU.
Evidence: gp0-bit25, gp0-bit25b; SCPH-5501 unit 2 only (3 runs each).

**51. A quad's own words stay counted in the FIFO during its first half: all but one vertex's.** gp0-bit25b's 48 words of E6h behind a long first half wrap the count early: at word 28 flat or semi, 26 gouraud, 25 textured, 23 gouraud-textured, so 4, 6, 7 and 9 words were counted before the first E6h. The quad's 5, 8, 9 and 12 words less 1, 2, 2 and 3, one vertex's. No E6h leaves the FIFO in that time. Every triangle of every kind, rectangle and copy reads 33, none of its own. nocash has "a quad leaves its last vertex"; that would be 1, 2, 2, 3.
Evidence: gp0-bit25b; SCPH-5501 unit 2 only (3 runs), the gouraud-textured row with F at 1 so read from W alone.

## Drawing cost

**21. Pixel rates and row costs of triangles, new GPU.** After set-up a flat triangle is drawn at about 0.5 cycle per pixel and a shaded one at about 1.0, with about 1.9 cycles per row and about 11-13 fixed (a fit that reproduces the four sizes within 5 cycles): right triangles with legs 8 / 16 / 32 / 64 take 51 / 115 / 339 / 1,171 (flat) and 63 / 179 / 603 / 2,219 (shaded) cycles after set-up. Semi-transparent flat triangles take 1.55x a flat one (legs 16: 221-229 against 143; legs 64: 1,866-1,891 against 1,201).
Evidence: gpu-shape-time3; models as finding 11.

**22. A sloped left edge costs time, a sloped right edge does not.** A triangle is drawn outward from its leftmost vertex and each row starts from the previous row's left edge, so the pixels the left edge moves are paid for. The up/down drawing direction costs nothing.

| Triangle (same pixel count), new GPU | 320x16 | 64x64 |
|---|---|---|
| TL (0,0),(W,0),(0,H): slope on the right | 1,438 | 1,201 |
| TR (0,0),(W,0),(W,H): slope on the left | 1,588 | 1,259 |
| TL drawn bottom-up (vertex order) | 1,444 | 1,204 |

The extra is 150 cycles at 20 pixels of left-edge movement per row and 58 at 1 pixel per row. A row that contains no pixel is nearly free: a 2x128 sliver with 64 empty rows takes 346-349 against 609-613 with all 128 rows drawn (fit: about 0.3 cycles against 4.6).
Evidence: gpu-shape-time5; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2).

**23. Rectangles: exact costs, and thin tall ones are dear.** Bit 26's D, fastest:

| Shape | New GPU | SCPH-1001 |
|---|---|---|
| Flat 1x1 / 8x8 / 16x16 | 21 / 69 / 183 | 16 / 90 / 234 |
| Flat 32x32 / 64x64 | 603 / 2,206 | 714 / 2,480 |
| Flat 64x4 / 4x64 | 161 / 339 | 162 / 522 |
| Flat 256x2 / 2x256 | 285 / 1,170 | 278 / 1,802 |
| Semi 16x16 / 64x16 | 320 / 872 | 490 / 1,642 |
| 15-bit sprite 16x16 / 64x4 (cached texels) | 182 / 746 | 233 / 974 |
| Fill 16x16 / 64x64 | 112 / 592 | 138 / 714 |

Same area, different shape: 256x2 and 2x256 hold 512 pixels, and the tall one costs 4x as much. Widths 1, 2 and 3 at 256 rows all take 1,363-1,368 cycles on the new GPU (a floor of about 5.3 cycles per row); the old GPU steps every two pixels (128 rows: 868 / 1,004 / 1,003 / 1,138 cycles for 1 / 2 / 3 / 4 wide, polled). The X position does not matter (3x256 at X = 0, 13, 1021: 1,368 / 1,366 / 1,363).
Evidence: gpu-shape-time; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2). gpu-thin; 1001 (3), 5000 (7), 5501 (7), 7001 (6), 7501 (8), 7502 (1), 9001 (5).

**24. Big primitives, 256x256, display off** (cycles; polled, 45-cycle resolution):

| Primitive | New GPU | Per pixel | SCPH-1001 | Old / new |
|---|---|---|---|---|
| Fill | 6,061-6,073 (one SCPH-5000 log 5,565) | 0.09 | 6,109 | 1.01 |
| Flat rectangle | 33,890 | 0.52 | 35,067 | 1.03 |
| Semi rectangle | 54,490-54,538 | 0.83 | 101,879 | 1.87 |
| VRAM copy | 88,248-88,299 | 1.35 | 106,593 | 1.21 |
| 15-bit sprite, non-zero texels | 198,392 | 3.03 | 250,342 | 1.26 |
| 15-bit sprite, 0000h texels | 175,447-175,581 | 2.68 | not run | |

Right triangles with legs of 128 (blanking) tell the same story: flat 4,415 against 4,976; gouraud 8,685 against 9,350; semi flat 6,709 against 13,462; semi gouraud 8,746 against 26,193; 15-bit textured 1:1 27,500 against 36,418; 4-bit 12,805 against 14,894; 15-bit minified 64x64 over a 256-texel span 19,598 against 29,410. Disagrees with nocash's remark that old GPUs run at about 60% of the new GPU's speed (the newer psx-spx table does not repeat it): the ratios run from 1.0 to 3.0 by primitive; pixels cost the same, and rows, semi-transparency and texture fills cost more.
Evidence: gpu-scanout (display-off rows), 1001 (3), 5000 (7), 5501 (7), 7001 (6), 7501 (8), 9001 (5); gpu-shape-time17, 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 9001 (6).

**25. Quads draw as two triangles with the second set-up hidden.** A flat 16x16 quad finishes after 248 cycles against 143 + 148 = 291 for its two lone halves; a 64x64 one after 2,391 against 2,433; 320x16 2,825 against 2,871. A flat quad's bit 28 returns at 132 (16x16) and 1,190 (64x64): about 11 cycles before the first half finishes. The saving is 31-46 cycles for flat quads.
Evidence: gpu-shape-time3, -time5; models as finding 11.

**52. With E1h.10 clear in 480i a fill pays only for the rows it writes; a copy and an upload write every row.** A 640x480 fill 21,886-21,903 cycles with the bit set, 10,941-10,983 clear, 49%; rectangle 49%, triangle 48%, line 49%, each writing 240 rows of 480. A VRAM copy of 512x240 or 16x240 costs the same either way (99-100%) and writes all 240 rows; copies and CPU uploads at y 0, 1, 40 and 41 write all 16 rows in either field.
Evidence: gpu-fill-480i, gpu-copy-480i; SCPH-5501 unit 2 only (3 runs each).

**53. A triangle's rows are walked out from its leftmost vertex, and a walk that has passed the drawing area stops.** Rows above the area on a walk going down, below it on a walk going up, or beside it, cost 1 cycle each. So with the leftmost vertex on top a triangle pays its rows above the area and none below; with it at the bottom, none above and all below; in the middle, half each way. Exact on six shapes at four places and two heights: D = S + 6 + rows paid, where S is finding 11's formula. Ties in "leftmost" go as mednafen's DrawTriangle has them, whose walk this is. Rectangles walk down: 64 rows above the area add 68 cycles, 64 below add none. The same holds for rows hidden above and below a shape that does draw (128x128 clipped by the top or the bottom, within 5 cycles of 4,000 on the rule).

| Kind wholly outside, 128 wide | Set-up reading | Row |
|---|---|---|
| Flat or semi triangle (top flat) | 37 | 1.00 |
| Gouraud triangle | 168 | 1.00 |
| Textured or raw-textured triangle | 135 | 1.00 |
| Gouraud-textured triangle | 256 | 1.00 |
| Rectangle: flat / semi / textured | 16 / 15 / 13 | 1.00 |

Readings are at 128 wide with the stores back to back, as `gpu-outside`; `gpu-outside2` and `3` store from a loop and read 3 less. psx-spx's dev text has 0.7 to 1.3 cycles a row and no side; the side is the rule above. A quad is two triangles, the second's set-up running under the first's rows.
Evidence: gpu-outside (5 runs), gpu-outside2, gpu-outside3 (3 runs each); SCPH-5501 unit 2 only.

**54. Wholly left of the drawing area a primitive pays once for its width.** W / 2 cycles flat or semi, W gouraud or textured, W / 2 for a rectangle of any kind: 8, 16, 32, 64, 128 and 16 to 256 at W 16 to 256, the same at distances of 136 to 512 pixels. A triangle whose leftmost vertex is in the middle has two walks and pays twice. Right of the area, none; above and to the left, none (a walk that meets no row of the area does not pay). A triangle with its left edge upright and its flat side at the bottom reads 3-4 less than this.
Evidence: gpu-outside, gpu-outside3; SCPH-5501 unit 2 only.

**55. Small rectangles: a list packet costs about 27-30 cycles, whatever it draws.** Cycles per primitive in a DMA2 list, one a packet (slope from 8 to 32): dot 68h 27.5, semi dot 27.5, textured dot 27.8, 60h 1x1 29.6, 2x2 29.9, 4x4 31.1, 16x1 29.6, 1x16 72.6, 70h 8x8 62.0, 60h 8x8 63.0, semi 72h 8x8 88.2, textured 75h 8x8 62.0. Sent in one block instead, so the GPU never waits for a packet, 64 dots take about 15 cycles each (974-1045 to idle). So the 27.5 is the list: finding 10's 13 for bit 28 and about 14 for the next packet to arrive.
Evidence: gpu-dmastream5, gp0-overrun; SCPH-5501 unit 2 only (3 runs each).

## Scan-out

**26. The old GPU's drawing ignores the display; the new GPU's does not, by mode and repeatably.** Fastest of four, 256x256 primitives drawn into the displayed area (SCPH-7501, first session; the other new-GPU models agree to within about 100 cycles for the same VRAM state (12 of 20 cells within 60; largest difference 173, semi rectangle at 640x480i 24-bit; the 7501's own three first-session logs differ by up to 129 on the 320x240 fill)):

| Primitive | Display off | 320x240 15-bit | 640x480i 15-bit | 640x480i 24-bit |
|---|---|---|---|---|
| New: flat rectangle | 33,890 | 35,203 | 34,347 | 37,146 |
| New: semi rectangle | 54,498 | 50,611 | 55,577 | 53,856 |
| New: fill | 6,061 | 5,928 | 5,563 | 6,646 |
| New: copy | 88,297 | 84,416 | 84,910 | 87,488 |
| New: 15-bit sprite | 198,394 | 196,001 | 201,057 | 209,190 |
| Old: flat rectangle | 35,067 | 35,062 | 35,064 | 35,066 |
| Old: semi rectangle | 101,879 | 101,883 | 101,880 | 101,881 |
| Old: copy | 106,593 | 106,593 | 106,593 | 106,593 |
| Old: 15-bit sprite | 250,342 | 250,347 | 250,347 | 250,347 |

The new GPU's cost is not monotonic in the picture's size (320x240 is cheaper than display off for semi rectangles, copies and sprites). When VRAM was cleared first (later sessions) the three display columns read about 51,400 / 55,600 / 59,100 for the semi rectangle and 84,500 / 84,600 / 91,950 for the copy, and the 15-bit sprite about 11% less in every mode (its texels were then 0000h).
Evidence: gpu-scanout; 1001 (3), 5000 (7), 5501 (7), 7001 (6), 7501 (8), 9001 (5). The 7502's one run (PAL) differs.

**27. What the picture costs a long draw, by kind** (right triangle, legs 128, averaged over 16 starts across one line of a 512x240 15-bit display, as extra over the same draw in the blanking; 256x64 for the last row):

| Draw | New GPU, blanking cycles | Extra in the display | SCPH-1001 extra |
|---|---|---|---|
| Flat | 4,415 | +6.2% | +0.3% |
| Gouraud | 8,685 | +1.1% | +0.1% |
| Semi flat | 6,709 | +9.8% | +0.1% |
| Semi gouraud | 8,746 | +6.6% | 0.0% |
| 15-bit textured, 1:1 | 27,500 | +7.8% | 0.0% |
| 4-bit textured, 1:1 | 12,805 | +5.3% | 0.0% |
| Gouraud-textured | 27,623 | +7.9% | 0.0% |
| 15-bit minified | 19,598 | +8.6% | 0.0% |
| Flat 256x64 / gouraud 256x64 | 4,345 / 8,621 | +6.2% / +1.0% | +0.1% / +0.1% |

On SCPH-5000, 5501, 7001, 7501 and 9001 the extra agrees to within 0.3 percentage points and the blanking cycles to within 7 cycles for flat, gouraud and semi draws and 13-33 cycles for the textured ones (4-bit 12,784-12,817).
Evidence: gpu-shape-time17; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 9001 (6).

**28. Both GPUs are held when a draw spans a display-line start; the amount differs.** Over the fastest-to-slowest spread of every `gpu-shape-time` shape (eight start points an eighth of a line apart): draws under 150 cycles show no spread (98.7% of 1,867 new-GPU rows, 77% of 276 old-GPU rows); draws over 270 cycles on the new GPU show a median spread of 15-25 cycles (90th percentile 24-39; rare maxima up to 87); on the old GPU 40-42 cycles with a 90th percentile of 42-48. This appears in the vertical blanking too. Shapes of wide rows often show much less than the full amount.
Evidence: all `gpu-shape-time` programs; about 15,500 shape rows on the new GPU (5501, 7501, 9001), 3,300 on the old (1001).

**29. The line-start fetch holds a draw by about 93-99 cycles on the new GPU.** A probe that pays every burst whole, started at 300 consecutive points 2 cycles apart across the fetch, is delayed at most by: vertical 1x64 line 94, semi rectangle 16x16 99, fill 16x16 93, 16-pixel copy 48-50. Same in 320x240 and 512x240 15-bit modes. The old GPU's worst delay for the same four is 38-42, the line hold alone.
Evidence: gpu-shape-time74; 1001 (1), 5000 (1), 5501 (2), 7501 (2), 7502 (1), 9001 (1).

**30. Copies pay by width and mode.** The extra a copy pays in the display over the blanking (percent): 16 wide 7.2 at 512x240 and 6.9 at 320x240; 256 wide 5.5 and 0.8; 512 wide 5.1 and 0.6. On the old GPU 4% at 16 wide (the line hold) and 0-1.3% above.
Evidence: gpu-copy3; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 9001 (6).

**31. Short-draw timings in the display vary from session to session, and VRAM contents change how much.** Copies and textured draws started at 32 points across one line differ between sessions of the same console at the same start. The mean spread (max - min over runs, per start point) at 320x240 falls from 37.7 to 3.0 cycles for a 64x64 copy and from 3.3 to 0.0 for a 15-bit 16x16 sprite when VRAM is filled first (6 runs against 4); a 256x32 copy 53.3 to 33.2; at 512x240 the effect is smaller (25.3 to 18.6, 40.3 to 37.8). Taking every 4 of the 6 runs, to compare like with like, gives 31.9 (25.7-36.0) for the 320x240 64x64 copy against 3.0, and 2.5 (1.2-3.3) for the 16x16 sprite against 0.0; for the 256x32 copy (40.7, range 16.9-51.5, against 33.2) and at 512x240 (22.3 against 18.6; 36.8 against 37.8) the difference lies inside the spread, so only the first two support a VRAM effect. Open: what the discrete session state is (the GPU clock's phase against the CPU's is suspected). A shape's mean over runs moves by a median of 2.6 cycles between runs; the 256x32 copy moves by 38.6 (320x240) and 30.4 (512x240), the 64x64 copy by 18.8.
Evidence: gpu-shape-time68 (SCPH-7501, 6 runs), gpu-shape-time68c (SCPH-7501, 4 runs, VRAM filled first).

## Copies and fills

**32. New-GPU VRAM copy cost is a curve in width** (cycles for 32 rows):

| Width | 16 | 32 | 64 | 128 | 256 | 320 | 512 |
|---|---|---|---|---|---|---|---|
| New GPU | 1,295 | 1,970 | 3,009 | 5,236 | 10,551 | 13,581 | 21,126 |
| Per pixel | 2.53 | 1.92 | 1.47 | 1.28 | 1.29 | 1.33 | 1.29 |
| SCPH-1001 | 1,200 | 1,970 | 3,506 | 6,671 | 13,359 | 16,922 | 26,726 |

Height is linear (128 wide: 8 / 64 / 128 rows = 1,340 / 10,464 / 20,900 on the new GPU, 1,743 / 13,308 / 26,546 on the old). Where the source and destination lie in VRAM does not matter (256x32 at four placements on the old GPU: 13,302-13,305).
Evidence: gpu-copy2; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2); gpu-shape-time27; 1001 (2), 9001 (2).

**33. Old-GPU copy rows: 1.5 cycles per pixel plus 13 per started 128-pixel block, less 2.** A single-row copy of 8 / 16 / 32 / 64 / 128 / 256 / 512 pixels takes 25 / 37 / 61 / 109 / 205 / 410 / 820 cycles (the formula plus 2 for the probe), exact in all seven; a copy that outlasts a display line also pays the line hold (about 41) for each line start it crosses.
Evidence: gpu-shape-time73, gpu-shape-time27; 1001 (3 and 2).

**34. Checking the mask bit (E6h bit 1) costs a copy extra on both GPUs, differently.** Extra cycles for a single row of 8 / 16 / 32 / 64 / 128 / 256 / 512 pixels:

| | 8 | 16 | 32 | 64 | 128 | 256 | 512 |
|---|---|---|---|---|---|---|---|
| New GPU | +5 | +7 | +6 | +15 | +29 | +61 | +125 |
| SCPH-1001 | +8 | +16 | +32 | +64 | +128 | +256 | +512 |

The new GPU's extra is about 0.2455 cycle per pixel from 64 wide, and rows add (16x16: +112 = 16 x 7; 128x8: +232; 256x8: +512). The old GPU pays exactly one cycle per pixel (16x16: +256; longer copies add the line hold as well). Agrees with psx-spx's old-GPU figures (1.5 plain, 2.5 masked per pixel); disagrees with its new-GPU masked-copy figure.
Evidence: gpu-shape-time73; 1001 (3), 7501 (3), 5501 (2), 9001 (1), 5000 (1), 7502 (1).

**35. Fills.** About 14-16 pixels per cycle plus 5-6 cycles per row (16 wide) and a small fixed cost: 112 (16x16), 52 (64x4), 400 (16x64), 592 (64x64) on the new GPU; 138 / 54 / 522 / 714 on the old GPU, which pays about 2 more cycles per row. Large fills, polled: 256x256 5,981-6,109 (both GPUs), 1008x16 1,230-1,361, 16x256 1,775-1,819 (SCPH-1001 2,183-2,257). A fill is also touched by the picture (finding 26: 5,563-6,646 by mode on the new GPU, flat on the old).
Evidence: gpu-shape-time, gpu-cost-sweep; models as finding 23.

## Lines

**36. Line cost by slope** (D, flat line from (192,128); dx = 32 with the dy shown):

| dy | 0 | 1 | 2 | 3 | 4 | 8 | 12 | 16 | 24 | 31 | 32 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| New GPU | 68 | 83 | 91 | 95 | 98 | 107 | 116 | 124 | 141 | 168 | 173 |
| SCPH-1001 | 56 | 73 | 82 | 86 | 89 | 104 | 128 | 152 | 200 | 242 | 248 |

A level line is about 34 + 1 cycle per pixel on the new GPU (4 / 32 / 128 / 255 pixels: 33 / 68 / 162 / 288). A tilted line is about 24 + 2 per pixel + 2 per row (a loose fit: it reads 150-152 against 168-173 at dy 31-32); the direction of drawing does not matter ((32,8) (32,-8) (-32,8) (-32,-8): 107 / 108 / 108 / 107); a line at 45 degrees or steeper costs by its length alone ((8,32), (16,32), (24,32), (32,32): all 173). Agrees with psx-spx's line table (about 2 cycles per pixel and per row off the horizontal).
Evidence: gpu-shape-time11; 1001 (2), 5000 (7), 5501 (7), 7001 (6), 7501 (2), 7502 (1), 9001 (2). The six new-GPU models agree to the cycle in all 25 logs.

**37. Line set-up.** Bit 28 returns 17 cycles after a flat line's last word on every model, and 79 after a gouraud line's; the gouraud line then draws as a flat one (32,8: 169 against 107, a difference of 62 = 79 - 17). A semi line adds almost nothing on the new GPU (+4) and about 2 cycles per pixel on the old (32,8: 170 against 104).
Evidence: gpu-shape-time3, -time11; models as findings 11 and 36.

## Texture and CLUT

**38. A palette (CLUT) reload costs about 22 cycles for 16 entries and 260 for 256 on the new GPU, and the 4-bit one hides when sprites are queued.** Cycles per sprite that reloads against one that does not:

| | New GPU | SCPH-1001 |
|---|---|---|
| 4-bit, sprites queued, CPU waits at the end | 0.0-3.2 | -0.1-1.1 |
| 8-bit, queued | 251.4-253.5 | 240.3-240.9 |
| 4-bit, CPU waits for bit 26 after every sprite | 20.1-24.3 | 8.8-10.6 |
| 8-bit, waiting every sprite | 259.1-262.8 | 256.5-258.6 |

Paletted rows over a "transparent" CLUT written by a fill cost the same as over an opaque one (4-bit 256x256 sprite 77,067-80,039 against 77,315-79,995), which is consistent with a fill not refreshing the CLUT cache (amidog's psxtest_gpu results); it is not shown independently here. The 7502's one run reads 16.6-17.9 (4-bit, waiting).
Evidence: clut-cost; 1001 (6), 5000 (7), 5501 (11), 7001 (6), 7501 (12), 7502 (1), 9001 (10); gpu-tex-opaque; 1001 (6), 5000 (7), 5501 (7), 7001 (6), 7501 (8), 7502 (1), 9001 (8).

**39. Textured drawing is dominated by texture-cache fills, and transparent texels are cheaper.** A 256x256 15-bit textured quad on a single texel (every UV 0, so the cache never refills) takes 67,397-67,782 cycles; on a full 1:1 texture it takes 230,563-234,505, 3.4x as much. 256x256 sprites: 4-bit 77,067-80,039, 8-bit 124,889-125,262, 15-bit 198,406-202,556. With all texels 0000h (transparent) the 15-bit sprite takes 177,473-179,615 (10% less) and the quad 212,469-216,350 (8% less). Open: the per-fill cost is not isolated by a direct measurement.
Evidence: gpu-semi-shape, gpu-tex-opaque; models as finding 38 and gpu-semi-shape 1001 (3), 5000 (7), 5501 (7), 7001 (6), 7501 (5), 7502 (1), 9001 (5).

**40. Semi-transparency on textured draws is tied to the cache lines on the new GPU and is a flat 2 cycles per pixel on the old.** 256x256 15-bit quad, extra over opaque: new GPU +4,400 (6.5%) on one texel and +28,000 (12%) on a full texture; SCPH-1001 +133,600 on both (2.04 cycles per pixel; 190% on one texel).
Evidence: gpu-semi-shape; models as finding 39.

**56. A cached 0001h texel against 0000h on the new GPU: 0.019 cycle a pixel, not 0.04.** A raw 15-bit 64x64 rectangle, texels cached by a texture window: 0001h 2,206-2,220 cycles, 0000h 2,127; uncached 11,813-11,842 and 10,392-10,417, 0.346 a pixel.
Evidence: gpu-transp-cached; SCPH-5501 unit 2 only (3 runs).

## MDEC

**41. Decode throughput is 512 cycles per 8x8 block.** 320 macroblocks fed by DMA0 and drained by DMA1 in 32-word blocks take 983,5xx cycles: 3,073 per macroblock at both 15 and 24 bpp (24 bpp moves 1.5x the output words, so the output transfer is hidden under the next decode), and 513 per block in 8-bit mono, for flat and for dense mono data alike. One SCPH-5501 log (the first day) hung on the DC-only mono run (status A204013F). All three psx-spx texts call MDEC decode time unknown. Agrees with mednafen's "about 512" a block; DuckStation's 448 is too fast.
Evidence: mdec-time; 1001 (1), 5000 (7), 5501 (4), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

**42. A decode command of F000h words never finishes.** A dense command of 61,440 (F000h) words stops with status A6007FFFh (15 bpp) or A4007FFFh (24 bpp): the data-in request drops after 7000h words (the count behaves as 15 bits), 8000h words stay unsent on DMA0, and the command waits for ever. The same on every model. Counts up to 10,240 words finished; nothing between 10,240 and 61,440 was run, so the 8000h limit is inferred.
Evidence: mdec-time; models as finding 41.

**43. DMA0 into the MDEC finishes a 32-word table 75 cycles after the CHCR write, and the MDEC reset is instant.** A 32-word table transfer finishes 75 cycles after the CHCR write in 1,016 of 1,236 completed transfers (75-85 in all but one that took 39), at every delay from 1 cycle to 4,096 after the reset. Open: in 24 of 30 logs (the sessions started through the locked-start runner; the other 6 are the first two days, so the runner and the date cannot be told apart) the first case, the request-enable write straight after the reset, left status 60000000h and DMA0 busy until the 2 ms limit; the other 1,236 transfers finished.
Evidence: mdec-reset; 1001 (1), 5000 (6), 5501 (4), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

**44. The YUV to RGB stage, exactly.** With signed Y, Cb, Cr in -128..127 from the IDCT, the output is reproduced in every cell of the flat-block sweep (5,120 R, 5,120 B and 18,432 G cells per depth, 56 sweeps per log, 16 logs on all seven models) by:

- R term = 359 x Cr; B term = 454 x Cb; G term = (-88 x Cb with its low 5 bits cleared) + (-183 x Cr with its low 3 bits cleared);
- S = 256 x Y + term, wrapped to a 17-bit signed range (a sum beyond ±256.0 wraps instead of saturating; between ±128.0 and ±256.0 it saturates: Y = +64 with Cb = +108 gives 255.5 and reads 255. Y = 127 with Cr = 92 gives 0, Y = -128 with Cr = -92 gives 255), then limited to -128..127.996;
- output = S + 128 x 256, then rounded once to the output precision: add 80h and shift right 8 for 24 bpp; add 400h and shift right 11 for 15 bpp (5 bits); clamp.

Taking each step away, on one log (SCPH-7501): without the two cleared-bit masks 325 of 18,432 G cells (24 bpp) and 46 (15 bpp) are wrong; truncating instead of rounding the sum gets 3,559 of 5,120 R cells right; saturating instead of wrapping gets 4,828 R and 4,521 B cells right. The cleared-bit masks are the ones DuckStation uses. An independent reimplementation from this text alone scored 917,504 of 917,504 cells. The sweep's Y takes five values (-128, -64, 0, +64, +127) and both chroma are non-zero together only at Y = 0, so this is exact on that grid, not tested for arbitrary YCbCr.
Evidence: mdec-color with each console's own mdec-idct flat-byte table; 1001 (1), 5000 (1), 5501 (3), 7001 (2), 7501 (4), 7502 (1), 9001 (4).

**45. The IDCT and dequantiser give byte-identical blocks on every console.** DC-only blocks at q_scale 0 and 1 (every DC from -512 to 511) and 72 single-AC blocks (positions 1, 2, 9, 63; DC -100, 0, 37; levels -300, -51, -7, 7, 51, 300) were identical in all 48 logs. At q_scale 0 the flat byte for DC >= 0 is exactly 80h + floor((DC + 1) / 4), clamped at FFh (checked for all 512 values); below zero the steps are 3, 4 or 5 DC values wide in an irregular order (00h for 2 values, 01h for 5, 02h for 3, 03h for 4, 04h for 5, 05h for 3, ...; run lengths over the whole range: 3 x32, 4 x189, 5 x34, 2 x1), so the first IDCT pass truncates rather than rounds. Open: the full IDCT arithmetic is not given here.
Evidence: mdec-idct; 1001 (6), 5000 (6), 5501 (9), 7001 (6), 7501 (10), 7502 (1), 9001 (10).

**46. A zero quantiser entry contributes nothing.** With q_scale 1 and luma quantiser entry 5 set to 0, a block with a level of 100 at position 5 decodes to the DC alone (flat 71h); with the normal entry (19) it decodes to 9881614A4A618198 per row. Agrees with psx-spx; mednafen's "level x 2" does not.
Evidence: mdec-idct (QZERO); models as finding 45.

**47. After a command that ends at its end code the MDEC keeps producing blocks.** A one-block 8-bit command [0340 FE00] sent without padding returned 32 words (two blocks) in 3 logs (5501 x1, 7501 x2, first session) and 96 words (six blocks) in the other 27, all models; the extra words are not zero (80857F88h 857C8073h ... in those first three logs; CBCBCBCBh, 80808080h or 7E7E7F7Fh ... in the rest). The same block padded with FE00h to 32 words returned exactly 16 words (one block) in every log. Open: the source of the extra blocks (stale input-buffer contents are suspected); games that feed the MDEC in padded 32-word blocks never meet it.
Evidence: mdec-mono-out; 1001 (1), 5000 (6), 5501 (4), 7001 (6), 7501 (6), 7502 (1), 9001 (6).

## Open

- How the display's fetches (a line-start fetch and a refill every 128 pixels in 15-bit modes) interact with each kind of draw is mapped only for a few shapes (`gpu-shape-time12`-`16`, `66`-`76`, SCPH-7501 and 9001); lines, fills and semi draws wait out the line-start fetch, other kinds hide or absorb parts of it, and no short account covers every kind.
- The discrete session state of short draws inside the display (finding 31).
- The mechanisms behind GP0(03h) (finding 18) and the early GPUREAD (finding 14).
- The old GPU is one console.
