# mark-cost: what a DB-25 mark costs (2026-11-24, the console fuzzer, for AngryEmpath)

SCPH-1001 (GameShark, Pico fw 2p-feedfix14) and SCPH-7001 (no cart: mark rows skipped), one run each:
console-logs/2026-11-24-SCPH-1001-markcost (with pico-db25.txt, the Pico's stamped marks), -SCPH-7001-markcost; the emulator
(_cores/next) in console-logs/2026-11-24-emu-markcost.

- **One mark costs the CPU nothing measurable**: the "sbMark then t1" row is the no-store row exactly (19..31). A RAM load behind it
  mostly does not wait either (19 26 21 22 23 24 28 26 ...: two cells +6 / +3 at K = 1 and 6, one run - repeat before reading them).
- **Back to back, the write queue hides four**: 1, 2, 4, 8 marks then no-ops to 16 instructions = 23, 27, 27, 90 (no-store 23).
- **On the pin, back-to-back marks come 17.2-17.6 CPU cycles apart** (the Pico's stamps, 0.4-cycle steps = its 13 ns quantum): each
  store to the cart holds the bus about 17.4 cycles, so the fifth and later wait - (8 - 4) x 17 = 68 against the measured 67.
- So for CD or DMA work: a lone mark is free to the CPU but occupies the sub-bus (the CD's and the SPU's) for ~17 cycles after it.
  Keep anything timed on that bus more than ~17 cycles from a mark, or measure with and without.
- Side result, already known (STATUS "exp2st, HELD"): Expansion 2 stores cost this core +10 cycles each where the consoles' cost 0
  (sbE2 rows 29.. vs 19..; packs 33 43 63 103 vs 23 23 37 78).
