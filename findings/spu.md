# SPU

What the sound chip does on real consoles, from test programs run on SCPH-1001, 5000, 5501, 7001, 7501, 7502 and 9001. Two units each of the 5501 and 9001 were used and one of every other model. Each finding gives the numbers, the test behind them and how many runs on which models. Ranges are the lowest and highest value over the runs listed. Tests are in `hwtests/`, logs in `consoles/`.

**Conventions**

- One SPU sample is 768 CPU cycles (44.1 kHz). Cycle counts are CPU cycles read from Timer 2.
- "The edge" is the moment a CPU polling SPUSTAT sees bit 11 change. Bit 11 changes every 256 samples (196,608 cycles), once per half of the capture buffers. Tests that need a fixed phase wait for the edge and then add a delay D.
- "Separate-chip boards" are SCPH-1001, 5000, 5501 and 7001. "One-chip boards" are SCPH-7501, 7502 and 9001, where psx-spx puts the SPU and the CD controller in one chip. Several results below split along exactly this line.
- "Class A" and "class B" are the two key-on phases of finding 23.
- A run count is a count of log files. Most tests ran through `hwtests/runner`, which sets 1F801DACh to 0004h and 1F801014h to 200931E1h before each test.
- Where psx-spx is named, "current text" is the newer SPU chapter and "nocash's text" is the older one.

---

## Manual writes and the 32-halfword FIFO

**1. The manual-write FIFO holds 32 halfwords.** Writing 33 to 128 halfwords to 1F801DA8h with the transfer stopped, then switching SPUCNT to manual write, puts the first 32 in sound RAM and drops the rest. Tested with 33, 40, 48, 56, 60, 63, 64, 65, 72, 96 and 128 halfwords; 31 and 32 land whole. Starting again lands nothing more.
Evidence: spu-fifo, 43 runs (1001 6, 5000 7, 5501 7, 7001 6, 7501 8, 7502 1, 9001 8); spu-fifo2 and spu-fifo3, 31 runs (1001, 5501, 7501, 7502, 9001); all alike.

**2. Halfwords written in manual-write mode with no transfer running are queued, not written.** With SPUCNT already in manual write, 8 or 64 halfwords written to 1F801DA8h land nowhere (0 of 64 in every spu-fifo run, also when paced 200 cycles apart). A Stop keeps them. After a new transfer address and a new manual-write start, up to 32 of them land at the new address: 8 queued, 8 land; 40 queued, 32 land; 32 queued after a completed 32, 32 land. Rewriting SPUCNT with the same manual-write mode (1 to 1) while 32 are queued writes them at the next address, giving 64 contiguous halfwords.
Evidence: spu-fifo (RUNNING 64, SLOW 64), spu-fifo2 (RUN8, RUN64, RUN8+STOPGO), spu-fifo3 (RUN40+GO, AGAIN, AFTER+GO); 31 runs on five models, all alike.

**3. A second batch written while the first is draining is only partly taken.** 32 halfwords, manual write started, 32 more written at once (before bit 10 clears): 37 to 40 contiguous halfwords land in spu-fifo's timing and 45 to 50 in spu-fifo2's. A few later halfwords land singly at later addresses; the rest are lost.
Evidence: spu-fifo TWICE (43 runs, 37-40), spu-fifo2 TWICE (17 runs, 45-50).

**4. A DMA read started while 32 halfwords are queued returns a few good halfwords, then repeats one.** With 32 halfwords queued behind a finished transfer, a DMA4 read of sound RAM returns the first 9 to 12 halfwords correctly and then the same halfword for the rest of the block. A second read straight after is clean (32 of 32), so sound RAM was not damaged.
Evidence: spu-fifo2 AFTER (17 runs), spu-fifo3 AFTER and AFTER read 2 (14 runs); 1001, 5501, 7501, 7502, 9001.

**5. SPUSTAT bit 10 (transfer busy) is set from a manual write's start until its FIFO has emptied, but is only seen at SPUSTAT's refreshes.** With SPUCNT written at the edge (D = 0), bit 10 is already clear when the new mode shows for up to 17 halfwords, and set from 18 on. The first length seen busy was 18 in 32 of 42 runs, 17 in 3 and 19 in 7. It clears 1,395 to 1,474 cycles after the SPUCNT write. Written later in the sample the mode shows sooner and even 8 halfwords are seen busy (D = 288-576: mode shown 149-467 cycles after the write, bit 10 clear 915-1,233 cycles after). Unsynchronised, 32 halfwords read busy for 700-763 or for 1,456-1,527 cycles. Bit 10 stayed clear for queued halfwords (finding 2) and for DMA writes (finding 8).
Evidence: spu-busy, 42 runs (1001 6, 5000 6, 5501 7, 7001 6, 7501 8, 7502 1, 9001 8); spu-fifo2 and spu-fifo3 busy columns.

---

## Transfer types (1F801DACh)

**6. Transfer types other than Normal write fewer halfwords, and the pattern differs between the two board groups.** Eight halfwords 1111h..8888h written by hand at 5000h over EEEEh and read back by DMA:

| 1F801DAC | Halfwords that land (numbered 1-8), separate-chip boards | one-chip boards |
|---|---|---|
| 0004h (type 2, Normal) | 1-8, in order | 1-8, in order |
| 0006h (type 3) | 1, 3, 5, 7 to four consecutive addresses | 2, 4, 6, 8 to four consecutive addresses |
| 0008h (type 4) | 4, 8 | 4, 8 |
| 000Ah (type 5) | 7 | 8 |
| 000Ch, 000Eh (types 6, 7) | 8 only | 8 only |
| 0000h, 0002h (types 0, 1) | 8 only (SCPH-1001: finding 7) | 8 only |

So a type writes one halfword per group of 2, 4 or 8 written (Fill: one in all). Disagrees with psx-spx: nocash's table, and the same rows in the current RAM-size table, list eight halfwords written for each Rep type (A,A,C,C,E,E,G,G for 0006h; A,A,A,A,E,E,E,E for 0008h; eight H for 000Ah) and eight X for Fill. The consoles write 4, 2, 1 and 1 locations. The value that lands agrees with nocash's table only for Fill and for one-chip Rep8; the count never agrees. The current text's remark that consecutive halfwords alias onto one location reads as predicting the one-chip Rep2 row (the later of each pair survives) but not the separate-chip one (the earlier survives).
Evidence: spu-xfer TYPES, 50 logs: separate-chip 28 (1001 6, 5000 7, 5501 9, 7001 6), one-chip 22 (7501 11, 7502 1, 9001 10). Two of the 7001's six have stray corrupted halfwords from the unstable read (finding 9).

**7. SCPH-1001 only: types 0 and 1 write at four-halfword spacing.** With 1F801DAC = 0000h the read-back shows 1111h at +0, 2222h at +4 and 3333h at +8; with 0002h it shows 1111h, 3333h, 5555h at +0, +4, +8. The other six models gave the last halfword only. Fits psx-spx (current text), which says the SCPH-1000 treats the invalid RAM-size setting differently.
Evidence: spu-xfer, 6 of 6 runs of one SCPH-1001.

---

## DMA to and from sound RAM

**8. A DMA write's last FIFO-full is still on its way when DMA4 reports done.** A Stop written k cycles after the DMA channel's busy bit clears loses the rest of a 32-halfword upload:

| k (cycles) | 0 | 25 | 50 | 100 | 200 | 400 | 800 | 1,600 and 3,200 |
|---|---|---|---|---|---|---|---|---|
| halfwords landed of 32 | 4-8 | 4-5 | 5-6 | 7-9 | 12-13 | 15-16 | 28-30 | 32 |

At k = 0 the first trial of a run reads 7 (8 on some 7001 runs) and the next two 4 or 5. SPUSTAT bit 10 was never seen set from the end of DMA4 on (1,242 trials). Agrees with psx-spx (current text: the last 50-60 bytes are not yet written when CHCR clears).
Evidence: spu-dma TAIL, 46 runs x 27 trials (1001 6, 5000 6, 5501 9, 7001 6, 7501 10, 7502 1, 9001 8).
Open: psx-spx (current text) says SPUSTAT bits 7 and 9 rise 1,400-1,700 cycles after CHCR clears. spu-dma polled bit 10 only, so this was not read.

**9. A DMA read of sound RAM is unstable with 1F801014h = 200931E1h and clean with 220931E1h.** At 200931E1h (the BIOS value) every read starts with an inserted FFFFh. Each 32-halfword unit starts with FFFFh and loses its last halfword (1000h-101Eh, FFFFh, 1020h-103Eh, ...). In 46 of the 138 block-16 reads one or two more halfwords per 64 also read FFFFh, at varying places. At 220931E1h (bits 24-27 nonzero) 64 halfwords read clean. With a block size of 17 words and 220931E1h each block delivers 1000h-101Fh, then 101Fh twice more, and the next block starts at 1020h (one 32-halfword FIFO per block). Agrees with nocash's text and with ps1-tests' spu/memory-transfer, which sets 220931E1h.
Evidence: spu-dma READ, 46 runs x 3 reads x 2 block sizes x 2 settings (1001 6, 5000 6, 5501 9, 7001 6, 7501 10, 7502 1, 9001 8); every read followed the pattern.

**10. How fast DMA4 pushes blocks into the SPU depends on 1F801014h.** Cycles until a CPU polling CHCR sees 1, 4, 16 and 64 blocks of 16 words finished, with the CPU doing 16 ALU operations per poll:

| 1F801014h | 1 block | 4 blocks | 16 blocks | 64 blocks |
|---|---|---|---|---|
| 200931E1h (43 runs, all models) | 86-95 | 3,319-3,382 | 16,556-16,582 | 69,956-70,042 |
| after a test that had left 220931E1h (5 runs: 5501 1, 7501 2, 9001 2) | 150 | 3,592-3,616 | 17,470-17,500 | 72,766-72,796 |

A 2,048-word DMA with the CPU spinning ended 140,792-140,976 cycles after the start (68.8 cycles a word) in 41 runs on all seven models; CPU work beside it moved the end by about 500 cycles at most. The 220931E1h setting was not read back in the five slower runs; it is inferred from the run order. psx-spx (Memory Control, Rix's logic-analyzer measurements) says bit 29 makes DMA use bits 24-27 for the strobe length; the 4% slower rate fits that.
Evidence: dma-rates SPU rows, 48 runs (1001 6, 5000 7, 5501 9, 7001 6, 7501 11, 7502 1, 9001 8); dma-arb SPU rows, 41 runs (1001 6, 5000 6, 5501 7, 7001 6, 7501 8, 7502 1, 9001 7).

**11. SPUSTAT bit 9 is the DMA-write request and bit 8 the DMA-read request.** Switching to DMA-write mode shows bits 9 and 7 together with the mode bits (SPUSTAT 02A0h, or 0AA0h with bit 11 set), 120-784 cycles after the SPUCNT write in 45 of 50 runs. Switching to DMA-read mode shows the mode bits alone first (0030h at 56-840 cycles, 37 of 37 runs where the first change was seen), and bits 8 and 7 (01B0h or 09B0h) were present at the next look, 15,900-32,900 cycles later. Disagrees with psx-spx's STATX table (bit 9 "DMA Read Request", bit 8 "DMA Write Request"), whose prose about DMA-write mode (bits 7 and 9 rise together) agrees with this. The read-mode delay agrees with the "not set immediately" remark; the labels need swapping.
Evidence: spu-xfer REQUEST, 50 runs (1001 6, 5000 7, 5501 9, 7001 6, 7501 11, 7502 1, 9001 10). The test prints on the first change, so a second look comes only after the print.
Open: when between 840 and about 16,000 cycles the read request bits rise.

---

## SPUSTAT, SPUCNT and register access

**12. SPUSTAT is refreshed once per sample, and a SPUCNT write shows in bits 5-0 at the next refresh, never sooner.** A write D cycles after the edge shows up in roughly 768 - D - 75 cycles:

| D | 0 | 96 | 192 | 288 | 384 | 480 | 576 | 672 |
|---|---|---|---|---|---|---|---|---|
| cycles from the write until bits 5-0 show | 606-699 | 608-693 | 534-549 | 377-467 | 303-390 | 226-309 | 149-154 | 765-862 |

The delay falls about one cycle per cycle of D and wraps to the following refresh between D = 576 and 672. Bit 11 itself changes at a refresh.
Evidence: spu-busy PHASE and LENGTH rows, 42 runs on seven models.

**13. SPUCNT bit 6 does not clear itself when IRQ9 fires, and the flag outlives the acknowledge by up to one refresh.** After an IRQ SPUCNT still reads C040h and SPUSTAT 0040h. Writing bit 6 to 0 clears SPUSTAT bit 6, but a poll still sees it set for 581-767 cycles after the write. Disagrees with pcsx-redux's nugget spu-offvoice test, which asserts that SPUCNT bit 6 clears itself (its premise is psx-spx's wording; no console log was behind it).
Evidence: spu-rearm CLR column, 154 observations in 14 runs (1001 3, 5501 4, 7501 3, 7502 1, 9001 3); spu-curvol IRQ rows, 21 runs on six models (the 7501's three have a dead capture and no IRQ, finding 33).

**14. A halfword read from SPUSTAT, SPUCNT or a voice volume register costs about 19 cycles, and the next 16 instructions run free behind it.** Per iteration over the empty loop: SPUSTAT 18.94, SPUCNT 18.95, voice 0 volume 18.92 cycles. Up to 16 nops add nothing (19.11 at 16); each nop beyond adds one cycle. Two SPUSTAT reads in a row cost 39.99. Identical to the hundredth on every model.
Evidence: spu-read-grid, 15 logs (1001 1, 5000 1, 5501 2, 7001 2, 7501 4, 7502 1, 9001 4).

**15. Clearing SPUCNT bit 15 turns every voice off and discards a Key On.** Voice 0 held at ENVX 7FFFh: bit 15 cleared, ENVX still reads 7FFFh immediately and 0000h 20 ms later. A Key On written during the clear is lost: ENVX is still 0000h 5 ms after bit 15 is set again.
Evidence: spu-regs, 48 runs, identical (1001 6, 5000 6, 5501 9, 7001 6, 7501 10, 7502 1, 9001 10).

**16. A fixed voice volume reaches the current-level register two samples after it is written, and the register reads twice the volume.** 1F801E00h + N x 4 (left, then right) returns the old level straight after the volume write and 2 x the new value two samples later: 1234h gives 2468h, 3FFFh gives 7FFEh, 4000h gives 8000h. A keyed-off voice does the same. A sweep of 8020h from 0 steps +0038h per sample (0038h, 0070h, 00A8h, ...) on a playing voice and on a keyed-off one, with the SPU IRQ off or on.
Evidence: spu-curvol FIXED and SWEEP rows, 24 logs, byte-identical (1001 1, 5000 6, 5501 3, 7001 6, 7501 3, 7502 1, 9001 4). nocash's console showed the same 2x in its initial register list (psx-spx).

**17. A write to a current-level register or to ENVX is read back at once and becomes the level the hardware steps from.** Writing 1E04h (voice level) or 1DB8h (main level): with a fixed volume the register reads the written value (1234h) immediately and the fixed level again from the next sample; with a sweeping volume it reads 4000h and the sweep goes on from there (4038h, 4070h, ...). Writing a voice's ENVX: with the envelope held, ENVX 2000h reads 2000h for all 48 samples watched and the capture ring drops from 1FDFh to 0FF0h; with a stepping envelope (-16 per sample) ENVX 7000h reads 7000h, 6FF0h, 6FE0h, ... and the ring jumps from about 0A05h to 37C8h. A voice with linear attack shift 0, decay shift 0 and sustain level Fh that reaches 7FFFh settles at ENVX 3FFFh, so one decay step is taken before the sustain level is compared.
Evidence: spu-curvol VWRITE, MWRITE and ENVX rows: current-level rows and ENVX readings in 24 logs; the ring in 21 (the 7501's ring was dead, finding 33).

---

## SPU interrupt (IRQ9)

**18. IRQ9 timing by trigger.** Times are CPU cycles after the action:

| Action | IRQ9 after | Note |
|---|---|---|
| Manual write over the IRQ address | 48-64 | no IRQ with the address 20h bytes past the end of the write |
| Transfer address set onto the IRQ address | 24-32 | 520-528 in 2 runs |
| IRQ address set onto the transfer address | 152-288 (34 runs), 400-560 (3), 800-896 (20) | two groups about a sample apart |
| IRQ9 enabled with the two already equal | 576-768 (54 runs), 24-32 (3) | |
| IRQ address set onto the first half of a playing voice's block | 200-880 (median 392) | |
| the same, second half, voice at pitch 0001h | 10,885,816-10,887,104 | about 4 source samples after the Key On (the address was set 50 ms after it) |
| two addresses that differ (control) | none | |

So a "late" set is noticed within a sample or so, not instantly and not never.
Evidence: spu-irq, 57 runs (1001 6, 5000 7, 5501 11, 7001 6, 7501 14, 7502 1, 9001 12).

**19. An IRQ9 on a block's start fires 6 source samples before the block is heard, and steps with the Key On.** In voice 1's capture ring the first sample off the previous block's level comes 6.0 (5.9-6.1) output samples after IRQ9 at pitch 1000h and 3.0 (2.9-3.1) at 2000h, at every Key On phase. That is 6 source samples at both pitches. Moving the Key On write across the class A step (finding 23) moves the onset and IRQ9 by 2 samples together in all 44 stepped sweeps of 82 (the other 38 never step).
Evidence: spu-lead-phase, 41 runs x 2 pitches (1001 6, 5000 6, 5501 7, 7001 6, 7501 8, 7502 1, 9001 7).

**20. A voice's decoder reads the next half-block at fixed distances from its first read.** With IRQ9 set on each half-block in turn before the Key On, IRQ9 fires after this many source samples past the first read (block 0 first half):

| half-block | 1008h | 1010h | 1018h | 1020h | 1028h |
|---|---|---|---|---|---|
| pitch 0001h | 4.00 | 20.00 | | | |
| pitch 0010h | 4.01 | 20.01 | 32.00-32.01 | 48.01 | 60.00-60.01 |
| pitch 0040h | 4.07 | 20.05 | 32.04-32.05 | 48.04-48.07 | 60.04-60.07 |

Gaps of 4, 16, 12, 16, 12: each later gap equals the number of samples in the half-block read before it (12 for a first half, 16 for a second). Higher pitches add a pitch-dependent offset to the first read (0100h: 4.28/20.22/32.17/48.17/60.17; 0400h: 4.77/20.62/32.52/48.42/60.42). A decoder that fetches each half-block 8 source samples before its first sample is needed would show this pattern; 8 is also the lead in pcsx-redux's flag_end_mute golden, a silicon capture made by that project. Values agree across models to 0.13 sample.
Evidence: spu-decode-ahead, 42 runs (1001 6, 5000 6, 5501 7, 7001 6, 7501 8, 7502 1, 9001 8).

**21. A keyed-off, silent voice still reads sound RAM and raises IRQ9.** Voice 1 keyed off, ENVX 0000h, IRQ address on block 6 of its eight-block loop: IRQ9 after 58,878-58,890 cycles; on block 2: 85,178-85,241 (20 runs; 699 in one SCPH-1001 run). Re-arming 40 samples after the first IRQ of a playing voice fires again a lap later, 140,334-140,400 cycles after (21 runs). Agrees with psx-spx ("all voices are permanently reading").
Evidence: spu-curvol IRQ rows, 21 runs on six models.

**22. nugget's mid-block probe reads two hits then none.** IRQ9 at 2008h, re-armed after each hit, gave 2 hits (first at 7,480-8,297 cycles, second about 2.3k later) and nothing for 0.5 s in 48 of 49 runs; one early run of an older version of the test logged 8 hits. The same test's 64-halfword upload lands only 32 halfwords (finding 1), so the loop-end block never reaches RAM; this fits but was not isolated.
Evidence: spu-lead MIDBLOCK, 49 runs (1001 6, 5000 7, 5501 9, 7001 6, 7501 10, 7502 1, 9001 10).
Open: spu-rearm (blocks uploaded 32 at a time) re-armed one or more samples after the first hit gives a second hit 7.5-8.4k cycles later, not none; what those hits are is not known.

---

## Key On, Key Off and the two-sample phase

**23. A Key On is taken on a two-sample grid, and which phase that grid has varies from run to run.** With a looping DC block on voice 1 and KON written D cycles after the edge (D = 0-768), voice 1's first sound in its capture ring (counted in samples from the half the edge began) reads one of two ways in a run, at both edge directions:

- Class A: 8 for the early writes and 10 for the late ones. The step is two samples and falls 637-735 cycles after the edge (each run brackets it within 31-35 cycles).
- Class B: 9 at every D from 0 to 768 (the step lies outside the swept window).

The class is constant inside a run and across the 96 rows of a sweep. Counts of runs:

| Model | runs | class A | class B |
|---|---|---|---|
| SCPH-1001 | 6 | 4 | 2 |
| SCPH-5000 | 6 | 5 | 1 |
| SCPH-5501 (two units) | 7 | 6 | 1 |
| SCPH-7001 | 6 | 0 | 6 |
| SCPH-7501 | 8 | 3 | 5 |
| SCPH-7502 | 1 | 0 | 1 |
| SCPH-9001 (two units) | 8 | 4 | 4 |

The same two classes show in the reverb (findings 34 and 35), the start-block IRQ (finding 27) and envelope readings (ENVX 96 samples after the edge on a -16-per-sample sustain was 140Fh in 17 of 24 spu-curvol runs and 13FFh, one sample later, in 7). It is not a property of a board: five models showed both. psx-spx (current text) describes the same 2-sample grid with index 8 on some boots and 9 on others, and says the phase is fixed for a given power-on; the logs show it also changes inside a power-on (finding 24).
Open: the 7001 and 7502 showed only class B (6 and 1 runs).
Evidence: spu-latch, 42 runs.

**24. The class is changed by holding 1F801DAC at a transfer type other than Normal, one sample per sample spent there, in a pattern set by the type.** The test reads the class, holds 1F801DAC for about 1,000 cycles at a value, restores 0004h and reads again, in rows of 8 holds:

| Held value | Holds that changed the class |
|---|---|
| 0004h rewritten (0 of 84, five models); 0005h held (0 of 40 on the 5501, 0 of 64 on the 9001) | none |
| 0000h, 0001h, 0002h, 0010h, 000Ch, 000Eh (Fill) | every hold |
| 0006h (Rep2) | every other hold |
| 0008h (Rep4) | 3 in 4 |
| 000Ah (Rep8) | 7 in 8 |

For 0000h holds the outcome depends on the hold's length: none at 100 and 300 cycles; every hold at 600, 900, 2,000 and 2,304; none at 1,200 (1 of 84), 1,536 and 3,072 (84 holds each, five models). Swept in steps of 25: every hold flips up to 1,125 cycles, 62 of 68 at 1,150, 55 of 68 at 1,175, 3 of 68 at 1,200, none from 1,225 (17 runs). That is an odd number of 768-cycle sample edges inside the hold, with the first edge counted 380-430 cycles after the 0000h write. Nothing else tried moved the class: the SPU switched off and on (20,000 cycles), a 512 KB or 1 KB DMA, SPUCNT muted, the reverb enable, an odd number of samples idled (20 runs). On the 7501 six consecutive runs started 9, 8, 9, 8, 9, 8: each run starts where the one before ended. Disagrees with psx-spx (current text), which says bits 2-3 = 0 do not stop the capture pointers on an SCPH-1000: a 0000h hold flipped the SCPH-1001's class at all 64 of its holds of 600, 900, 2,000 and 2,304 cycles.
Open: what moves inside the SPU (the capture position against the two-sample cycle) is not identified.
Evidence: spu-phase (20 runs), spu-phase2 (21), spu-phase3 (17), spu-phase4 and spu-phase5 (5501 5 runs, 9001 8 each); 1001, 5501, 7501, 7502, 9001.

**25. The phase of the SPU's two-sample cycle against the DRAM refresh counter is set at each power-up.** The runner measures it as `spuRefresh` (0-255 cycles). It stays the same for every run of a boot (12 of 12 tests on the 9001: 103; 12 of 12 on the 5501: 231; 12 of 12 on a 7501 boot: 226) and differs between boots. Four consecutive resets of one SCPH-7501 gave 142 (one test), 149 (11 tests), 187 (4) and 226 (12). Across all sessions, counting values one apart separately and leaving out a recurring 227, the values seen number 22 on the 1001, 21 on the 5501, 24 on the 7501, 24 on the 9001, 9 on the 5000, 8 on the 7001 and 4 on the 7502. This is a different quantity from the class of findings 23-24.
Evidence: lock.json from hwtests/runner sessions, all seven models.

**26. Key On timing from the write.** For the fastest linear attack ENVX first reaches 0800h at sample 5, 6 or 7 after the write (13, 28 and 1 of 42 runs) and 7FFFh at sample 8-9. The voice first sounds in the capture ring 8-10 samples after the edge for a write within a sample of it (finding 23). psx-spx (current text) gives 6 or 7 samples to the first nonzero ENVX for a write at the edge. The three values 5, 6, 7 are the run's grid phase.
Evidence: spu-regs ENVELOPE rows, 42 runs (1001 6, 5000 6, 5501 7, 7001 6, 7501 8, 7502 1, 9001 8).

**27. The first IRQ on a voice's start block follows the edge, not the write.** IRQ address on the start block, enabled before the Key On, KON written D cycles after the edge: D plus the cycles from the write to IRQ9 is 2,940-3,074 for every D from 0 to 736 in 10 runs (the flat shape), and in 4 runs (the stepped shape) 2,178-2,313 up to D = 608-640 and 3,750-3,828 from there, a two-sample step. Pitch 1000h and 0040h alike. The two shapes are what a two-sample grid shows from its two phases.
Evidence: spu-kon-touch, 14 runs (1001 3, 5501 4, 7501 3, 7502 1, 9001 3).
Open: spu-decode-ahead's first-read column, with an unsynchronised write on voice 0 at 1000h, reads 824-2,064 cycles after the write, earlier than any spu-kon-touch row (minimum 1,636). The difference is not explained.

**28. Key On does not copy the start address into the repeat address, and a write 4 samples after Key On has no fixed outcome.** Four cases on voice 1 (blocks at 1000h, 2000h, 3000h; level read from the capture ring: 2FCEh = looped to the written 3000h block, 1FDEh = looped to the block-4 loop start):

| Case | Result | Runs |
|---|---|---|
| A: repeat written before Key On, start block a one-shot | 2FCEh, repeat register reads 0202h (the written value, not 0200h) | 42 of 42 |
| B: repeat written 4 samples after Key On, a loop-start block follows | 2FCEh (write wins) or 1FDEh (loop-start flag wins) | 28 and 14 of 42 |
| B: repeat written 20 samples after Key On | 2FCEh | 42 of 42 |
| C: no write (control) | 1FDEh, register reads 0208h | 42 of 42 |

B at 4 samples is a function of the run's key-on phase: the 28 runs where the write won all read 6 in the rate-00h linear attack's 0800h column (finding 26), and the 14 where the flag won all read 5 (13 runs) or 7 (1). Per model, write won/flag won: 1001 5/1, 5000 1/5, 5501 6/1, 7001 5/1, 7501 7/1, 7502 1/0, 9001 3/5. Disagrees with nocash's text ("Key ON ... copies Voice Start Address to Voice Repeat Address"); agrees with psx-spx's current text on that point. The current text also says the cutoff depends only on the time since Key On; here the same 4-sample write falls either side depending on the run's phase.
Evidence: spu-regs REPEAT rows, 42 runs.
Open: the cutoff in samples (a sweep of the write time is not done).

**29. Envelope rates, in samples from the Key On or Key Off write.** ENVX read every 256 cycles, first sample reaching each level, ranges over 42 runs on seven models:

| Setting | Samples |
|---|---|
| Linear attack, rate 20h: 0800h / 7FFFh | 41 / 590 |
| Linear attack, rate 28h: 7FFFh | 2,345-2,346 |
| Linear attack, rate 30h: 7FFFh | 9,366-9,367 |
| Linear attack, rate 38h: 0800h / 4000h | 2,342-2,348 / 18,726-18,733 |
| Exponential attack, rate 20h: 6000h / 7FFFh | 444 / 1,029 |
| Exponential attack, rate 30h: 7FFFh | 16,379-16,384 |
| Decay shift 4 to 0800h (sustain level 0) | 95-96 |
| Decay shift 8 to 0800h | 1,368-1,370 |
| Decay shift 12 to 0800h | 18,174-18,176 |
| Sustain linear decrease, rate 20h: 6000h, 4000h, 2000h, 1000h, 0800h, 0 | 8-9, 9, 136-137, 200-201, 232-233, 264-265 |
| Release linear, shift 8 (key off at sample 40): 0 | 552 |
| Release linear, shift 12: 0 | 8,230 |
| Release exponential, shift 8: 0800h / 0 | 1,401 / 2,466-2,467 |

The fast attack rows carry the key-on phase (rate 00h: 0800h at 5-7, 7FFFh at 8-9; rate 08h: 7FFFh at 14-16); the slow ones are constant to a sample. Exponential attack follows the linear until 6000h, then slows.
Evidence: spu-regs ENVELOPE rows, 42 runs.

**30. Key Off acts within a few samples.** A key-off written at sample 40 of a voice at 7FFFh with the fastest release (shift 0, linear) takes ENVX below 6000h by sample 40-41 and to 0000h by sample 42-44.
Evidence: spu-regs release rows, 42 runs.

---

## Where a register write takes effect

| Register | Takes effect | Finding |
|---|---|---|
| Key On (1F801D88h) | on the next grid sample; the grid step lies 637-735 cycles after the edge (class A) | 23 |
| Reverb master enable (SPUCNT bit 7) | at 469-602 cycles after the edge, one half-cycle at a time | 36 |
| SPUCNT mode bits into SPUSTAT | next refresh, about 700 cycles after the edge | 12 |
| Fixed voice volume into 1E00h+ | read back two samples later | 16 |
| IRQ address or IRQ enable set onto a location already reached | seen within a sample or so | 18 |

---

## Capture buffers

**31. Voice 1's capture ring is 512 samples at 0800h, written after the envelope and before the volume, in step with SPUSTAT bit 11.** Bit 11 reads 1 while the second half (halfwords 100h-1FFh) is written and 0 while the first (000h-0FFh) is. Ring values are the sample times the envelope: a DC block of 4000h gives 3FBFh at ENVX 7FFFh and 1FDFh at 3FFFh, and with voice volume 3FFFh set the ring still holds 99.6% of the DC level, so the volume is not applied in the ring. A single 7000h ADPCM sample at pitch 1000h and full envelope shows in the ring as four consecutive values: FFFFh, 10A5h, 4E7Bh, 106Dh. The CD's left channel is captured at 0000h.
Evidence: spu-reverb-in voice-ring dumps (48 runs), spu-curvol ENVX rows (21 runs), spu-latch (42 runs), cd-xa (CD ring at 0000h).

**32. 1F801DBCh-DBFh, 1E60h-1E7Eh and 1DA0h hold what is written; their start contents differ per unit; 1E80h-1E86h read FFFFh.** 0000h, FFFFh, 5A5Ah and A5A5h written to each read back unchanged. 1E80h-1E86h read FFFFh before and after a write of 1234h. 1DA0h reads 0000h (AC00h on the SCPH-1001 and on the first 7501 run). 1DBCh/1DBEh at entry are constant over every run of a unit and different between the nine units: 1001 B99F 7CC9; 5000 BFFB F3BF; 5501 767F D272 and DFDF BFFD; 7001 BD91 D5DF; 7501 C84D B8D2; 7502 B80D 3E4D; 9001 7ABA 7F4B and 5277 5BD6. psx-spx lists 80 21 4B DF on nocash's console.
Evidence: spu-curvol UNKNOWN rows, 24 logs, plus spu-unk-1dbc and spu-unk-kill (40 logs).

**33. On one SCPH-7501 any write to 1F801DBCh stopped voice 1's capture until a RESET.** Writing its own value (C84Dh, read back unchanged) was enough; ENVX kept reading 7FFFh, and spu-curvol on that unit then saw no IRQ from voice 1 over its IRQ address (3 logs). It was found by a write in three tests on two days, and the capture was dead at the start of three further runs. The other eight units (1001, 5000, 5501 x2, 7001, 7502, 9001 x2) kept their capture through every write in 40 tests.
Evidence: spu-unk-1dbc and spu-unk-kill, 6 logs on the one 7501 (dead) and 40 on the others (alive).
Open: whether this is the 7501 model or the SPU of one unit; the 7502 (same board generation) was not affected.

---

## Reverb

**34. The reverb's left and right halves run on alternate 44.1 kHz samples.** With only the input stage active (vIIR 7FFFh, vLIN = vRIN = 8000h, walls, combs, all-passes 0) the work area records the input filter's response. The impulse of finding 31 gives:

```
left lines  steps 16-34: 0001 ffff 0006 ffeb 003f ff5d 017a fcd0 0717 DF26 DF41 0717 fcd0 017a ff5d 003f ffeb 0006 ffff
right lines steps 17-33: 0001 fffd 0009 ffeb 002e ffa3 00d6 fc49 CE6D fc5f 00ce ffa6 002c ffec 0008 fffe 0001
```

An impulse one 44.1 kHz sample later swaps them: left gets the single peak (CE6Dh) and right the two-peak response. A DC input of 4000h settles every line at C049h (left reaches it at step 24, right at 22). Step numbers are one higher in class B runs (DF26h at step 26 instead of 25). The values are identical on every run and model. pcsx-redux's reverb commit reports the alternation fitting an I2S capture off an SCPH-1001.
Evidence: spu-reverb-in, 48 runs (1001 6, 5000 6, 5501 9, 7001 6, 7501 10, 7502 1, 9001 10): class A 25, class B 23.

**35. psx-spx's Studio Small preset leaves one of two work-area dumps, a reverb step apart.** An impulse on voice 0 stopped 1,000.5 reverb steps later and the work area read back: 1,033 non-zero halfwords (checksum 533F44A7h) or 1,031 (B0A72EA3h). The 1,033 dump was seen in 26 of 48 runs and the 1,031 dump in 22. In the 42 runs of the final test version the voice's capture-ring onset lies at 053h/153h with the 1,033 dump (22 runs) and at 054h/154h with the 1,031 dump (20 runs). The 7001 and 7502 gave only the 1,031 dump, as in finding 23.
Evidence: spu-reverb, 48 runs (1001 6, 5000 6, 5501 9, 7001 6, 7501 10, 7502 1, 9001 10).

**36. The reverb master enable (SPUCNT bit 7) is taken at 469-602 cycles after the edge.** The first write of a side's lines moves one step later when the enable is written past that point. In class A runs this shows in the left lines, in class B in the right ones (the other side's point lies a sample away, outside the swept window). In all 42 runs the step was bracketed between 469 and 602 cycles after the edge, at both edge directions, each run within 31-33 cycles.
Evidence: spu-latch REVERB rows, 42 runs (1001 6, 5000 6, 5501 7, 7001 6, 7501 8, 7502 1, 9001 8).

**37. The reverb's address pointer runs and raises IRQ9 whatever SPUCNT bit 7 says.** A 64 KB work area at 70000h and an IRQ address of 78000h gave IRQ9 24,808,960-24,809,920 cycles after mBASE was written with the reverb enabled. That is 16,152 steps of 1,536 cycles (the pointer advances 2 bytes per step), the pointer 1D0h bytes short of the IRQ address, the furthest tap in that layout. With bit 7 clear the same IRQ came 32 cycles later in 51 runs that started with the pointer already there, and one lap later (50,046,536-50,050,960 cycles) in six runs on the 5501, 7501 and 9001 that printed a line first. Agrees with psx-spx that reverb accesses trigger IRQ9; it does not say which access.
Evidence: spu-irq REVERB rows, 57 runs enabled and 57 disabled (1001 6, 5000 7, 5501 11, 7001 6, 7501 14, 7502 1, 9001 12).

---

## CD audio input

**38. An XA sector with one loud sample plays back as a fixed ringing response, in one of several alignments.** A 37,800 Hz sector of constant value 16 with one sample of +28,672 plays 4,702-4,707 samples through the CD capture ring. Two alignments are seen (neighbours of the peak):

```
alignment 1: ... 755 -1169 1816 -2331 2837 [27152] 2824 -2346 1803 -1183 741 -371 156 -65 57 -30 14 -7 3 -1
alignment 2: ... 316 -219 -46 1125 -3951 [23204] 13443 -5147 2642 -1287 602 -199 46 -20 37 -14 5 -1
```

Alignment 2's peak is 23,199 on some runs. The base before the impulse is 13. An 18,900 Hz sector plays 9,406-9,407 samples, peak 26,294-26,296 (27,343 on two logs), with ringing across all 49 samples recorded. Both alignments appeared on the 5501, 7501 and 9001; the 1001 and 7001 gave alignment 1. The alignment differs between sessions on the same console (5501, 9001).
Evidence: cd-xa, 11 logs (1001 1, 5501 2, 7001 2, 7501 3, 9001 3), on cd-xa's own disc.

**39. XA audio starts about 0.8 sector after the sector arrives, ADPBUSY lasts as long as the playback, and a sector that arrives mid-playback is dropped.** The first sample reaches the capture ring 82,888-115,802 cycles before the INT1 of the data sector that follows the audio sector (70 sectors; a sector at 1x lasts 451,584). ADPBUSY (1F801800h bit 2) is set for 3,612,588-3,612,724 cycles for one 37,800 Hz sector (4,704 samples x 768 = 3,612,672) and 7,225,339-7,225,393 for an 18,900 Hz one (9,408 x 768), and falls 10,168-10,307 cycles (13-14 samples) before the last sample. Twenty silent 37,800 Hz sectors arriving every second sector time kept ADPBUSY set for 18,063,285-18,063,417 cycles, which is 5 playbacks (5 x 3,612,672 = 18,063,360): a sector that arrives while the previous one is still playing is dropped, not queued.
Evidence: cd-xa (70 usable sectors: 1001 4, 5501 13, 7001 19, 7501 18, 9001 16) and cd-adpbusy (5 logs: 5501 1, 7501 1, 9001 1, 7001 2; the 1001's disc did not read).

**40. The capture ring is before the SPU's CD input volume, and the drive's own volume matrix scales it.** Setting the CD input volume (1F801DB0h) to 4000h instead of 7FFFh left the captured response unchanged (peak and neighbours equal in 9 logs on five models). The drive's matrix volume changes it: at 40h the peak is half of 80h (11,601 against 23,204; 13,575 against 27,152; 11,599 against 23,199); at 7Fh it is 23,022 (x0.992); at 90h it is x1.125 (26,104 against 23,204); above that it falls short (A0h 28,318 against 29,005 expected, C0h 30,709 against 34,806) and at FFh it clips at 32,767 with a reshaped response.
Evidence: cd-xa, 9 logs for the input volume (1001, 5501 x2, 7001 x2, 7501 x2, 9001 x2) and at least 6 for the matrix on four models (5501, 7001, 7501, 9001).
