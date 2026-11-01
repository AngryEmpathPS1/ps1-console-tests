/*
 * cpu/pair-cdep - hwtests/pair-dep's kinds with every line of the block cold (2026-11-01)
 *
 * See pair-warm and pair-dep. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js pair-cdep --frames=30000 --quiet=2500`.
 */
#define PA_DEP 1
#define PA_COLD 1
#define PA_CDEP 1
#include "../pair-warm/main.cpp"
