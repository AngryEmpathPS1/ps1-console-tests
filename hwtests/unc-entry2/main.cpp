/*
 * cpu/unc-entry2 - unc-entry's transitions taken further: where a read in uncached code samples, against how many
 * uncached instructions follow it, and the entry with the on-die read long over (2026-10-11 night)
 *
 * WHY. hwtests/unc-entry on the SCPH-1001 (three runs, identical): with both Timer 2 reads inside the uncached stub (UU)
 * every row is this core's; with both in the cached caller around the call (CU) the console reads 2 less at every N;
 * with the second read inside the stub (CE) 7 more (8 at N = 0). The uncached fetch's pace is right; the reads' places
 * at the transitions are not. LSI's relative IDT R3051 (hardware manual, chapter 6, "the CPU core will perform the data
 * request first") serves a data read and an instruction miss of the same cycle in that order, and a load reaches its MEM
 * stage beside the fetch three instructions on: in uncached code a load's read would wait for the fetches of the two
 * instructions behind it. This separates that from the entry's own cost.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; each stub called at its KSEG1 alias (uncached) and at its
 * KSEG0 address (cached control), 16 trials spread over the DRAM refresh, fastest and slowest:
 *   CU4 N   lhu T2; 4 nops; jalr stub (N nops, jr ra, nop); nop; lhu T2       unc-entry's CU with the read long over
 *   CE N k  lhu T2; jalr stub (N nops, lhu T2, k nops, jr ra, nop)             the read with k uncached nops behind it
 *   CE4 N   lhu T2; 4 nops; jalr stub (N nops, lhu T2, jr ra, nop)             CE with the first read long over
 *   UC N    jalr stub (lhu T2, N nops, jr ra, nop); nop; lhu T2                the first read uncached, the second back home
 *
 * READ IT LIKE THIS. CE over k: a read that waits for the fetches behind it moves 6 a k until k reaches its depth and
 * then holds still against the nops; a read at its own fetch moves 0. CU4/CE4 against unc-entry's CU/CE: the entry with
 * and without the on-die read's shadow. UC: the exit.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js unc-entry2`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define NOPS(K) ".rept " #K "\nnop\n.endr\n"
#define STUB(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\n" body "jr $ra\nnop\n.set pop\n");
/* t9 = 1F800000h on entry; reads into v0 (the caller's second) or t1 (UC's first) */
STUB(cu0, "") STUB(cu1, NOPS(1)) STUB(cu4, NOPS(4))
#define CE(N, K) STUB(ce##N##_##K, NOPS(N) "lhu $v0, 0x1120($t9)\n" NOPS(K))
CE(0, 0) CE(0, 1) CE(0, 2) CE(0, 3) CE(2, 0) CE(2, 1) CE(2, 2) CE(2, 3)
#define UC(N) STUB(uc##N, "lhu $t1, 0x1120($t9)\n" NOPS(N))
UC(0) UC(1) UC(4)

extern "C" uint32_t callCU4(uint32_t (*f)(void));
extern "C" uint32_t callCE(uint32_t (*f)(void));
extern "C" uint32_t callCE4(uint32_t (*f)(void));
extern "C" uint32_t callUC(uint32_t (*f)(void));
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callCU4\ncallCU4:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1120($t9)\n" NOPS(4) "jalr $a0\nnop\nnop\nlhu $t2, 0x1120($t9)\nnop\n"
        "subu $v0, $t2, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callCE\ncallCE:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1120($t9)\njalr $a0\nnop\n"
        "subu $v0, $v0, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callCE4\ncallCE4:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1120($t9)\n" NOPS(4) "jalr $a0\nnop\n"
        "subu $v0, $v0, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callUC\ncallUC:\nmove $t8, $ra\nlui $t9, 0x1f80\njalr $a0\nnop\nnop\nlhu $t2, 0x1120($t9)\nnop\n"
        "subu $v0, $t2, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

typedef uint32_t (*Fn)(void);
typedef uint32_t (*Caller)(Fn);
struct Span { uint32_t lo, hi; };
static Span trials(Caller c, Fn f) {
    Span s = { 0xFFFF, 0 };
    c(f);
    for (uint32_t k = 0; k < 16; k++) {
        spin(k * 8);
        const uint32_t v = c(f);
        if (v < s.lo) s.lo = v;
        if (v > s.hi) s.hi = v;
    }
    return s;
}
static void line(const char* name, Caller c, Fn f) {
    const Span u = trials(c, (Fn)((uint32_t)f | 0xA0000000u)), k = trials(c, f);
    printf("  %-8s uncached %4u..%-4u  cached %4u..%-4u  difference %d\n", name, (unsigned)u.lo, (unsigned)u.hi,
           (unsigned)k.lo, (unsigned)k.hi, (int)u.lo - (int)k.lo);
}

int main() {
    printf("\ncpu/unc-entry2\n");
    printf("unc-entry's transitions further (Timer 2 cycles, fastest..slowest of 16 over the refresh, against the stub cached).\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    line("CU4 0", callCU4, cu0); line("CU4 1", callCU4, cu1); line("CU4 4", callCU4, cu4);
    line("CE 0 0", callCE, ce0_0); line("CE 0 1", callCE, ce0_1); line("CE 0 2", callCE, ce0_2); line("CE 0 3", callCE, ce0_3);
    line("CE 2 0", callCE, ce2_0); line("CE 2 1", callCE, ce2_1); line("CE 2 2", callCE, ce2_2); line("CE 2 3", callCE, ce2_3);
    line("CE4 0", callCE4, ce0_0); line("CE4 2", callCE4, ce2_0);
    line("UC 0", callUC, uc0); line("UC 1", callUC, uc1); line("UC 4", callUC, uc4);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
