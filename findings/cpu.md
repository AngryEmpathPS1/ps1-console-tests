# The CPU and its bus

The R3000A as the PlayStation uses it: loads, stores and the write queue, the instruction cache and cold code, the DRAM behind them and its refresh, on-die and slow device registers, uncached code, exceptions, DMA and timers as the CPU sees them, and GTE timing. Everything here was measured on retail consoles with the programs in `hwtests/`, plus a few from [ps1-tests](https://github.com/JaCzekanski/ps1-tests) by Jakub Czekański, credited where used. Logs are in `consoles/`, by console and session.

## How to read the numbers

- **Cycles** are CPU cycles (33.8688 MHz), taken as the difference of two reads of Timer 2 counting the system clock, interrupts off. Two back-to-back reads with nothing between are 4 apart on every model (counter-bracket), so every bracket carries 4 cycles of its own.
- **"Above the nops"** means the loop's cost with the instruction minus the same loop with a nop in its place, per instance. An ALU instruction costs 0.
- **Warm** code is in the instruction cache. **Cold** code is code where every 16-byte line is a miss. **Uncached** code runs through KSEG1. **On-die registers** are I_STAT, I_MASK, the timers, the DMA registers, GPUSTAT/GP0, the MDEC status and the controller/serial ports. **Slow** devices are the CD controller, the SPU, the BIOS ROM and the expansion regions.
- **The DRAM refresh** falls due every 256 cycles and changes any row it lands in. Where a program prints "fastest..slowest of N", the number quoted is the undisturbed (most common) value unless stated. Programs started through the small resident `hwtests/runner` begin at a fixed refresh phase, so their output repeats run to run.
- **Models:** SCPH-1001 (BIOS 2.1, CPU revision register 1), SCPH-5000 (BIOS 2.2 J), two SCPH-5501 units (BIOS 3.0), SCPH-7001, 7501 and 9001 (BIOS 4.1), SCPH-7502 (BIOS 4.1 E, PAL). All but the 1001 read revision 2. "All seven models" means these, with both 5501 units counted as one model; "unit 2" is the second 5501.
- psx-spx (nocash's document, and its newer revision where noted) is cited where the logs agree or disagree with it.

## Do models differ?

**CPU-1. In the CPU, bus, cache, refresh and GTE programs the models print the same numbers.** Of 83 programs in this area that ran on more than one model, 34 printed identical output (ignoring the BIOS banner lines) in every run on every model, both 5501 units included, 10 to 31 runs each: for example line-fill2, seq-miss2, miss-grid, cold-branch, fix-shadow 1-3, io-pairs, io-shadow, sw-ioread 2-4, hold-read, st-land, t2-path, icache-tagmatch, unc-entry, gte-irgb, gte-sqr-align, gte-hold, cpu-loop-tick 1-4. Another 26 to 29 were identical in at least 90% of runs, the odd run being the second 5501 unit or an early session and differing by hundredths in refresh-dependent rows. The other 20 print 100-pass totals that move in the last digit with the refresh. The one model difference found is CPU loads during a GPU DMA on the SCPH-1001 (CPU-43). Programs whose result depends on the video or DMA phase (timer sync, DMA timing) vary run to run on every model.
Evidence: the programs named in the findings below; all seven models; 10-31 runs each.

## Loads and the load shadow

**CPU-2. A load from RAM does not stall the CPU for its whole latency. Instructions that neither read the loaded register nor use the bus run inside it.** In a warm loop a RAM `lw` whose value is never read costs 2.0 cycles above a nop. Reading the value soon costs more:

| first reader of the loaded register (instruction number after the load; 1 is the delay slot) | cost above a nop |
|---|---|
| 2 | 6.0 (6.02-6.13) |
| 3 | 6.0 (6.07-6.11) |
| 4 | 5.1-5.3 |
| 5 | 4.0 (4.00-4.15) |
| 6 | 3.0 (3.01-3.12) |
| 7 | 2.1 (2.06-2.19) |
| none | 2.0 (2.02-2.08) |

A reader in instruction 1 sees the old value and adds nothing. A branch in the shadow, taken or not, does not end it (2.04-2.08 for a branch at any of positions 2-7). If the next RAM load comes only 5 instructions after the first, each costs 3.2; from 6 instructions apart each costs 2.1. A scratchpad load has no stall: alone it costs 0.00.
Evidence: load-use (READER, SHADOW), load-pairs (BRANCH, TAKEN, SCRATCH); all seven models; 31 and 40 runs.

**CPU-3. Two RAM loads close together cost much more than two lone ones, and the second costs most one nop apart.** Two loads into different registers, k nops apart, above the same number of nops:

| k | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| cycles | 7.28 | 8.22 | 7.22 | 6.25 | 5.32 | 4.21 | 4.23 | 4.27 |

Into the same register (k = 0-3): 9.15, 9.10, 9.21, 8.27. The second load from a page 4 KB away costs the same (7.21, 8.28, 7.17, 6.27), so no DRAM page effect shows. A scratchpad load k nops behind a RAM load (k = 0-4): 5.22, 6.21, 5.02, 4.14, 3.23.
Evidence: load-pairs (PAIRS, SAMEREG, FARPAGE, SCRATCH); all seven models; 40 runs, rows within 0.1 of each other.

**CPU-4. A RAM load behind a RAM store waits for the store to leave the write queue. There is no forwarding, and it makes no difference whether the load reads the stored word.** `sw; k nops; lw` of the same word, k = 0-8: 7.40, 6.38, 5.07, 4.26, 3.31, 2.29, 2.28, 2.09, 2.14 above the same number of nops. A load of a different word at k = 0 costs 7.27 against 7.19 for the same word (SCPH-1001). In store-cost's loop `sw; lw` of the same word, of a word far away, and with a nop between all cost about 10.2.
Evidence: load-pairs (STORELOAD), load-use (DRAIN), store-cost (WRITE BUFFER); all seven models; 40, 31 and 31 runs.

**CPU-5. A store behind a RAM load waits for the load's bus access. It is not hidden in the load's shadow.** With `lw t1` followed by a store of another register at instruction p behind it (the load alone costs 1.97):

| p | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| RAM store | 5.2-5.4 | 6.1 | 5.1-5.2 | 4.1-4.3 | 3.1 | 2.2 | 2.1 |
| scratchpad store | 5.2-5.3 | | 5.1-5.2 | | 3.0 | | |
| RAM store after a BIOS ROM `lbu` (alone 2.05) | 7.1 | 8.0 | 7.0 | 6.1 | 5.1 | 4.0-4.1 | 3.0 |

Storing the loaded register itself at p = 1 costs 6.05. A scratchpad store waits as long as a RAM store, so the wait is the pipeline's, not the DRAM's.
Evidence: store-shadow; all seven models; 16 runs.

**CPU-6. After a load from an on-die register, a writer of the same destination register in instruction 1 or 2 pays the wait a reader at 2 would. A reader in the delay slot (instruction 1) does not.** `lhu v0, Timer2` followed by a reader in instruction 1 costs 7.07 cycles an iteration of a short loop, in instruction 2 10.05, in 3 11.06 (one more nop each). A pure writer (`addiu v0, zero, 1`) in 1 costs 9.05 and in 2 10.05. `andi v0, v0` (reads and writes) in 1 costs 9.05 and in 2 10.05. The same shapes after an I_STAT read (9.05) and after a word read of Timer 2 (9.06) agree. Cold and hot passes agree to 0.05.
Evidence: cpu-loop-tick3; SCPH-1001, 5501 (both units), 7501, 7502, 9001; 10 runs, identical.

## Multiply, divide and partial-word accesses

**CPU-7. `mult`/`multu` followed by `mflo` costs 6, 9 or 13 extra cycles by the size of the first operand; `div`/`divu` always 36.** For `multu rs, rt` the extra cycles are 6 for rs below 800h, 9 for 800h-FFFFFh and 13 above. rt does not matter (`multu 3, FFFFFFFFh` is 6, `multu FFFFFFFFh, 0` is 13). For `mult` the same limits apply to the magnitude: rs from -800h to 7FFh is 6, -100000h to -801h and 800h to FFFFFh is 9, anything else 13. Every `div`/`divu` case printed 36, including division by zero, FFFFFFFFh/1 and 80000000h/-1. The test's "latency" column is the extra plus 1 (7, 10, 14, 37). Agrees with psx-spx (the newer text; nocash's and the stable signed ranges are off by one at FFFFF801h and FFF00001h).
Evidence: cpu-extras; all seven models, both 5501 units; 31 runs, identical in every run.

**CPU-8. `mflo`/`mfhi` one cycle before the result is ready does not wait. A second multiply or divide replaces the running one without waiting. `mtlo`/`mthi` abort the wait.** With K nops between a multiply and `mflo`, the extra cost falls 1 per nop and then steps from 2 straight to 0. `multu 3,3`: 5.99, 4.99, 3.99, 2.98, 1.99, 0.00 (K = 0-5). A medium multiply (rs = 1234h) reaches 0.00 at K = 8. `divu` 35.99 down to 1.99 at K = 34 and 0.00 from K = 35. A multiply or divide that nothing reads costs nothing (one in six instructions: 0.00). Started while another is running, a multiply or divide takes over. `multu slow; multu fast; mflo` costs the fast one's 5.99 and reads the fast result, `divu; multu; mflo` costs 5.99, and `mult slow; div; mflo` costs 36.03. `multu; mtlo x; mflo` costs 0.00 and reads x (`mthi`/`mfhi` the same). The other half is left with a partial value: after `mult slow; mtlo`, HI reads 43C3C3C3h where the product's high half is 1FE3A76Ah. psx-spx describes the same.
Evidence: cpu-extras (SWEEPS, IN FLIGHT), muldiv-overlap; all seven models, both 5501 units; 31 and 16 runs.

**CPU-9. `swl`/`swr`/`lwl`/`lwr` cost the same as `sw`/`lw`: no read-modify-write shows in the timing.** Above a nop: `sw` 1.01-1.03, `swl` and `swr` at any byte offset 1.01-1.13, `sb` 1.01-1.13, `sh` 1.01-1.03. Loads from RAM: `lw` 5.05-5.10, `lwl`/`lwr` 5.04-5.16. To the SPU a word store, `swr +0` and `swl +3` of the same word all cost 10.39-10.44. To Expansion 2 all three cost 57.92.
Evidence: store-cost; all seven models, both 5501 units; 31 runs.

**CPU-10. `lwc2` from RAM is a load with no shadow.** Eight `lwc2` from RAM back to back cost 6.15 above a nop each (`lw`: 5.02). From the scratchpad both cost 0.00. One `lwc2` followed by K = 0-6 independent ALU instructions costs 6.12, 6.07, 6.10, 6.09, 6.09, 6.05, 6.08 (no shadow), where `lw` falls 4.86, 5.38, 4.37, 3.61, 2.78, 2.08, 2.09.
Evidence: cpu-extras; all seven models, both 5501 units; 31 runs.

## Instruction cache and cold code

**CPU-11. A cache miss costs 5 cycles per 16-byte line: a cold line of four instructions takes 9 cycles, a warm one 4.** The cost does not depend on the word entered. A `jal` to word 0, 1, 2 or 3 of a cold line and a fall-through into it all add 5. Two cold lines back to back add 10, and a cold line followed by a second cold line one step on adds 13. A call over N cold lines of nops, cold minus warm: N = 1 5, 2 10, 4 20, 8 40, 16 80, 32 164, 64 328, 128 656 (the extra 4 per 32 lines is a refresh, see CPU-26). Over 255 lines the cold call takes 2357 cycles and the warm one 1058, on every model. The long-run slope is 9.1-9.3 cycles a line. In 2048 straight-line nops (512 lines) the cold block takes 4716 cycles, the warm block 2066, an uncached block 12580. The 9-cycle fill is the figure a Sony development-board bus capture also gave; the test was written to tell 8 from 9 on retail boards.
Evidence: line-fill (31 runs), line-fill2 (21), seq-miss2 (21), load-fill (16); all seven models; line-fill2 and seq-miss2 identical in every run.

**CPU-12. A taken branch in a line that is still being fetched waits for the rest of the line.** In a cold line entered by `jal`, a taken branch at word 0 raises the miss cost from 5 to 8, at word 1 to 7, and at words 2 and 3 leaves it 5. Entered by a fall-through the figures are the same (8, 7, 5, 5). With a second cold line first, 13, 12, 10, 10. `beq`, `bnez`, `j`, `jr` and `jr ra` behave alike.
Evidence: miss-grid, cold-branch; all seven models; 21 runs each, identical.

**CPU-13. In cold code a RAM load costs 4.15 above a nop at every word of its line, an on-die register load costs nothing at words 0, 2 and 3 with no reader behind it, and a store costs 5.66 (5.48 on unit 2).** Warm, the same RAM load costs 2.00. One load per 16 instructions in 8 KB of cold code: RAM at word 0-3 4.14-4.18; I_STAT 0.00 at words 0, 2 and 3 and 1.09 at word 1. Other shapes, per unit:

| shape | cost |
|---|---|
| RAM load, reader 2 or 3 instructions on | 7.15 |
| RAM load, reader 5 or 9 instructions on | 4.14-4.15 |
| two loads in adjacent words | 10.24 |
| two loads two lines apart | 8.36 |
| lone `sw` | 5.66 |
| `lw` at word 0 plus `sw` at word 2 | 12.19 |
| scratchpad load | 0.00 |
| I_STAT load with a reader | 3.85 |
| SPU register read | 14.75 |

Executed through KSEG1 a lone RAM load costs 4.30 above nops and an I_STAT load 0.00.
Evidence: load-fill (16 runs), load-fill2 (21); all seven models; load-fill2 identical across models.

**CPU-14. The bus-interface register at FFFE0130h changes cold-code timing.** With load scheduling off (bit 16) a load no longer overlaps what follows it. With streaming off (bit 17) every cold instruction costs one cycle more. With a two-word refill (bits 9:8 = 0) cold code costs 55% more. The BIOS leaves 0001E988h. Each bit was flipped alone from uncached code and restored before printing, with no hang:

| FFFE0130h | cold nops, 2048 | RAM load, word 0/1/2/3 | I_STAT load, word 1 | warm RAM load | `lw; 4 nops` bracket |
|---|---|---|---|---|---|
| 0001E988 (as left) | 4716 | 4.15 4.15 4.14 4.16 | 1.08 | 2.00 | 12 |
| 0001E888, IBLKSZ 0 | 7340 | 4.21 4.21 5.26 5.23 | 1.11 | 2.00 | 12 |
| 0003E988, NOSTR 1 | 6773 | 3.10 4.33 -0.01 1.77 | 1.37 | 2.00 | 12 |
| 0000E988, LDSCH 0 | 4720 | 7.15 6.09 9.10 8.12 | 3.84 | 6.19 | 16 |

With load scheduling off a warm RAM load costs 6.19 above a nop instead of 2.00, and every nop after it adds a cycle (`lw; k nops`: 12, 13, 14, 15, 16, 19 for k = 0, 1, 2, 3, 4, 7, against 10, 12, 12, 12, 12, 14). The flat 4.15 of CPU-13 is therefore the load overlap, not a property of the line fill. Disagreement: the newer psx-spx says a RAM load followed by four nops costs "about 2 more" with LDSCH off. The measurement is 4 more (12 against 16, and 2.00 against 6.19 in the warm loop). Streaming off was measured in cold cached code only, so psx-spx's statement about uncached fetch was not tested. pcsx-redux's cache probe warns that RDPRI, NOPAD, BGNT and LDSCH must be preserved; clearing LDSCH from uncached code here caused no hang.
Evidence: biu-sched; SCPH-5501 unit 2 only; 2 runs, identical. Open: one unit; RDPRI, NOPAD, BGNT and INTP were not touched.

**CPU-15. Clearing RAM_SIZE bit 7 (1F801060h) takes 1.3 to 2.9 cycles off a cold RAM access: 2.3 off a load at words 0, 2 and 3, 1.3 at word 1, 2.9 off a store, and 3.2 off a load plus a store together.** The register as the BIOS leaves it is 00E00B88h; bit 7 clear is 00E00B08h:

| cold-code row | bit 7 set | bit 7 clear |
|---|---|---|
| RAM load, word 0/1/2/3 | 4.14 4.15 4.15 4.16 | 1.86 2.82 1.86 1.86 |
| I_STAT load, word 1 | 1.11 | 0.80 |
| `sw` at word 0 | 5.73 | 2.88 |
| `lw` word 0 plus `sw` word 2 | 12.19 | 9.03 |
| warm RAM load | 2.00 | 2.00 |

With load scheduling also off: RAM load 7.15 6.07 9.11 8.13 (bit set) against 6.13 6.09 7.95 6.95 (bit clear). The warm store-then-load brackets move by one cycle (24 to 23, 23 to 22). psx-spx describes the bit as one idle cycle between consecutive RAM accesses; the saving per cold load or store here is more than one cycle.
Evidence: biu-dram; SCPH-5501 unit 2 only; 2 runs, identical. Open: one unit; the mechanism is not known. psx-spx reports that clearing the bit hangs CD loading on early boards; the test was not run around CD loading.

## The write queue

**CPU-16. Four RAM stores are free, a fifth costs about a cycle, and after that the cost rises two cycles every second store.** N stores to consecutive words in one 1 KB row, then 48 nops, above the same loop with nops, N = 1-12: 0, 0, 0, 0.05, 0.84, 2.05, 2.26, 4.90, 4.00, 6.39, 6.67, 8.69. With one nop between the stores: 0.00 to N = 4, 0.03 at N = 5, 0.89 at N = 6, then 0.42 at most. With two nops between: 0.3 at most, except 1.23 at N = 11. Halfword stores to consecutive halfwords read the same (0.89, 2.05, 2.24, 4.90 for N = 5-8). Agrees with psx-spx's "4 words deep".
Evidence: wb-drain; all seven models; 16 runs, identical on every model.

**CPU-17. A burst of stores to one 1 KB DRAM row costs 2 cycles a word after the first. A store that starts a new burst costs about 5.** Stores 4 KB apart, N = 5-8: 0.87, 6.89, 11.53, 16.31. Stores 1 KB, 2 KB or 4 KB apart cost the same (1 KB, N = 5-12: 0.87, 6.88, 11.53, 16.31, 22.31, 26.55, 31.89, 36.91). Stores anywhere in one 1 KB-aligned block queue as one burst: the same word, consecutive halfwords, or words 8 to 128 bytes apart all read like consecutive words. A burst does not cross a 1 KB boundary. With the fifth of N consecutive words crossing a boundary, the cost of the new burst shows when the queue is full, at N = 9. It is 9.8 for a 1 KB, 2 KB or 4 KB boundary against 4.0 with no crossing; 256-byte and 512-byte boundaries make no difference. Stores 512 bytes apart pair, two to a row (7.16 at N = 7 against 2.26). Stores 256 bytes apart behave like a crossing at the fifth store (9.82 at N = 9).
Evidence: wb-page, wb-page2, wb-drain; all seven models; 21, 21 and 16 runs, identical apart from the second 5501 unit (hundredths).

**CPU-18. A read already waiting when the bus becomes free between two queued stores goes first. It does not wait for the whole queue.** Reading GPUSTAT after n stores with no nops between (`lhu T2; n stores; lw GPUSTAT; lhu T2`, most common of 16 trials over the refresh):

| n | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|
| all stores in one 1 KB block | 13 | 15 | 17 | 19 | 21 |
| stores alternating between two blocks 1 KB apart | 13 | 13 | 19 | 19 | 25 |
| stores alternating between pages 4 KB apart | 13 | 13 | 19 | 19 | 25 |

A Timer 1 read behaves like the GPUSTAT read. The read goes ahead unless a queued store is within its own 64 bytes. After stores to A, B and A+4, a RAM read at A+4, +8, +12, +16 or +32 waits for A+4 (28 cycles); from A+64 up to A+1020 it goes first (18). At B, B+4 and B+8 it waits (24); from B+64 it goes first (18). A read of another 2 KB, 4 KB, 8 KB or 16 KB away also goes first (18). This may be the CPU core's read-priority switch (RDPRI, bit 13 of FFFE0130h, set by the BIOS); it was not toggled here, and the newer psx-spx reports that toggling it changes none of this.
Evidence: ram-store-io4 to io7; all seven models, both 5501 units; 12-22 runs each, identical but for refresh draws in the fastest-of-16 column.

**CPU-19. A read of an on-die register behind queued RAM stores samples late, as if it waited until the queue had all but its last 2 cycles left. The CPU runs on unless it needs the value.** Two Timer 2 reads with one RAM store between them, N nops after the store: 8, 8, 8, 9, 10, 11, 12, 13, 14 for N = 0-8 (4, 6, 6, 6, 7, 8, 9, 10, 11 with no store). Three stores between: 12 (one store adds 4, each further store of the burst 2). A store to an I/O register (Timer 1's target) between: 6. In sw-ioread a store before a JOY_STAT read costs the CPU nothing if no instruction reads the value (16, 17, 18, 19 for 0-3 nops, as without the store). A reader 1-3 nops on pays 2 more behind a RAM store (21, 22, 22 against 19, 20, 20) and 1 more behind a store to a timer register (20, 21, 21).
Evidence: counter-bracket (39 runs), sw-ioread (31 runs); all seven models.

**CPU-20. A read between two stores to the same DRAM row costs about 3 cycles more than the same read between stores to different rows, and which register is read does not matter.** `sw A; lhu REG; nop; sw A+d` per iteration: same row (d = 8 or 64) 15.1 for Timer 2, I_STAT, JOY_STAT and DPCR alike; second store 2 KB or 16 KB away 12.5. With a nop instead of the read: 7.08 for the same row (one burst), 12.5 for a different row. The read hides behind a store to another row and puts a store to its own row in series behind it.
Evidence: cpu-loop-tick5, 6, 7; SCPH-5501 (both units), SCPH-9001; 5 runs each, identical.

**CPU-21. Stores to GP0 retire at 2 cycles each after the first few, and a timer write takes effect a couple of cycles after the store.** n stores of a NOP word to GP0, n = 0, 1, 2, 3, 4, 6, 8, 12, 16: 4, 6, 6, 8, 8, 10, 14, 22, 30 cycles between two Timer 2 reads. A store of 0 to Timer 2's counter (or its mode), N nops and a read of the counter reads 0, 0, 1, 2, 3, 4, 5, 6, 7 for N = 0-8 in cached code. From uncached code it reads 4, 10, 16, 22, 28, 34, 40, 46, 52, six cycles per fetch.
Evidence: gp0-store (all seven models, 13 runs, the n-store rows identical in all 13), counter-store (all seven models, 31 runs).

## DRAM refresh

**CPU-22. The DRAM refresh falls due every 256 cycles and holds the RAM bus for 5. A RAM read that meets it waits up to 6.** Sweeping a lone RAM read across the 256-cycle grid, its cost is flat except for one six-step ramp, +6, +5, +4, +3, +2, +1 (21 20 19 18 17 16 over a rest value of 15). A read of RAM_SIZE (1F801060h, the DRAM controller's register) ramps the same way. A polling loop shows the period: `lhu I_STAT; lhu T2; 12 nops; sll; or; sw` takes 25 cycles a turn, and 203 turns in 4096 cost 28 and 203 cost 34, alternating 259 and 253 cycles apart (512 for two refreshes). Agrees with psx-spx's 256-cycle refresh and the zero refresh-period field in the BIOS's RAM_SIZE value.
Evidence: refresh-io (31 runs), refresh-io3 (21), spu-read-trace (43); all seven models; identical apart from the printed phase.

**CPU-23. An I_STAT read on an idle bus never waits for a refresh. Behind a RAM store or RAM read it inherits their delay. Only I_STAT was swept; RAM_SIZE, also on-die, does wait.** I_STAT alone: 13 at all 72 phases swept. I_STAT behind a store: 14 at rest, 23 at one phase, then 19, 18, 17, 16, 15. Behind a `lw`: 19 at rest and 25 down to 20. An SPU register read behind a store: 29 at rest, 38, then 34-30. RAM read alone: 15 at rest and 21 down to 16. RAM read behind a store: 18 at rest (CPU-24). RAM_SIZE alone: 13 at rest, 19 down to 14.
Evidence: refresh-io, refresh-io2, refresh-io3; all seven models, both 5501 units; 31, 21 and 21 runs.

**CPU-24. What a refresh costs a RAM store depends on where the refresh falls due inside the store's own bus transaction.** d is the number of cycles after the store's first bus cycle at which the refresh falls due, and L the burst length in cycles (6 for one word, 8 for two, 12 for four):

| refresh due | cost to what follows |
|---|---|
| d <= 0 (before the store starts) | the store waits: +5 at d = 0, one less for each cycle earlier, down to 0 |
| d = 1 | the refresh runs right behind the first cycle; whatever is behind waits 6 |
| d = 2 to L - 4 | the burst is cut: the refresh (5) plus a new start (5), +10 at even d and +9 at odd d |
| d = L - 3 to L | the burst finishes, then the refresh: +5 to a RAM read behind, +0 to an on-die read |
| d = L + 1 | +6 |

Sweeping `sw A; lw A` (16 at rest) from d = 7 down to d = -5 gives 22, 21, 21, 21, 21, 26, 22, 21, 20, 19, 18, 17, 16. A two-word burst (18 at rest, d = 9 down to -5): 24 23 23 23 23 28 27 28 24 23 22 21 20 19 18. A four-word burst (22 at rest): 28 27 27 27 27 32 31 32 31 32 31 32 28 27 26 25 24 23 22. Stores to two and four different pages show the +10 at each store, 6 phases apart. In a stream of new-row stores a refresh therefore costs about 10 cycles, not 5 (two pages per turn: 12.50 a turn where the bus's own work is 12; four pages 24.98).
Evidence: refresh-store (SCPH-5501 unit 1 three runs, unit 2 one run, SCPH-9001 two runs, identical), cpu-loop-tick8 (SCPH-5501 both units, SCPH-9001); the table also reproduces refresh-io2 (all seven models, 21 runs) and refresh-store2 to 7 (SCPH-5501 unit 2, 3 runs each).

**CPU-25. A refresh can make two stores cheaper.** `sw A; 2 nops; sw A+4; lw A+4` (same row) costs 22 at rest. At the six phases after the refresh it costs 24, 23, 22, 21, 20, 19, so the pair finishes 3 cycles under rest. The refresh delays the first word, and the second store then joins its burst. `sw; nop; sw; lw` (18 at rest) shows the same ramp without the dip. An SPUSTAT read after a store with 0-3 nops costs 28 at rest (29 at 3 nops), 38 at the top phase, then 34 33 32 31 30 29.
Evidence: refresh-store2; SCPH-5501 unit 2; 3 runs, identical. Open: one unit.

**CPU-26. A refresh costs a stream of line fills 4 cycles and a run of back-to-back RAM loads 3 to 6.** Twenty cold lines of nops take 188 cycles when a refresh falls inside and 184 when none does, with 189 or 190 at two phases of each 9-cycle fill. 24 `lw` from RAM back to back take 175 cycles (7.3 each) with no refresh. A refresh adds 3 at most phases and rises 3, 4, 5, 6 over the phases just before the next load starts (period 7). Agrees with psx-spx's 7 cycles per RAM load. With one nop after each load (198 undisturbed) the refresh costs 2 and rises 3, 4, 5, 6.
Evidence: refresh-cold, refresh-warm; all seven models; 21 runs each, arrays identical, only the printed phase differs.

## On-die registers and slow devices

**CPU-27. Cost of one load above a nop, by region and width.** From ps1-tests' `cpu/access-time`, a loop of `nop; addiu; load; bnez; nop`. The program prints the hundredths without a leading zero, so "3.8" in its output is 3.08.

| region | 8-bit | 16-bit | 32-bit |
|---|---|---|---|
| RAM | 5.14-5.24 | 5.07-5.15 | 5.06-5.15 |
| BIOS ROM | 7.06 | 12.94 | 24.94 |
| scratchpad | 1.01 | 1.04 | 0.94 |
| Expansion 1 (1F000000h) | 6.94 | 13.07 | 25.07 |
| Expansion 2 (1F802000h) | 10.99 | 25.99 | 56.02 |
| Expansion 3 (1FA00000h) | 6.07 | 5.95 | 9.96 |
| DMAC_CTRL, JOY_STAT, SIO_STAT, RAM_SIZE, I_STAT, Timer 0, MDECSTAT | 2.95-3.08 | 2.99-3.08 | 2.93-3.21 |
| GPUSTAT | 2.93 | 2.93 | 3.08 |
| CD controller (1F801800h) | 8.00 | 13.99 | 25.93 |
| SPU control (1F801DAAh) | 17.99 | 17.99 | 38.94 |
| cache control (FFFE0130h) | 0.95 | 1.09 | 1.09 |

A range means the loop's window met a refresh in some runs. Every on-die register costs the same, as psx-spx says; the absolute figure depends on the loop around the load.
Evidence: cpu/access-time (ps1-tests); all seven models; 30 runs.

**CPU-28. On-die registers read in the same time: a lone read costs 3.75 cycles above a nop and two back-to-back halfword reads 7.62.** `lhu A; nop` costs 3.75 for Timer 2's counter, mode and target, Timer 0's counter and mode, I_STAT, I_MASK, DPCR, DICR, MADR2, GPUSTAT, JOY_STAT and the MDEC status. `lhu A; lhu B` costs 7.62 for 32 pairings of them.
Evidence: io-pairs; all seven models; 16 runs, identical.

**CPU-29. A read of an SPU register takes about 19 cycles, and the next 16 independent instructions run for free inside it.** `lhu SPUSTAT` followed by k nops costs 18.94 above the empty loop for k = 0-16 (19.11 at 16), then one cycle a nop (20.12 at k = 17, 34.18 at k = 31). SPUCNT and voice 0's volume read the same. In a read-and-timestamp loop SPUSTAT takes 31 cycles a turn at k = 0, 40 at k = 12 and 52 at k = 24. Two SPUSTAT reads back to back cost 39.99, SPUSTAT then I_STAT 20.96, I_STAT then SPUSTAT 19.94.
Evidence: spu-read-grid (15 runs), spu-read-trace (43); all seven models.

**CPU-30. A CD or SPU register read grows with COM_DELAY per byte-wide bus access.** CD status register (1F801800h) with COM0 in use, 8-, 16- and 32-bit reads:

| COM0 | 8-bit | 16-bit | 32-bit |
|---|---|---|---|
| 3 | 7.85 | 15.85 | 31.85 |
| 5 | 7.85 | 17.85 | 37.85 |
| 12 | 13.79 | 30.79 | 64.79 |

SPU 1F801DAAh with COM0 in use: COM0 5 17.98, 18.04, 38.04; COM0 3 17.85, 18.04, 35.85; COM0 12 23.79, 23.98, 50.79. At the kernel's settings (COM_DELAY 00031125h, CD_DELAY 00020843h) the CD register costs 8.04, 14.04, 26.04.
Evidence: mem-delay; all seven models; 31 runs (29 for the SPU rows), within 0.1.

**CPU-31. A BIOS ROM byte load alone costs 1.9 above a nop, two together cost 9, and a nop between them makes it dearer.** `lbu` from BFC00100h alone 1.83-1.89. `lbu; lbu` 8.91-8.99, with one nop between 9.79-9.88, two 8.94, three 7.82-7.86, four 6.86-6.92. A RAM store right behind a ROM load waits for the access: `lbu; sb` of another register 6.92-6.97, of the loaded register 7.86-7.94.
Evidence: slow-b2b; all seven models, both 5501 units; 31 runs.

**CPU-32. A 32-bit read of a 16-bit on-die register shows bus residue in its upper half.** `lw` of I_MASK in cached code after a RAM load of C3C35A00h reads C3C3xxxx, and a scratchpad load, or no load at all, leaves it unchanged. After a store to I_MASK itself it shows 8001 (an earlier word) rather than the stored A5A5. After a store to Timer 2's target the 5A5A data shows through the KUSEG and KSEG1 addresses. In uncached code every cell reads 2400h, the opcode fetched two instructions after the read.
Evidence: imask-upper (31 runs), bus-latch (16); all seven models. Open: the Timer 2 store row at the KSEG0 address reads 5A5A in 12 runs (5 of the first 6 runs, six SCPH-5000 runs, unit 2) and 8001 in the other 19, so it is not stated as fact.

## Uncached code

**CPU-33. Every uncached instruction fetch from RAM costs about 6 cycles.** 2048 nops through KSEG1 take 12580 cycles (6.14 each). The same block cold in the cache takes 4716 and warm 2066.
Evidence: load-fill; all seven models, both 5501 units; 16 runs, 12580 in every run.

**CPU-34. In uncached code a lone RAM load adds 4.3 cycles, a lone store 5.5, and neighbouring accesses combine.** Above nops, one per 16-instruction unit: load 4.28, store 5.48, two stores back to back 7.42 (one burst), load then store right behind 10.42, four loads back to back 19.61, eight 40.05, sixteen 81.84 (about 5.1 above a nop each). Phase-resolved costs of a 24-instruction block with no refresh (24 nops = 160): load 164, store 165, two stores 167, three stores 172, two stores a nop apart 170, load then store 170, load, nop, store 169, two loads then a store 175 (179 when a refresh falls inside the block), store then load 169; a taken branch after a store changes nothing (165). A refresh falling due as an uncached store starts costs 10 (two back-to-back stores: 10, 9, 10), as in CPU-24.
Evidence: unc-load (17 runs, all seven models, identical), unc-store-phase (SCPH-9001 two runs, SCPH-5501 unit 2 one run, identical).

**CPU-35. In uncached code two Timer 2 reads are 8 cycles apart with nothing between, and each nop adds 6.** `lhu T2; N nops; lhu T2` from KSEG1: 8, 12, 18, 24, 30, 36, 42, 48, 54 for N = 0-8 (cached: 4, 6, 6, 6, 7, 8, 9, 10, 11). With a RAM store between: 20, 26, 32, 38, 48, 50, 62, 62, 68 uncached (some cells carry a refresh) and 8, 8, 8, 9, 10, 11, 12, 13, 14 cached.
Evidence: counter-bracket; all seven models; 39 runs, the commonest rows identical in 18.

## Exceptions and the BIOS window

**CPU-36. A bus error is taken late: instructions after the faulting access still complete.** Instructions that complete after the access before the exception is taken, and where EPC points (opcodes past the access):

| code | word 0 | word 1 | word 2 | word 3 |
|---|---|---|---|---|
| uncached | 0 (EPC +1) | 0 (+1) | 0 (+1) | 0 (+1) |
| cached, line in the cache | 1 (+2) | 1 (+2) | 1 (+2) | 1 (+2) |
| cached, the line being filled | 1 (+2) | 0 (+1) | 3 (+4) | 2 (+3) |

The rows were taken for a `lw` past the BIOS window, a `lbu` and an `sh` in gaps of the I/O region, and a `lw`/`sw` at 08000000h. All behave alike apart from two warm rows of `lw` at 08000000h. Word 0 reads +5 (CAUSE 2000001Ch) in 18 of the 31 runs and +2 in the other 13. Word 2 reads +9 (all eight ori/xori done, CAUSE 0000001Ch) in the 7 latest runs (six SCPH-5000 runs and the second 5501) and +2 in the other 24. nocash's DIAG source notes the same 1-3 instruction lag.
Evidence: bios-window; all seven models; 31 runs; the exception table is identical in 18 (24 if the word-0 warm row is ignored).

**CPU-37. After a bus error CAUSE.CE (bits 29-28) holds bits 27-26 of the opcode of the instruction in hand, not a coprocessor number.** The test's sequence alternates `ori` and `xori`. CE reads 1 after an `ori` and 2 after an `xori` (CAUSE 1000001Ch and 2000001Ch), and 0 after a nop. nocash's text says CE is opcode bits 26-27, which is this finding. Disagreement: the newer psx-spx says CE keeps its previous value on exceptions other than Coprocessor Unusable.
Evidence: bios-window; all seven models; 31 runs.

**CPU-38. The BIOS ROM region follows the size field of 1F801010h: a window of 2^N bytes, mirroring the chip if larger, a bus error past it.** With the 512 KB chip:

| size field | result |
|---|---|
| 12h (256 KB) | BFC40000h already faults |
| 13h (512 KB) | the chip; BFC80000h faults |
| 14h (1 MB) | BFC80000h-BFCFFFFCh mirror the chip; BFD00000h faults |
| 15h (2 MB) | mirrors through BFDFFFFCh; BFE00000h faults |
| 16h (4 MB) | mirrors through BFFFFFFCh |

A ROM word read takes 32 cycles at the BIOS's setting (13h) and a faulting access plus handler 30. Agrees with psx-spx.
Evidence: bios-window; all seven models; 28 of 31 runs identical (the three early console-day runs read 38 cycles for the same rows).

**CPU-39. Executing from I_STAT raises an instruction bus error at 1F801078h.** A jump to 1F801070h fetches I_STAT and I_MASK as two instructions and faults on the word after them: exception 6, EPC 1F801078h. Copying `jr ra; nop` there first (two stores that clear I_STAT bits and zero I_MASK) changes nothing about where it faults.
Evidence: cpu-code-in-irq; all seven models; 30 runs, EPC identical in all.

## DMA

**CPU-40. An OTC (DMA6) transfer does not stop the CPU, but the first bus access after it waits for its end, about 16 + N cycles plus 15 per 256 words.** A CHCR read issued straight after the write returns "busy" at 17, 18, 32, 283-287 and 1100-1110 cycles for N = 1, 2, 16, 256 and 1024 words. A CHCR read issued after 64 ALU instructions returns "idle" at 75, 75, 75, 291-301 and 1099-1114: the ALU instructions run during the transfer, the read then waits for the bus.
Evidence: dma-rates; all seven models, both 5501 units; 48 runs, rows within 10 cycles.

**CPU-41. While a 128x128 block-mode GPU upload (8192 words in blocks of 16) runs, CPU stores to RAM slow it by about 10%, and the CPU's RAM loads run 2.7 times slower.** The transfer takes 14,011-14,179 cycles with the CPU polling CHCR, 14,140-14,307 with the CPU running 64 ALU operations per iteration, 15,532-15,624 with 64 word stores per iteration, and 15,365-15,474 with `lw`/`lw`/`sw` steps. A `lw`/`lw`/`sw` step costs 60.0 cycles (60.01-60.44) during the transfer against 22.27 with no DMA.
Evidence: dma-contention; SCPH-5000, 5501 (both units), 7001, 7501, 7502, 9001; 43 runs.

**CPU-42. Two block-mode transfers started together each get blocks as their device asks for them, and the priority fields had no visible effect.** A 2048-word GPU upload (DMA2) and a 2048-word SPU write (DMA4), started back to back in four priority settings (equal either way, DMA2 high, DMA4 high): DMA2 finished at 3,944-4,688 cycles, by when DMA4, which alone runs at about 1,090 cycles a block, had moved 4 of its 128 blocks; DMA4 then finished at 140,888-163,008. An OTC clear (DMA6, 16,384 entries) started after DMA4 finished at about 17,550 with DMA4 at 2 of 128 blocks.
Evidence: concurrent-dma; all seven models, both 5501 units; 49 runs.

**CPU-43. The SCPH-1001 differs: CPU loads run faster during the GPU upload, and the upload is slower.** A `lw`/`lw`/`sw` step costs 52.5 cycles during the transfer (60.0 on the others) and the transfer takes 16,811-16,824 cycles under that load (15.4k elsewhere). The polling, ALU-loop and store cases agree with the other models (14,046, 14,155-14,158, 15,521-15,571). The 1001 has the older GPU; the cause was not isolated.
Evidence: dma-contention; SCPH-1001 only; 6 runs.

## Timers

**CPU-44. Timer 2's divide-by-8 prescaler runs free: writing the counter or mode does not restart it.** Of 32 counter writes at pseudo-random phases, the number reading 1 or more after K nops rises as a ramp, not a step. For K = 0-8 the median over 50 runs is 9, 2, 13, 13, 25, 23, 29, 32, 32 (ranges 2-9, 2-6, 9-15, 9-18, 23-30, 20-28, 28-32, 32, 32). For a MODE write it is 0, 3, 5, 13, 19, 21, 19, 29, 32 (ranges 0, 1-5, 3-6, 10-15, 12-19, 19-26, 11-25, 27-31, 32). A divider restarted by the write would read 0 until its first tick and then 32 at once.
Evidence: timer-sync (PRESCALER); SCPH-1001, 5501, 7001, 7501, 7502, 9001 (the 5000 was not run); 50 runs.

**CPU-45. Counting the dot clock against the system clock gives 6.71 MHz on NTSC-board consoles and 6.65 MHz on the PAL SCPH-7502, with the mode forced to NTSC on all of them.** Timer 0 on the dot clock (320-wide), over a 1011-cycle delay loop: 199-201 counts on SCPH-1001, 5000, 5501, 7001, 7501 and 9001; 198-199 on the SCPH-7502 with the mode forced to NTSC. Over 5011 cycles: 991-992 and 982-983. These fit 53.693 MHz / 8 and 53.203 MHz / 8. Timer 2 on system clock/8 counts 126-127 in 1011 cycles and 626-627 in 5011. The ps1-tests author's own console read 198/199 for the first, in his repository; its log starts "Detected NTSC system", so it is the count, not the mode flag, that puts it on the PAL crystal.
Evidence: timers (ps1-tests); all seven models; 48 runs.

## GTE

**CPU-46. After `mtc2` to IRGB, IR1 and IR2 read new from the fourth following instruction and IR3 from the third. Uncached, all three read new from the second.** `mtc2 IRGB; N fillers; mfc2 IRn`, the first N that reads the new value, cached: IRGB to IR1 3, IR2 3, IR3 2; IRGB to ORGB 2 (N = 0 old, N = 1 red and green new but blue old, 2 or more all new); IR1 to ORGB 1; IR3 to ORGB 2; IR1 to IRGB 1. Uncached: every IRn 1, ORGB as cached. The control `mtc2 LZCS; N; mfc2 LZCR` needs 2 cached and 1 uncached. IR3's cached answer is 1 on the first pass through cold code and 2 once warm. Agrees with psx-spx (three and two nops); Sony's developer notes give two for all three, which reads IR1 and IR2 stale.
Evidence: gte-irgb; all seven models; 31 runs, identical.

**CPU-47. A GTE command sees a late write to IR1-3 for the same instruction count as `mfc2` does.** From a warm cache, `mtc2 IRGB; N fillers; SQR` squares the old IR1 for N = 0, 1, 2 and the new from N = 3. After `mtc2 IR1` it squares the old for N = 0, 1 and the new from N = 2. From a cold cache the answer depends on which word of the line the move sits in (IRGB: N = 1 old, new, old, old for words 0-3; N = 2 new, new, old, old).
Evidence: gte-sqr-align; all seven models; 16 runs, identical.

**CPU-48. Each GTE input register is read by a running command at a fixed cycle after issue.** The smallest N (nops between the command and a `mtc2`/`ctc2` to the register) at which the write no longer changes the result, from a warm cache:

| command | register: N |
|---|---|
| RTPS | H 5, OFX 5, DQB 8, DQA 9 |
| RTPT | VXY1 8, R13R21 9, TRY 9, VXY2 10, R22R23 11, H 11, DQA 17 |
| NCT | VZ0 2, VZ1 6, VXY2 8, VZ2 11 |
| NCCT | RGBC 32 |
| NCDT | RGBC and RFC: later than 32 |
| DCPL | IR1 2 |
| DPCS, INTPL | IR0 2 |
| DPCT | IR0 11; RGB0, RGB1, RGB2: 5, 9 and 14 in an order that changes from run to run |

Controls (RTPS VZ0, NCT VXY0) read 0. These are cycles: pcsx-redux's gte-latency numbers for the same cells are smaller because its probes run from cold code (a line fill every four nops). The table gives the last cycle at which an input is still picked up; a first read is not separated from it.
Evidence: gte-latch-cached; all seven models; 49 runs, every row identical except DPCT RGB0-2.

**CPU-49. A GTE result read waits for the command to finish and never returns the old value.** After `RTPS`, `swc2` or `mfc2` of SXY2 with K nops between costs 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2 extra for K = 0-13 and 0 from K = 14; the value read is the new one at every K. As with `mflo`, the step from 2 to 0 skips 1.
Evidence: cpu-extras; all seven models, both 5501 units; 31 runs.

## Not settled

- **Open: interrupt entry latency** (cycles from a change in I_STAT or CAUSE to the handler's first instruction) was not measured by any program here. The only exception timing measured is the bus error in CPU-36.
- **Open: a register effect on a delay-slot store.** `sw A; lhu Timer2 -> t2; sw A+8; lw t2, A+8` costs 24 cycles at rest; the same with the load into any other register costs 23. Seen on SCPH-5501 unit 2 only (refresh-store7, 3 runs). The mechanism is unknown.
- **Open: one-unit results.** biu-sched, biu-dram and refresh-store2 to 7 have run on a single console (SCPH-5501 unit 2); the other models owe them. CPU-14, 15 and 25 rest on that unit, and the CPU-24 table additionally on refresh-io2 and refresh-store on other models.
- **Open: KSEG1 data accesses and the write queue.** The newer psx-spx says any KSEG1 access flushes the whole queue first. These programs run their data accesses through KSEG0 and test uncached code only, so this is untested.
- **Open: gte-hold** (does a write that follows a running command reach it when the next instruction has to wait?) prints different answers on the cold and warm passes of the same cached code; uncached it is stable (the write is never seen). No result is stated.
- **Open: ordinary GTE registers.** The newer psx-spx lists a late-write delay for ordinary registers (`mtc2` then `mfc2`); only IRGB, IR1-3, ORGB and LZCS were measured.
- **Open: RDPRI, NOPAD, BGNT and INTP** (FFFE0130h) and the RAM_SIZE refresh-period and hold-off fields were not toggled here. The newer psx-spx reports results for RDPRI.
