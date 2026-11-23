/*
 * cpu/at-memctrl4 (2026-11-23, the cold-io session) - a cold on-die read in dense code, every pass cold: is the consoles' extra cost
 * the read itself or the DRAM refresh landing among the reads?
 *
 * WHY. hwtests/at-memctrl2 / at-memctrl3 (one cold pass each, so each crosses two or three refreshes) read a cold DPCR read dearer on
 * the SCPH-7001 and 1001 than this emulator in every shape: `lw; nop; nop` 3.75 a read against 2.3, `lw` six apart 0.6 against 0.1.
 * hwtests/load-fill5 (the FASTEST of four cold passes: no refresh) the emulator matches, and with the refresh's cost set to 0 here
 * at-memctrl2's L rows hardly move. So either the read is dear on the consoles, or the refresh costs them far more among cold
 * on-die reads than it does here. This times each shape cold EIGHT times - the BIOS FlushCache before every pass, as
 * hwtests/icache-place10 - and prints every pass: the fastest is the shape without a refresh, the spread is what the refresh adds.
 *
 * WHAT IT DOES. For each shape, 64 copies in a row (DPCR, 1F8010F0h, as at-memctrl3), and its base with the read a nop:
 *   L2: lw v0, R; nop                       (a read every 2 instructions)
 *   L3: lw v0, R; nop; nop                  (at-memctrl2's L)
 *   L6: lw v0, R; nop x5                    (at-memctrl3's k0 n)
 *   N3: lw v0, R; nop; sw v0, 4(buf)        (at-memctrl2's N)
 *   S3: lw v0, R; sw t1, 0(buf); sw v0, 4(buf)   (at-memctrl2's S, the fuzz host's memory-control reads)
 * A row: shape, then "read" and "base" with the eight cold passes (Timer 2 cycles, a first warm-up pass dropped), then min and max.
 * READ (read min - base min) / 64 as the read's cost without a refresh, and each column's spread as the refresh's.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-memctrl4`.
 */

#include <common.h>
#include <measure.hpp>

extern "C" void am_flush(void);
__asm__(".set push\n.set noreorder\n.global am_flush\nam_flush:\n  li $t1, 0x44\n  j 0xA0\n  nop\n.set pop\n");   // BIOS A(44h) FlushCache

static volatile uint32_t buf[4] __attribute__((aligned(16)));

#define REP8(x) x x x x x x x x
#define REP64(x) REP8(REP8(x))
#define RD "lw $v0, 0x10F0($t8)\n"
#define NO "nop\n"
#define BODY(seq) asm volatile(".set noreorder\n lui $t8, 0x1F80\n move $t7, %0\n move $t1, $zero\n" REP64(seq) "nop\n .set reorder\n" \
                               :: "r"(buf) : "v0", "t1", "t7", "t8", "memory")
#define L2(x) x NO
#define L3(x) x NO NO
#define L6(x) x NO NO NO NO NO
#define N3(x) x NO "sw $v0, 4($t7)\n"
#define S3(x) x "sw $t1, 0($t7)\n sw $v0, 4($t7)\n"

static const int PASSES = 8;
template <typename F> static void passes(F f, uint32_t *t) {
    for (int i = -1; i < PASSES; i++) { am_flush(); uint32_t c = measure<MeasureMethod::CpuClock>(f); if (i >= 0) t[i] = c; }
}
static void show(const char *name, const uint32_t *r, const uint32_t *b) {
    uint32_t rmin = ~0u, rmax = 0, bmin = ~0u, bmax = 0;
    printf("%s read", name);
    for (int i = 0; i < PASSES; i++) { printf(" %u", (unsigned)r[i]); if (r[i] < rmin) rmin = r[i]; if (r[i] > rmax) rmax = r[i]; }
    printf("  base");
    for (int i = 0; i < PASSES; i++) { printf(" %u", (unsigned)b[i]); if (b[i] < bmin) bmin = b[i]; if (b[i] > bmax) bmax = b[i]; }
    printf("  min %u %u  max %u %u\n", (unsigned)rmin, (unsigned)bmin, (unsigned)rmax, (unsigned)bmax);
}
#define CASE(name, SH) do { \
    auto b = [] { BODY(SH(NO)); }; auto r = [] { BODY(SH(RD)); }; uint32_t tr[PASSES], tb[PASSES]; \
    passes(r, tr); passes(b, tb); show(name, tr, tb); } while (0)

int main() {
    printf("\ncpu/at-memctrl4\n");
    printf("a cold DPCR read, 64 in a row, in five shapes; each shape and its base (the read a nop) cold 8 times, FlushCache before each; Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    CASE("L2", L2); CASE("L3", L3); CASE("L6", L6); CASE("N3", N3); CASE("S3", S3);
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
