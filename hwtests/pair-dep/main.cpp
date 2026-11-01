/*
 * cpu/pair-dep - hwtests/pair-warm's grid with kinds that depend on each other, warm (2026-11-01)
 *
 * X and Y load into the same register. nop; addu t5,t4,t4; lw A; lhu I_STAT; lhu SPUSTAT; lw scratchpad; sw t4,A; multu; mfc2 $25; mtc2 $9; NCLIP; RTPS; lwc2 $9,A; swc2 $25,A; a taken
 * branch to the word behind its delay slot; mfc0 $12. See pair-warm for the rest. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js pair-dep --frames=30000 --quiet=2500`.
 */
#define PA_DEP 1
#include "../pair-warm/main.cpp"
