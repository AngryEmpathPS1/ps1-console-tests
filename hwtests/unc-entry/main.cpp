/*
 * cpu/unc-entry - what entering and leaving uncached code costs: N uncached nops with the Timer 2 reads around them
 * placed three ways, N = 0..4, 8, 16, 32 (2026-10-11 night)
 *
 * WHY. hwtests/refresh-warm on the SCPH-5501, 9001 and 1001 (all byte-identical): 32 uncached nops (the block called at its
 * KSEG1 alias from cached code) cost 208 cycles between the two Timer 2 reads where this core reads 210, and the refresh's
 * pattern over them sits two phases late here - the shape (6 4 4 4 4 5 every six cycles) exact. So the block runs at the
 * consoles' pace and two cycles go elsewhere: the call into KSEG1 and the first uncached fetch, or the return to cached
 * code. Where the reads sit separates them.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. Three kinds of row, each stub called at its KSEG1 alias
 * (uncached) and at its KSEG0 address (cached, the control - the same instructions from the cache):
 *   CU  lhu T2 (cached); jalr stub: N nops, jr ra, nop; nop; lhu T2 (cached)     the entry AND the return in the window
 *   CE  lhu T2 (cached); jalr stub: N nops, lhu T2, jr ra, nop                    the entry only
 *   UU  jalr stub: lhu T2, N nops, lhu T2, jr ra, nop                             neither (both reads in the stub)
 * 16 trials a row, each started 16k cycles later than the last (a spin) so the 256-cycle DRAM refresh lands at every place;
 * the fastest and slowest printed, and fastest uncached less fastest cached.
 *
 * READ IT LIKE THIS. Against the emulator, the first kind whose difference moves names the piece: UU alone off - the
 * uncached fetch or a read from uncached code; CE (and CU) off with UU right - the entry; CU alone - the return. Over N, a
 * constant is a transition, a slope the fetch.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js unc-entry`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

/* t9 = 1F800000h on entry (the callers set it); ue/uu return the second read in v0, uu its first in t1 */
#define STUBS(N) \
    extern "C" void cu##N(void); extern "C" uint32_t ce##N(void); extern "C" uint32_t uu##N(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n" \
            ".balign 16\n.global cu" #N "\ncu" #N ":\n.rept " #N "\nnop\n.endr\njr $ra\nnop\n" \
            ".balign 16\n.global ce" #N "\nce" #N ":\n.rept " #N "\nnop\n.endr\nlhu $v0, 0x1120($t9)\njr $ra\nnop\n" \
            ".balign 16\n.global uu" #N "\nuu" #N ":\nlhu $t1, 0x1120($t9)\n.rept " #N "\nnop\n.endr\nlhu $v0, 0x1120($t9)\njr $ra\nnop\n" \
            ".set pop\n");
STUBS(0) STUBS(1) STUBS(2) STUBS(3) STUBS(4) STUBS(8) STUBS(16) STUBS(32)

extern "C" uint32_t callCU(void (*f)(void));
extern "C" uint32_t callCE(uint32_t (*f)(void));
extern "C" uint32_t callUU(uint32_t (*f)(void));
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callCU\ncallCU:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1120($t9)\njalr $a0\nnop\nnop\nlhu $t2, 0x1120($t9)\nnop\n"
        "subu $v0, $t2, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callCE\ncallCE:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1120($t9)\njalr $a0\nnop\n"
        "subu $v0, $v0, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callUU\ncallUU:\nmove $t8, $ra\nlui $t9, 0x1f80\njalr $a0\nnop\n"
        "subu $v0, $v0, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

template <typename F> static uint32_t unc(F f) { return (uint32_t)f | 0xA0000000u; }

struct Span { uint32_t lo, hi; };
template <typename T> static Span trials(T t) {
    Span s = { 0xFFFF, 0 };
    t();
    for (uint32_t k = 0; k < 16; k++) {
        spin(k * 8);
        const uint32_t v = t();
        if (v < s.lo) s.lo = v;
        if (v > s.hi) s.hi = v;
    }
    return s;
}

static void line(const char* kind, int n, Span u, Span c) {
    printf("  %s N=%-2d  uncached %4u..%-4u  cached %4u..%-4u  difference %d\n", kind, n, (unsigned)u.lo, (unsigned)u.hi,
           (unsigned)c.lo, (unsigned)c.hi, (int)u.lo - (int)c.lo);
}

#define ROWS(N) \
    { const Span u = trials([] { return callCU((void (*)(void))unc(cu##N)); }), c = trials([] { return callCU(cu##N); }); line("CU", N, u, c); } \
    { const Span u = trials([] { return callCE((uint32_t (*)(void))unc(ce##N)); }), c = trials([] { return callCE(ce##N); }); line("CE", N, u, c); } \
    { const Span u = trials([] { return callUU((uint32_t (*)(void))unc(uu##N)); }), c = trials([] { return callUU(uu##N); }); line("UU", N, u, c); }

int main() {
    printf("\ncpu/unc-entry\n");
    printf("N uncached nops from cached code, the Timer 2 reads around them (CU), entry only (CE), inside (UU);\n");
    printf("Timer 2 cycles, fastest..slowest of 16 spread over the refresh, against the same stub cached.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    ROWS(0) ROWS(1) ROWS(2) ROWS(3) ROWS(4) ROWS(8) ROWS(16) ROWS(32)
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
