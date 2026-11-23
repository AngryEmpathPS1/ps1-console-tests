/*
 * cpu/at-memctrl5 (2026-11-23, the cold-io session) - where a train of cold on-die reads starts to cost: spacing against train length.
 *
 * WHY. hwtests/at-memctrl4 (every pass cold, the fastest pass refresh-free; the SCPH-7001 and 1001 identical): 64 cold DPCR reads 2
 * or 3 instructions apart cost ~1.5 more each on the consoles than this emulator (L2 4.50 / 3.03, L3 3.69 / 2.22), 6 apart the same
 * (0.28 / 0.30); hwtests/load-fill5's PAIRS (two reads 1-3 apart, refresh-free) the emulator matches. So the extra builds in a train.
 * This places its edge: trains of n reads (n = 1, 2, 4, 8, 16, 64) s instructions apart (s = 2, 3, 4, 5), each train followed by 24
 * nops (six lines: the next train starts on a fresh line pattern), 64 reads a row in all, against the same code with the reads nops.
 *
 * WHAT IT DOES. Row "s<s> n<n>": the read body's and the base's eight cold passes (the BIOS FlushCache before each, a first pass
 * dropped; Timer 2 cycles), then their minima. READ (read min - base min) / 64: a cold read's cost in a train of n at spacing s.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-memctrl5`.
 */

#include <common.h>
#include <measure.hpp>

extern "C" void am_flush(void);
__asm__(".set push\n.set noreorder\n.global am_flush\nam_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");   // BIOS A(44h) FlushCache

/* TRAIN s n rd: n copies of [the read (rd 1) or a nop, then s-1 nops], then 24 nops; BODYF name s n rd: that, 64/n times, as a function */
__asm__(".set push\n.set noreorder\n"
        ".macro TRAIN s, n, rd\n .rept \\n\n  .if \\rd\n   lw $v0, 0x10F0($t8)\n  .else\n   nop\n  .endif\n  .rept \\s - 1\n   nop\n  .endr\n .endr\n"
        " .rept 24\n  nop\n .endr\n.endm\n"
        ".macro BODYF name, s, n, rd\n .balign 16\n .global \\name\n\\name:\n lui $t8, 0x1F80\n .rept 64 / \\n\n  TRAIN \\s, \\n, \\rd\n .endr\n jr $ra\n nop\n.endm\n"
        ".set pop\n");
#define DEF(s, n) extern "C" void am_r_##s##_##n(void); extern "C" void am_b_##s##_##n(void); \
    __asm__(".set push\n.set noreorder\nBODYF am_r_" #s "_" #n ", " #s ", " #n ", 1\nBODYF am_b_" #s "_" #n ", " #s ", " #n ", 0\n.set pop\n");
#define DEFS(s) DEF(s, 1) DEF(s, 2) DEF(s, 4) DEF(s, 8) DEF(s, 16) DEF(s, 64)
DEFS(2) DEFS(3) DEFS(4) DEFS(5)

static const int PASSES = 8;
static uint32_t cold(void (*f)(void), uint32_t *t) {
    uint32_t m = ~0u;
    for (int i = -1; i < PASSES; i++) { am_flush(); uint32_t c = measure<MeasureMethod::CpuClock>(f); if (i >= 0) { t[i] = c; if (c < m) m = c; } }
    return m;
}
static void row(int s, int n, void (*r)(void), void (*b)(void)) {
    uint32_t tr[PASSES], tb[PASSES]; const uint32_t rm = cold(r, tr), bm = cold(b, tb);
    printf("s%d n%-2d read", s, n); for (int i = 0; i < PASSES; i++) printf(" %u", (unsigned)tr[i]);
    printf("  base"); for (int i = 0; i < PASSES; i++) printf(" %u", (unsigned)tb[i]);
    printf("  min %u %u  extra %d/64\n", (unsigned)rm, (unsigned)bm, (int)rm - (int)bm);
}
#define ROW(s, n) row(s, n, am_r_##s##_##n, am_b_##s##_##n)
#define ROWS(s) ROW(s, 1); ROW(s, 2); ROW(s, 4); ROW(s, 8); ROW(s, 16); ROW(s, 64)

int main() {
    printf("\ncpu/at-memctrl5\n");
    printf("trains of n cold DPCR reads s instructions apart, then 24 nops, 64 reads a row; each body and its base cold 8 times (FlushCache before each); Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    ROWS(2); ROWS(3); ROWS(4); ROWS(5);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
