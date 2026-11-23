/*
 * cpu/at-memctrl6 (2026-11-23, the cold-io session) - does a cold on-die read train cost by the words its reads sit at?
 *
 * WHY. hwtests/at-memctrl5 (the SCPH-7001 and 1001): trains of cold DPCR reads 2, 3 or 5 instructions apart cost ~1-1.5 a read more
 * on the consoles than this emulator, 4 apart (one read a line, at one word) the same; at-memctrl4's reads 6 apart (two words,
 * alternating) the same. A timing window cannot tell 5 apart (dear) from 6 apart (cheap); the words the reads walk through can: 3 and 5
 * apart visit all four words of a line, 4 one, 6 two. This times trains at a FIXED word and trains started at each word.
 *
 * WHAT IT DOES. Rows "s<s> k<k>": 16-read trains (64 reads a row: four trains, each then 24 nops), reads s instructions apart, the first
 * at word k of its line (the function is 16-byte aligned; k nops before the first read), against the same code with the reads nops.
 * s 4 and 8 put every read at word k; s 2 and 6 alternate k and k+2; s 5 walks k, k+1, k+2, k+3. Each body and base cold 8 times (the
 * BIOS FlushCache before each, a first pass dropped), Timer 2 cycles, then the minima and their difference over 64 reads.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-memctrl6`.
 */

#include <common.h>
#include <measure.hpp>

extern "C" void am_flush(void);
__asm__(".set push\n.set noreorder\n.global am_flush\nam_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");   // BIOS A(44h) FlushCache

/* BODYK name s k rd: lui t8 in a line of its own, then four times [k nops, 16 x (read or nop, s-1 nops), nops to the next line, 24 nops] */
__asm__(".set push\n.set noreorder\n"
        ".macro TRK s, k, rd\n .rept \\k\n  nop\n .endr\n .rept 16\n  .if \\rd\n   lw $v0, 0x10F0($t8)\n  .else\n   nop\n  .endif\n  .rept \\s - 1\n   nop\n  .endr\n .endr\n"
        " .balign 16\n .rept 24\n  nop\n .endr\n.endm\n"
        ".macro BODYK name, s, k, rd\n .balign 16\n .global \\name\n\\name:\n lui $t8, 0x1F80\n nop\n nop\n nop\n .rept 4\n  TRK \\s, \\k, \\rd\n .endr\n jr $ra\n nop\n.endm\n"
        ".set pop\n");
#define DEF(s, k) extern "C" void am_r_##s##_##k(void); extern "C" void am_b_##s##_##k(void); \
    __asm__(".set push\n.set noreorder\nBODYK am_r_" #s "_" #k ", " #s ", " #k ", 1\nBODYK am_b_" #s "_" #k ", " #s ", " #k ", 0\n.set pop\n");
#define DEFS(s) DEF(s, 0) DEF(s, 1) DEF(s, 2) DEF(s, 3)
DEFS(2) DEFS(4) DEFS(5) DEFS(6) DEFS(8)

static const int PASSES = 8;
static uint32_t cold(void (*f)(void), uint32_t *t) {
    uint32_t m = ~0u;
    for (int i = -1; i < PASSES; i++) { am_flush(); uint32_t c = measure<MeasureMethod::CpuClock>(f); if (i >= 0) { t[i] = c; if (c < m) m = c; } }
    return m;
}
static void row(int s, int k, void (*r)(void), void (*b)(void)) {
    uint32_t tr[PASSES], tb[PASSES]; const uint32_t rm = cold(r, tr), bm = cold(b, tb);
    printf("s%d k%d read", s, k); for (int i = 0; i < PASSES; i++) printf(" %u", (unsigned)tr[i]);
    printf("  base"); for (int i = 0; i < PASSES; i++) printf(" %u", (unsigned)tb[i]);
    printf("  min %u %u  extra %d/64\n", (unsigned)rm, (unsigned)bm, (int)rm - (int)bm);
}
#define ROW(s, k) row(s, k, am_r_##s##_##k, am_b_##s##_##k)
#define ROWS(s) ROW(s, 0); ROW(s, 1); ROW(s, 2); ROW(s, 3)

int main() {
    printf("\ncpu/at-memctrl6\n");
    printf("16-read trains of cold DPCR reads s apart from word k, 64 reads a row; each body and its base cold 8 times (FlushCache before each); Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    ROWS(2); ROWS(4); ROWS(5); ROWS(6); ROWS(8);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
