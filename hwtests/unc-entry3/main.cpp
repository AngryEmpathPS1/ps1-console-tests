/*
 * cpu/unc-entry3 - a read or a store in uncached code with the instructions behind it uncached or cached: where its
 * register access happens (2026-10-11 night)
 *
 * WHY. hwtests/unc-entry and unc-entry2 on the SCPH-1001 (three runs each, identical): a Timer 2 read inside uncached
 * code samples about 9 cycles later than this core samples it, measured from cached code on either side (unc-entry2's CE
 * N k 18 / 30 at every k, here 10 / 23; its UC - the uncached read first, the second back in cached code - 7 11 29, here
 * 14 20 38), while two reads both inside it (unc-entry's UU) are this core's at every N. LSI's relative IDT R3051 runs a
 * load's MEM stage beside the fetch three instructions on and serves the data request first (hardware manual, chapter
 * 6), so in uncached code the read would wait for the fetches of the two instructions behind it - always uncached in
 * those rows (k nops, or jr ra and its delay slot). Here the read sits in jr ra's delay slot, the instructions behind it
 * cached: a read that waits for the fetches behind it samples early there; a fixed delay does not care.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; each stub called at its KSEG1 alias (uncached) and at its
 * KSEG0 address (cached control), 16 trials spread over the DRAM refresh, fastest and slowest:
 *   CE N / CEd N   lhu T2 (cached); jalr stub: N nops, lhu T2, jr ra, nop / N nops, jr ra, lhu T2 (the delay slot)
 *   UC N / UCd N   jalr stub: N nops, lhu T2, jr ra, nop / N nops, jr ra, lhu T2; then (cached) nop; lhu T2
 *   SC N / SCd N   jalr stub: N nops, sh 0,T2, jr ra, nop / N nops, jr ra, sh 0,T2; then (cached) nop; lhu T2 - the count
 *
 * READ IT LIKE THIS. CEd against CE (and UCd against UC): the same instructions, the read moved past the jump - a read
 * that waits for the two fetches behind it moves by about two fetches, a fixed delay by one instruction. SC/SCd: the
 * same question for a store, read back from cached code (counter-store's from-cached rows as the reference).
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js unc-entry3`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define NOPS(K) ".rept " #K "\nnop\n.endr\n"
#define STUB(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\n" body ".set pop\n");
/* t9 = 1F800000h on entry */
#define CE(N) STUB(ce##N, NOPS(N) "lhu $v0, 0x1120($t9)\njr $ra\nnop\n") STUB(ced##N, NOPS(N) "jr $ra\nlhu $v0, 0x1120($t9)\n")
#define UC(N) STUB(uc##N, NOPS(N) "lhu $t1, 0x1120($t9)\njr $ra\nnop\n") STUB(ucd##N, NOPS(N) "jr $ra\nlhu $t1, 0x1120($t9)\n")
#define SC(N) STUB(sc##N, NOPS(N) "sh $zero, 0x1120($t9)\njr $ra\nnop\n") STUB(scd##N, NOPS(N) "jr $ra\nsh $zero, 0x1120($t9)\n")
CE(0) CE(2) UC(0) UC(2) SC(0) SC(2)

extern "C" uint32_t callCE(uint32_t (*f)(void));
extern "C" uint32_t callUC(uint32_t (*f)(void));
extern "C" uint32_t callSC(uint32_t (*f)(void));
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callCE\ncallCE:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1120($t9)\njalr $a0\nnop\n"
        "nop\nsubu $v0, $v0, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callUC\ncallUC:\nmove $t8, $ra\nlui $t9, 0x1f80\njalr $a0\nnop\nnop\nlhu $t2, 0x1120($t9)\nnop\n"
        "subu $v0, $t2, $t1\nandi $v0, $v0, 0xffff\njr $t8\nnop\n"
        ".global callSC\ncallSC:\nmove $t8, $ra\nlui $t9, 0x1f80\njalr $a0\nnop\nnop\nlhu $v0, 0x1120($t9)\nnop\n"
        "andi $v0, $v0, 0xffff\njr $t8\nnop\n"
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
    printf("  %-6s uncached %4u..%-4u  cached %4u..%-4u  difference %d\n", name, (unsigned)u.lo, (unsigned)u.hi,
           (unsigned)k.lo, (unsigned)k.hi, (int)u.lo - (int)k.lo);
}

int main() {
    printf("\ncpu/unc-entry3\n");
    printf("A read or store in uncached code before jr ra and in its delay slot (Timer 2 cycles, fastest..slowest of 16).\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    line("CE 0", callCE, ce0); line("CEd 0", callCE, ced0); line("CE 2", callCE, ce2); line("CEd 2", callCE, ced2);
    line("UC 0", callUC, uc0); line("UCd 0", callUC, ucd0); line("UC 2", callUC, uc2); line("UCd 2", callUC, ucd2);
    line("SC 0", callSC, sc0); line("SCd 0", callSC, scd0); line("SC 2", callSC, sc2); line("SCd 2", callSC, scd2);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
