/*
 * cpu/cpu-code-in-irq - ps1-tests' cpu/code-in-io testCodeInInterrupts, with nothing printed until the interrupt
 * controller is back as it was (2026-10-06).
 *
 * WHY. cpu/code-in-io HUNG on all three consoles (SCPH-5501, 7501, 9001, two runs each) right after "pass -
 * testCodeInMDEC": in testCodeInInterrupts, which its author's console passes (its psx.log) and this core passes. The
 * test copies its code - jr $ra; nop (03E00008h, 00000000h) - to 1F801070h and jumps there, expecting a bus error on
 * the fetch. But the copy is two STORES to the interrupt controller: I_STAT is acknowledged down to bit 3 and I_MASK
 * becomes 0000h, every interrupt masked - and the test then prints its "pass" line (TEST_MULTIPLE_END) before it puts
 * I_MASK back. Its author's console printed through something that needed no interrupt; ours print through Unirom, whose
 * debug handler hears the cable by interrupt (console day: a program that ends with interrupts off leaves it deaf). So
 * the hang may be the printing, not the bus error. This test runs the same steps and prints only afterwards.
 *
 * WHAT IT DOES. Its own unresolved-exception handler (the test's, plus EPC, Cause and BadVaddr kept), then:
 *   JUMP    interrupts off, a jump to 1F801070h with nothing copied there (the fetch alone), I_MASK restored, then print.
 *   COPY    the original's order: interrupts off, the two words copied to 1F801070h (its copyCodeTo, FlushCache and
 *           all - which turns interrupts back ON on its way out, as the original's does), I_STAT and I_MASK read back,
 *           the jump, I_MASK restored, interrupts off then on, then print.
 * Each says first what it is about to do, so a console that hangs inside one names it.
 *
 * READ IT LIKE THIS. A bus error on the instruction fetch is exception 6 with EPC 1F801070h. COPY's read-back shows
 * what the instruction words did to the controller (I_MASK 0000h: everything masked while the original printed). If both
 * pass here on a console, code-in-io's hang was printing with I_MASK 0 under Unirom, not the CPU.
 *
 * THE RULE: run here first: `node hwtests/run.js cpu-code-in-irq`.
 * HERE AT v0.369: both a bus error (type 6) - but at EPC 1F801078h, not 1F801070h: this core FETCHES the two words at
 * 1F801070h and 1F801074h (I_STAT and I_MASK as instructions) and faults on the word after them. code-in-io asserts only
 * "a bus error", so it passes either way; a console's EPC says where the fault really is. (src/psx.js fetch() infers that
 * rule from code-in-io's six pass/fail rows: a fetch from I_STAT "reads back register state, executes it as garbage, and
 * runs off the end of that tiny register block into an unmapped gap". 1F801070h on a console withdraws it.) COPY's read-back: I_STAT and
 * I_MASK 00000000 (the nop in I_MASK). I_MASK reads ffff0000 after the exception (00000000 before).
 * THE OTHER EXPLANATION (v0.377.46, src/psx.js busWord / ioLast): a 32-bit DATA read of I_STAT or I_MASK takes its top
 * half from the main bus's last word (hwtests/imask-upper, all three consoles). Were an instruction FETCHED from them
 * the same, code-in-io's I_STAT word would be the top half of the line fill before it - the same on every console,
 * since the test's own FlushCache makes it so (0C00xxxxh, a jal, here) - and the test hangs exactly where the
 * consoles did; but then ps1-tests' console would hang too, and its log passes. So this core keeps the old top half for
 * a FETCH (the last I/O store's: the nop's 0), and the readings above stand at v0.377.46. If a console hangs here in
 * JUMP or COPY, or code-in-io hangs again under hwtests/runner (no Unirom printing), the fetch takes the bus's word and
 * ps1-tests' console differs in some way no one has named.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <exception.hpp>

typedef uint32_t (*func)();
extern "C" void FlushCache();
extern "C" void testCode();

struct Seen { int thrown, type; uint32_t epc, cause, badv, ra; };
static volatile Seen seen;

static void handler() {
    Thread* th = getCurrentThread();
    const auto ex = th->registers.cause.exception;
    uint32_t badv;
    __asm__ volatile("mfc0 %0, $8" : "=r"(badv));
    seen.thrown = 1; seen.type = (int)ex;
    seen.epc = th->registers.returnPC; seen.cause = th->registers.cause._reg; seen.badv = badv; seen.ra = th->registers.r[31];
    if (ex == cop0::CAUSE::Exception::busErrorInstruction) th->registers.returnPC = th->registers.r[31];
    else th->registers.returnPC += 4;
}

static void memcpy32(uint32_t* dst, uint32_t* src, size_t words) { for (size_t i = 0; i < words; i++) *dst++ = *src++; }
__attribute__((noinline)) static func copyCodeTo(uint32_t addr) {
    EnterCriticalSection();
    memcpy32((uint32_t*)addr, (uint32_t*)testCode, 2);
    FlushCache();
    ExitCriticalSection();
    return (func)addr;
}
static void clearSeen() { seen.thrown = 0; seen.type = -1; seen.epc = seen.cause = seen.badv = seen.ra = 0; }
static void report(const char* name, uint32_t imaskBefore, uint32_t istatBefore, uint32_t imaskAfter) {
    printf("%s: exception %s", name, seen.thrown ? "thrown" : "NOT thrown");
    if (seen.thrown) printf(", type %d%s, EPC %08x, Cause %08x, BadVaddr %08x", seen.type, seen.type == 6 ? " (bus error, instruction)" : "",
                            (unsigned)seen.epc, (unsigned)seen.cause, (unsigned)seen.badv);
    printf("\n  I_MASK before %08x, I_STAT before %08x; I_MASK when it came back %08x\n", (unsigned)imaskBefore, (unsigned)istatBefore, (unsigned)imaskAfter);
}

int main() {
    printf("\ncpu/cpu-code-in-irq\n");
    printf("code-in-io's testCodeInInterrupts, printing only after I_MASK is restored. No assertions.\n\n");
    EnterCriticalSection();
    hookUnresolvedExceptionHandler(handler);
    ExitCriticalSection();

    /* JUMP: the fetch alone. */
    printf("JUMP: about to jump to 1F801070h, nothing copied there\n");
    {
        EnterCriticalSection();
        const uint32_t imask = read32(0x1F801074), istat = read32(0x1F801070);
        clearSeen();
        ((func)0x1F801070)();
        const uint32_t imaskNow = read32(0x1F801074);
        write32(0x1F801074, imask);
        ExitCriticalSection();
        report("JUMP", imask, istat, imaskNow);
    }

    /* COPY: the original's steps. */
    printf("COPY: about to copy jr $ra; nop to 1F801070h and jump there\n");
    {
        EnterCriticalSection();
        const uint32_t imask = read32(0x1F801074), istat = read32(0x1F801070);
        clearSeen();
        func tested = copyCodeTo(0x1F801070);
        const uint32_t istatCopy = read32(0x1F801070), imaskCopy = read32(0x1F801074);
        tested();
        const uint32_t imaskNow = read32(0x1F801074);
        write32(0x1F801074, imask);
        ExitCriticalSection();
        report("COPY", imask, istat, imaskNow);
        printf("  after the copy (interrupts back on - copyCodeTo's ExitCriticalSection): I_STAT %08x, I_MASK %08x\n",
               (unsigned)istatCopy, (unsigned)imaskCopy);
    }

    printf("\ncpu/cpu-code-in-irq: Done.\n");
    for (;;) VSync(0);
}
