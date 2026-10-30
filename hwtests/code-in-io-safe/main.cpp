/*
 * cpu/code-in-io-safe - ps1-tests' cpu/code-in-io, every case as its author wrote it, with an exception handler that
 * cannot loop (2026-10-08 night, after the SCPH-7501 froze in code-in-io under the test runner).
 *
 * WHY. code-in-io copies `jr $ra; nop` into RAM, the scratchpad, the MDEC, the interrupt controller (I_STAT/I_MASK), the
 * SPU and two DMA registers, FlushCaches, and jumps there; its handler, on a bus error on the fetch, returns to $ra, and
 * on any other exception skips the instruction (common/exception.cpp). Its author's console passes all seven (psx.log),
 * and so does this core. The three consoles hung on it through Unirom (console day: after "pass - testCodeInMDEC",
 * put down to Unirom's printing with I_MASK 0 - hwtests/cpu-code-in-irq, the interrupt case alone printed afterwards,
 * PASSED on the consoles), and the SCPH-7501 froze in it under hwtests/runner on 2026-10-08 (20:07: no byte, no answer
 * to 'V', until a reset) - where nothing prints until the end, so where it stopped is unknown. One way it loops for ever
 * (STATUS v0.377.46): an instruction fetched from I/O that is not the stored word - a jal - changes $ra, the handler
 * returns there, faults again. Another: junk that is not a bus error, skipped one word at a time into more junk.
 *
 * WHAT IT DOES. The same seven cases, in the same order, with the same copy (two words), FlushCache and jump - the copy
 * done by prepare(), the original's copyCodeTo word for word, called by the guard itself. Each case is called through a
 * guard that keeps its stack and its own return point; the handler (entered with the program's own $gp), on a bus error
 * on the fetch, returns THERE with that stack, whatever $ra holds - and on any other exception skips the instruction as
 * the original does, sixteen times at most a case, then returns there too. Each exception is kept (its cause, EPC,
 * BadVaddr, $ra) and the first four printed per case, with the original's own pass/fail. Before each case a red bar is
 * filled down the whole of VRAM (x = 64 k .. 64 k + 47), green once it returns: a console that freezes anyway shows on
 * the television which case it froze in. The two words at each target are read back AFTER the case (a read before the
 * jump would change the main bus's last word).
 * AND FOUR TIMES MORE, THE INTERRUPT CASE PRIMED. On a scratch core with the bus-word fetch rule (an instruction fetched
 * from a 16-bit I/O register takes its top half from the main bus's last word, as a 32-bit data read does - v0.377.46)
 * the ORIGINAL hangs right after testCodeInMDEC, as all three consoles did on console day, while this program passes
 * there: its guard changes what the bus carried last. First with one RAM load just before the jump, of 0C001234h (a jal)
 * and of 0 - which move nothing even on that core: there the word fetched is the last of the I-CACHE LINE FILL before
 * the jump, and the original's jump sits in a line ending in the jal of its next assertion. So then the jump at word 1
 * of an aligned line that FlushCache left uncached, the line's last word a jal (to the guard's own return point,
 * harmless if it ever ran) or a nop.
 *
 * READ IT LIKE THIS. "Interrupts, line ends in a jal" decides the rule: a console that fetches the bus's last word from
 * I/O runs that jal from 1F801070h - "ra moved", a bus error where the jal leads - and one that does not faults at
 * 1F801078h as the plain case. "16 exceptions" is a runaway the original could not leave.
 *
 * THE RULE: run it here first: `node hwtests/run.js code-in-io-safe --ntsc --quiet=2000`.
 * HERE AT v0.377.52 (an I/O fetch keeps the last I/O store's top half, ioLast): all eleven pass - RAM, SPU, DMA0, DMA
 * control no exception; the scratchpad a bus error at 1F800000h, the MDEC at 1F801828h, the interrupt controller at
 * 1F801078h in all five of its cases, $ra the jump's own. ON A SCRATCH CORE WITH THE BUS-WORD FETCH RULE (openBus16 taking
 * busValue() for a fetch too): the ORIGINAL hangs after "pass - testCodeInMDEC" (console day's place on all three
 * consoles); this program finishes, every case passing its assertion, and THREE INTERRUPT CASES MOVE $ra: the plain one
 * (a bus error at 1F801078h with $ra 00000001h), the RAM-primed jal and "line ends in a jal" (each a jal run from
 * 1F801070h: a bus error at EPC 0FFFFFFCh, $ra 10000004h); the nop-primed and "line ends in a nop" at 1F801078h, $ra the
 * jump's. So on a console: any "ra moved" in the interrupt cases is the bus-word rule (this core's fetch rule is then the
 * wrong one for these consoles), none is the rule this core has. Which cases move depends on the code's layout - this
 * build's; the line guards' do not.
 */
#include <common.h>
#include <psxapi.h>
#include <string.h>
#include <exception.hpp>
#include <io.h>

extern "C" {
uint32_t guardedCall(uint32_t addr);
uint32_t guardedCallPrimed(uint32_t addr, const volatile uint32_t* prime);
uint32_t guardedCallLineJal(uint32_t addr);
uint32_t guardedCallLineNop(uint32_t addr);
void flushCache(void);
void prepare(uint32_t addr);
void safeHandlerEntry(void);
extern uint32_t testCode[];
uint32_t guardSp, guardRet;
}

__asm__(
    ".set push\n.set noreorder\n"
    /* guardedCall(addr): its stack and its return point kept, then prepare (the original's copyCodeTo), then the jump;
     * the handler can always come back to 1: */
    ".global guardedCall\nguardedCall:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  move $s0, $a0\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 1f\n  sw $t1, 0($t0)\n"
    "  jal prepare\n  move $a0, $s0\n"
    "  jalr $s0\n  nop\n"
    "1:\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    /* guardedCallPrimed(addr, prime): the same, with one RAM load of *prime right before the jump */
    ".global guardedCallPrimed\nguardedCallPrimed:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  sw $s1, 12($sp)\n  move $s0, $a0\n  move $s1, $a1\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 2f\n  sw $t1, 0($t0)\n"
    "  jal prepare\n  move $a0, $s0\n"
    "  lw $t0, 0($s1)\n"
    "  jalr $s0\n  nop\n"
    "2:\n  lw $s1, 12($sp)\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    /* guardedCallLineJal / guardedCallLineNop(addr): prepare's call in one aligned line, the jump at word 1 of the next
     * - FlushCache (in prepare) leaves both uncached, so the jump's line is filled just before the jump, its LAST word
     * the main bus's last - and that word a jal (to the guard's own return point) or a nop */
    ".global guardedCallLineJal\nguardedCallLineJal:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  move $s0, $a0\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 3f\n  sw $t1, 0($t0)\n"
    "  .balign 16\n  jal prepare\n  move $a0, $s0\n  nop\n  nop\n"
    "  nop\n  jalr $s0\n  nop\n  jal 3f\n"
    "3:\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    ".global guardedCallLineNop\nguardedCallLineNop:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  move $s0, $a0\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 4f\n  sw $t1, 0($t0)\n"
    "  .balign 16\n  jal prepare\n  move $a0, $s0\n  nop\n  nop\n"
    "  nop\n  jalr $s0\n  nop\n  nop\n"
    "4:\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    /* A(44h) FlushCache, as the original's code.s */
    ".global flushCache\nflushCache:\n  li $t1, 0x44\n  li $t2, 0xa0\n  jr $t2\n  nop\n"
    /* the code copied: jr $ra; nop, as the original's */
    ".global testCode\ntestCode:\n  jr $ra\n  nop\n"
    /* the handler's entry: the kernel calls it with ITS $gp, and the program's small globals (guardSp, guardRet, nExc)
     * are $gp-relative - without this the handler read 0 for the guard and sent the CPU to address 0 (the virtual
     * console, 2026-10-08: a reserved instruction at 00000068h with $sp 0 after the scratchpad case's bus error) */
    ".global safeHandlerEntry\nsafeHandlerEntry:\n"
    "  addiu $sp, $sp, -16\n  sw $ra, 12($sp)\n  sw $gp, 8($sp)\n  la $gp, _gp\n"
    "  jal safeHandler\n  nop\n"
    "  lw $gp, 8($sp)\n  lw $ra, 12($sp)\n  addiu $sp, $sp, 16\n  jr $ra\n  nop\n"
    ".set pop\n");

/* the original's copyCodeTo, word for word */
extern "C" void prepare(uint32_t addr) {
    EnterCriticalSection();
    ((volatile uint32_t*)addr)[0] = testCode[0];
    ((volatile uint32_t*)addr)[1] = testCode[1];
    flushCache();
    ExitCriticalSection();
}

struct Exc { uint32_t cause, epc, bad, ra; };
static Exc excs[16];
static volatile int nExc;

static inline uint32_t badVaddr() { uint32_t v; __asm__ volatile("mfc0 %0, $8" : "=r"(v)); return v; }

extern "C" void safeHandler() {
    Thread* t = getCurrentThread();
    const uint32_t cause = (uint32_t)t->registers.cause.exception;
    if (nExc < 16) { excs[nExc].cause = cause; excs[nExc].epc = t->registers.returnPC; excs[nExc].bad = badVaddr(); excs[nExc].ra = t->registers.r[31]; }
    nExc++;
    if (cause == (uint32_t)cop0::CAUSE::Exception::busErrorInstruction || nExc >= 16) {
        t->registers.r[29] = guardSp;        /* the guard's stack */
        t->registers.returnPC = guardRet;    /* and its return point, whatever $ra holds */
    } else {
        t->registers.returnPC += 4;          /* the original: skip the failing instruction */
    }
}

static void bar(int k, uint32_t rgb) {
    /* GP0(02h) fill: colour, then y,x, then h,w - down the whole of VRAM, in two halves */
    for (int y = 0; y < 512; y += 256) {
        write32(0x1F801810, 0x02000000u | rgb);
        write32(0x1F801810, ((uint32_t)y << 16) | (uint32_t)(k * 64));
        write32(0x1F801810, (256u << 16) | 48u);
    }
}

/* the RAM-load primes: a jal (0C001234h: to 100048D0h, nothing there) and zero */
static const volatile uint32_t PRIME_JAL = 0x0C001234u, PRIME_ZERO = 0;
struct Case { const char* name; uint32_t addr; bool expectBusError; bool imask; const volatile uint32_t* prime; int line; };
#define NCASE 11
static const Case CASES[NCASE] = {
    { "testCodeInRam", 0x00040000, false, false, 0, 0 },
    { "testCodeInScratchpad", 0x1F800000, true, false, 0, 0 },
    { "testCodeInMDEC", 0x1F801820, true, false, 0, 0 },
    { "testCodeInInterrupts", 0x1F801070, true, true, 0, 0 },
    { "testCodeInSPU", 0x1F801C00, false, false, 0, 0 },
    { "testCodeInDMA0", 0x1F801084, false, false, 0, 0 },
    { "testCodeInDMAControl", 0x1F8010F0, false, false, 0, 0 },
    { "Interrupts, bus primed 0C001234h", 0x1F801070, true, true, &PRIME_JAL, 0 },
    { "Interrupts, bus primed 00000000h", 0x1F801070, true, true, &PRIME_ZERO, 0 },
    { "Interrupts, line ends in a jal", 0x1F801070, true, true, 0, 1 },
    { "Interrupts, line ends in a nop", 0x1F801070, true, true, 0, 2 },
};
static uint32_t readBack[NCASE][2], caseRet[NCASE];
static int caseExc[NCASE];
static Exc caseFirst[NCASE][4];
static bool casePass[NCASE];

int main() {
    printf("\ncpu/code-in-io-safe\n");
    printf("ps1-tests' code-in-io, every case as written, with a handler that cannot loop. No assertions of its own.\n\n");
    EnterCriticalSection();
    *(volatile uint32_t*)(0x200 + 0x40 * 4) = (uint32_t)safeHandlerEntry;   /* hookUnresolvedExceptionHandler, as common/exception.cpp */
    ExitCriticalSection();
    for (int k = 0; k < NCASE; k++) {
        const Case& c = CASES[k];
        bar(k, 0x0000FF);                                       /* red: running */
        uint32_t imask = 0;
        if (c.imask) { EnterCriticalSection(); imask = read32(0x1F801074); }
        nExc = 0;
        if (c.line == 1) guardedCallLineJal(c.addr);              /* the jump's line filled just before it, ending in a jal */
        else if (c.line == 2) guardedCallLineNop(c.addr);         /* ... ending in a nop */
        else if (c.prime) guardedCallPrimed(c.addr, c.prime);    /* a RAM load of the prime just before the jump */
        else guardedCall(c.addr);                               /* the copy, FlushCache and the jump */
        caseExc[k] = nExc;
        caseRet[k] = guardRet;
        for (int i = 0; i < 4 && i < nExc; i++) caseFirst[k][i] = excs[i];
        casePass[k] = c.expectBusError ? (nExc > 0 && excs[0].cause == (uint32_t)cop0::CAUSE::Exception::busErrorInstruction)
                                       : nExc == 0;
        if (c.imask) { write32(0x1F801074, imask); ExitCriticalSection(); }
        readBack[k][0] = ((volatile uint32_t*)c.addr)[0];       /* afterwards: a read before the jump moves the bus */
        readBack[k][1] = ((volatile uint32_t*)c.addr)[1];
        bar(k, 0x00FF00);                                       /* green: back */
    }
    for (int k = 0; k < NCASE; k++) {
        printf("%s - %s: %d exception%s, read back after %08x %08x\n", casePass[k] ? "pass" : "FAIL", CASES[k].name, caseExc[k],
               caseExc[k] == 1 ? "" : "s", (unsigned)readBack[k][0], (unsigned)readBack[k][1]);
        for (int i = 0; i < 4 && i < caseExc[k]; i++)
            printf("    #%d cause %2u EPC %08x BadVaddr %08x ra %08x%s\n", i, (unsigned)caseFirst[k][i].cause,
                   (unsigned)caseFirst[k][i].epc, (unsigned)caseFirst[k][i].bad, (unsigned)caseFirst[k][i].ra,
                   /* the jump's own return address is the guard's label, or (the line guards) the word before it */
                   caseFirst[k][i].ra == caseRet[k] || caseFirst[k][i].ra == caseRet[k] - 4 ? "" : "  (ra moved: a jal fetched)");
    }
    printf("\ncpu/code-in-io-safe: Done.\n");
    for (;;) VSync(0);
}
