/*
 * cpu/gte-undef - the GTE's 42 undefined function codes, 16 random cases of each (2026-11-01)
 *
 * WHY. hwtests/fuzz-gte found they do things: 00h is RTPS to the last bit, 1Ah DCPL, 17h NCDT. Nobody documents them. This asks of each of the 42 what fuzz-gte asks: every register after it, and the time.
 * See fuzz-gte for the lines. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gte-undef --frames=20000 --quiet=2500`.
 */
#define GT_UNDEF 1
#include "../fuzz-gte/main.cpp"
