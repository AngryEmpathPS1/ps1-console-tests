/*
 * cpu/code-in-io-irqsplit - code-in-io's interrupt-controller case taken apart, one step at a time, each printed as it ends
 * (2026-11-09).
 *
 * WHY. The SCPH-1000 (PU-7, BIOS 1.0, CPU PRID 0) froze in hwtests/code-in-io-safe's case 4, testCodeInInterrupts, twice (its red
 * bar on the screen; the second time with no disc in and the console cooled), and in ps1-tests' original right after "pass -
 * testCodeInMDEC". The case copies `jr $ra; nop` over I_STAT and I_MASK (1F801070h / 74h: the store to I_STAT acknowledges, the
 * one to I_MASK masks everything), flushes the caches and jumps to 1F801070h: the CPU fetches I_STAT as an instruction, then I_MASK,
 * then 1F801078h - where every later console takes an instruction bus error (and this core). So the 1000 hangs somewhere in: what
 * I_STAT reads as an instruction, what I_MASK reads as, or the fetch from the hole at 1F801078h - or a plain DATA read of it.
 *
 * WHAT IT DOES. Interrupts off for each step; a bar per step down VRAM (red while it runs, green once back: a step that hangs shows
 * which on the television), and each step's line printed as soon as it is back (under Unirom's debug mode they come over the cable
 * as they happen). In order, least likely to hang first:
 *   0-3  a data read (lw, not in a delay slot) of 1F801070h, 1F801074h, 1F801078h, 1F80107Ch - its value or its exception;
 *   4    a jump to 1F801078h, nothing copied - the hole fetched first;
 *   5    I_MASK set 0, a jump to 1F801074h, nothing copied - I_MASK fetched, then the hole;
 *   6    code-in-io's case 4 as written: the copy over I_STAT / I_MASK, a flush, a jump to 1F801070h.
 * code-in-io-safe's guard and handler: an instruction bus error returns to the step's own return point, any other exception skips
 * the instruction, sixteen at most a step. I_MASK put back after each step.
 */
#include <common.h>
#include <psxapi.h>
#include <string.h>
#include <exception.hpp>
#include <io.h>

extern "C" {
uint32_t guardedCall(uint32_t addr);
uint32_t readWord(uint32_t addr);
void flushCache(void);
void prepare(uint32_t addr);
void safeHandlerEntry(void);
extern uint32_t testCode[];
uint32_t guardSp, guardRet, noCopy, stopFirst;
}

__asm__(
    ".set push\n.set noreorder\n"
    /* guardedCall(addr): its stack and its return point kept, then prepare (the copy unless noCopy, and FlushCache), the jump */
    ".global guardedCall\nguardedCall:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  move $s0, $a0\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 1f\n  sw $t1, 0($t0)\n"
    "  jal prepare\n  move $a0, $s0\n"
    "  jalr $s0\n  nop\n"
    "1:\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    /* readWord(addr): one lw, not in a delay slot, so the handler's skip lands on the nop after it */
    ".global readWord\nreadWord:\n  li $v0, 0x5EEDF00D\n  lw $v0, 0($a0)\n  nop\n  jr $ra\n  nop\n"
    ".global flushCache\nflushCache:\n  li $t1, 0x44\n  li $t2, 0xa0\n  jr $t2\n  nop\n"
    ".global testCode\ntestCode:\n  jr $ra\n  nop\n"
    ".global safeHandlerEntry\nsafeHandlerEntry:\n"
    "  addiu $sp, $sp, -16\n  sw $ra, 12($sp)\n  sw $gp, 8($sp)\n  la $gp, _gp\n"
    "  jal safeHandler\n  nop\n"
    "  lw $gp, 8($sp)\n  lw $ra, 12($sp)\n  addiu $sp, $sp, 16\n  jr $ra\n  nop\n"
    ".set pop\n");

extern "C" void prepare(uint32_t addr) {
    if (!noCopy) {
        ((volatile uint32_t*)addr)[0] = testCode[0];
        ((volatile uint32_t*)addr)[1] = testCode[1];
    }
    flushCache();
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
    /* stopFirst (the jump steps, 2026-11-09 second build): back to the guard on ANY first exception - the first build's step 4
     * came back from the hole with 16 exceptions on the SCPH-1000 (not an instruction bus error: the handler's +4 then ran the DMA
     * registers after the hole as code) and hung printing them */
    if (cause == (uint32_t)cop0::CAUSE::Exception::busErrorInstruction || nExc >= 16 || stopFirst) {
        t->registers.r[29] = guardSp;
        t->registers.returnPC = guardRet;
    } else {
        t->registers.returnPC += 4;
    }
}

static void bar(int k, uint32_t rgb) {
    for (int y = 0; y < 512; y += 256) {
        write32(0x1F801810, 0x02000000u | rgb);
        write32(0x1F801810, ((uint32_t)y << 16) | (uint32_t)(k * 64));
        write32(0x1F801810, (256u << 16) | 48u);
    }
}

struct Step { const char* name; int kind; uint32_t addr; };
#define NSTEP 7
static const Step STEPS[NSTEP] = {
    { "read I_STAT 1F801070h", 0, 0x1F801070 },
    { "read I_MASK 1F801074h", 0, 0x1F801074 },
    { "read the hole 1F801078h", 0, 0x1F801078 },
    { "read the hole 1F80107Ch", 0, 0x1F80107C },
    { "jump to the hole 1F801078h, nothing copied", 1, 0x1F801078 },
    { "I_MASK 0, jump to 1F801074h, nothing copied", 2, 0x1F801074 },
    { "case 4 as written: the copy, a jump to 1F801070h", 3, 0x1F801070 },
};

int main() {
    printf("\ncpu/code-in-io-irqsplit\n");
    printf("code-in-io's interrupt-controller case in steps; each line printed when its step is back.\n\n");
    EnterCriticalSection();
    *(volatile uint32_t*)(0x200 + 0x40 * 4) = (uint32_t)safeHandlerEntry;   /* hookUnresolvedExceptionHandler, as code-in-io-safe */
    ExitCriticalSection();
    for (int k = 0; k < NSTEP; k++) {
        const Step& s = STEPS[k];
        bar(k, 0x0000FF);
        EnterCriticalSection();
        const uint32_t imask = read32(0x1F801074);
        nExc = 0;
        uint32_t value = 0;
        if (s.kind == 0) value = readWord(s.addr);
        else {
            stopFirst = 1;
            noCopy = s.kind == 3 ? 0 : 1;
            if (s.kind == 2) write32(0x1F801074, 0);
            guardedCall(s.addr);
        }
        const int n = nExc;
        stopFirst = 0;
        write32(0x1F801074, imask);
        ExitCriticalSection();
        bar(k, 0x00FF00);
        if (s.kind == 0) printf("step %d - %s: %08x, %d exception%s\n", k, s.name, (unsigned)value, n, n == 1 ? "" : "s");
        else printf("step %d - %s: back, %d exception%s\n", k, s.name, n, n == 1 ? "" : "s");
        for (int i = 0; i < 4 && i < n; i++)
            printf("    #%d cause %2u EPC %08x BadVaddr %08x ra %08x\n", i, (unsigned)excs[i].cause, (unsigned)excs[i].epc,
                   (unsigned)excs[i].bad, (unsigned)excs[i].ra);
    }
    printf("\ncpu/code-in-io-irqsplit: Done.\n");
    for (;;) VSync(0);
}
