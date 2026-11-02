/*
 * cpu/pair-ext - hwtests/pair-warm's grid over the kinds it left out: lh, lb, sh, lwl, lwr, swl, swr, div, mfhi, mthi, cfc2, ctc2, mtc0, mtlo, lbu scratchpad (2026-11-01)
 *
 * See pair-warm. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js pair-ext --frames=30000 --quiet=2500`.
 */
#define PA_EXT 1
#include "../pair-warm/main.cpp"
