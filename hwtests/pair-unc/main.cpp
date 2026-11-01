/*
 * cpu/pair-unc - hwtests/pair-warm's grid with the block run uncached, through KSEG1 (2026-11-01)
 *
 * See pair-warm. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js pair-unc --frames=30000 --quiet=2500`.
 */
#define PA_UNC 1
#include "../pair-warm/main.cpp"
