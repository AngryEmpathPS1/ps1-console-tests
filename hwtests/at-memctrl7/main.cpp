/*
 * cpu/at-memctrl7 (2026-11-23, the cold-io session) - at-memctrl6's decisive trains, each read into the SAME register or alternating two.
 *
 * WHY. hwtests/at-memctrl6 (the SCPH-7001 and 1001): a cold DPCR read at its line's last word overlaps the next line's fill on the
 * consoles only every other line (reads 4 apart at word 3: 237 / 64, this emulator 5). Every read there loads v0. hwtests/pair-cold's
 * pairs of on-die reads load two registers (t4, t5), and a rule that ignores the register costs 108 of its exact cells; with "into the
 * same register as the read the fill before deferred" it costs none (experiments/land_iotrain.js, TRNM 4). That condition is fitted:
 * this measures it. Rows as at-memctrl6 (16-read trains from word k, four a row, 64 reads): "v0" every read into v0, "alt" alternating
 * v0 and v1 (lw v0 / lw v1 / lw v0 ...).
 *
 * WHAT IT DOES. Rows "<reg> s<s> k<k>": the read body's and its base's eight cold passes (FlushCache before each, a first dropped;
 * Timer 2 cycles), then the minima and their difference over 64 reads. s4 k3 and s4 k2 (one read a line at word 3 / 2), s2 k1 (words 1
 * and 3), s5 k0 (walking the words).
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-memctrl7`.
 */

#include <common.h>
#include <measure.hpp>

extern "C" void am_flush(void);
__asm__(".set push\n.set noreorder\n.global am_flush\nam_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");   // BIOS A(44h) FlushCache

/* ONE rd reg s: the read into reg (or a nop), then s-1 nops. TRR s k rd alt: k nops, then 8 x two reads (v0 then v0, or v0 then v1 when
 * alt), nops to the next line, 24 nops. BODYR: lui t8 in a line of its own, then four trains. */
__asm__(".set push\n.set noreorder\n"
        ".macro ONE rd, reg, s\n .if \\rd\n  lw \\reg, 0x10F0($t8)\n .else\n  nop\n .endif\n .rept \\s - 1\n  nop\n .endr\n.endm\n"
        ".macro TRR s, k, rd, alt\n .rept \\k\n  nop\n .endr\n .rept 8\n  ONE \\rd, $v0, \\s\n  .if \\alt\n   ONE \\rd, $v1, \\s\n  .else\n   ONE \\rd, $v0, \\s\n  .endif\n .endr\n"
        " .balign 16\n .rept 24\n  nop\n .endr\n.endm\n"
        ".macro BODYR name, s, k, rd, alt\n .balign 16\n .global \\name\n\\name:\n lui $t8, 0x1F80\n nop\n nop\n nop\n .rept 4\n  TRR \\s, \\k, \\rd, \\alt\n .endr\n jr $ra\n nop\n.endm\n"
        ".set pop\n");
#define DEF(s, k, a) extern "C" void am_r_##s##_##k##_##a(void); extern "C" void am_b_##s##_##k##_##a(void); \
    __asm__(".set push\n.set noreorder\nBODYR am_r_" #s "_" #k "_" #a ", " #s ", " #k ", 1, " #a "\nBODYR am_b_" #s "_" #k "_" #a ", " #s ", " #k ", 0, " #a "\n.set pop\n");
#define DEFS(a) DEF(4, 3, a) DEF(4, 2, a) DEF(2, 1, a) DEF(5, 0, a)
DEFS(0) DEFS(1)

static const int PASSES = 8;
static uint32_t cold(void (*f)(void), uint32_t *t) {
    uint32_t m = ~0u;
    for (int i = -1; i < PASSES; i++) { am_flush(); uint32_t c = measure<MeasureMethod::CpuClock>(f); if (i >= 0) { t[i] = c; if (c < m) m = c; } }
    return m;
}
static void row(const char *reg, int s, int k, void (*r)(void), void (*b)(void)) {
    uint32_t tr[PASSES], tb[PASSES]; const uint32_t rm = cold(r, tr), bm = cold(b, tb);
    printf("%s s%d k%d read", reg, s, k); for (int i = 0; i < PASSES; i++) printf(" %u", (unsigned)tr[i]);
    printf("  base"); for (int i = 0; i < PASSES; i++) printf(" %u", (unsigned)tb[i]);
    printf("  min %u %u  extra %d/64\n", (unsigned)rm, (unsigned)bm, (int)rm - (int)bm);
}
#define ROW(s, k, a) row(a ? "alt" : "v0", s, k, am_r_##s##_##k##_##a, am_b_##s##_##k##_##a)
#define ROWS(a) ROW(4, 3, a); ROW(4, 2, a); ROW(2, 1, a); ROW(5, 0, a)

int main() {
    printf("\ncpu/at-memctrl7\n");
    printf("at-memctrl6's trains with every cold DPCR read into v0, or alternating v0 / v1; each body and its base cold 8 times (FlushCache before each); Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    ROWS(0); ROWS(1);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
