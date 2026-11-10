/*
 * cpu/code-in-io-prime - what the SCPH-1000 fetches from the hole after the interrupt controller, read by priming the bus
 * (2026-11-09).
 *
 * WHY. hwtests/code-in-io-irqsplit on the 1000 (PU-7, BIOS 1.0, CPU PRID 0): a jump into 1F801078h does not take the instruction
 * bus error every later console takes there - it ends in a data bus error at EPC 00000C9Ch, in the kernel's RAM - and code-in-io-
 * rest's SPU case never returned (STATUS "The SCPH-1000 runs no code out of I/O"). Likeliest: the hole hands an instruction fetch
 * the main bus's last word. This proves or kills that with no runaway.
 *
 * WHAT IT DOES. Interrupts off; a bar per step (red running, green back); results printed at the end.
 *   0  the SPU: code-in-io's copy (jr $ra; nop) into 1F801C00h, read back with two data reads (what the SPU returns) - no jump.
 *   1  the hole, the bus primed with BREAK: FlushCache, then the jump (jalr) at word 1 of a 16-byte-aligned line whose word 3 is a
 *      BREAK (0000000Dh, cause 9) - the line is filled from RAM just before the jump, so its last word is the bus's last word.
 *   2  the same with a reserved instruction (FC000000h, opcode 3Fh: cause 10) as word 3.
 *   3  as 1, the jump to 1F80107Ch (the hole's second word).
 * A console that fetches the bus's last word from the hole raises cause 9 / 10 / 9 at EPC 1F801078h / 1F801078h / 1F80107Ch; one
 * that decodes the hole raises an instruction bus error (cause 6) there - every later console and this core. The handler goes back
 * to the step's own return point on its FIRST exception, whatever it is (no instruction after it runs).
 */
#include <common.h>
#include <psxapi.h>
#include <string.h>
#include <exception.hpp>
#include <io.h>

extern "C" {
uint32_t guardLineBreak(uint32_t addr);
uint32_t guardLineRI(uint32_t addr);
void flushCache(void);
void prepare(uint32_t addr);
void safeHandlerEntry(void);
uint32_t guardSp, guardRet;
}

__asm__(
    ".set push\n.set noreorder\n"
    /* the line guards (code-in-io-safe's guardedCallLineJal, the line's last word changed): prepare (FlushCache only) in one aligned
     * line, the jump at word 1 of the next, its last word the marker */
    ".global guardLineBreak\nguardLineBreak:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  move $s0, $a0\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 1f\n  sw $t1, 0($t0)\n"
    "  .balign 16\n  jal prepare\n  move $a0, $s0\n  nop\n  nop\n"
    "  nop\n  jalr $s0\n  nop\n  .word 0x0000000D\n"
    "1:\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    ".global guardLineRI\nguardLineRI:\n"
    "  addiu $sp, $sp, -24\n  sw $ra, 20($sp)\n  sw $s0, 16($sp)\n  move $s0, $a0\n"
    "  la $t0, guardSp\n  sw $sp, 0($t0)\n"
    "  la $t0, guardRet\n  la $t1, 2f\n  sw $t1, 0($t0)\n"
    "  .balign 16\n  jal prepare\n  move $a0, $s0\n  nop\n  nop\n"
    "  nop\n  jalr $s0\n  nop\n  .word 0xFC000000\n"
    "2:\n  lw $s0, 16($sp)\n  lw $ra, 20($sp)\n  addiu $sp, $sp, 24\n  jr $ra\n  nop\n"
    ".global flushCache\nflushCache:\n  li $t1, 0x44\n  li $t2, 0xa0\n  jr $t2\n  nop\n"
    ".global safeHandlerEntry\nsafeHandlerEntry:\n"
    "  addiu $sp, $sp, -16\n  sw $ra, 12($sp)\n  sw $gp, 8($sp)\n  la $gp, _gp\n"
    "  jal safeHandler\n  nop\n"
    "  lw $gp, 8($sp)\n  lw $ra, 12($sp)\n  addiu $sp, $sp, 16\n  jr $ra\n  nop\n"
    ".set pop\n");

extern "C" void prepare(uint32_t) { flushCache(); }

struct Exc { uint32_t cause, epc, bad, ra; };
static Exc exc0;
static volatile int nExc;

static inline uint32_t badVaddr() { uint32_t v; __asm__ volatile("mfc0 %0, $8" : "=r"(v)); return v; }

extern "C" void safeHandler() {
    Thread* t = getCurrentThread();
    if (nExc == 0) { exc0.cause = (uint32_t)t->registers.cause.exception; exc0.epc = t->registers.returnPC; exc0.bad = badVaddr(); exc0.ra = t->registers.r[31]; }
    nExc++;
    t->registers.r[29] = guardSp;       /* the first exception, whatever it is: back to the step's own return point */
    t->registers.returnPC = guardRet;
}

static void bar(int k, uint32_t rgb) {
    for (int y = 0; y < 512; y += 256) {
        write32(0x1F801810, 0x02000000u | rgb);
        write32(0x1F801810, ((uint32_t)y << 16) | (uint32_t)(k * 64));
        write32(0x1F801810, (256u << 16) | 48u);
    }
}

static const char* NAMES[4] = { "the SPU, the copy read back", "the hole 1F801078h, the bus primed with BREAK (cause 9)",
                                "the hole 1F801078h, the bus primed with a reserved instruction (cause 10)",
                                "the hole 1F80107Ch, the bus primed with BREAK (cause 9)" };
static int stepExc[4];
static Exc stepFirst[4];
static uint32_t spu[2];

int main() {
    printf("\ncpu/code-in-io-prime\n");
    printf("What the hole after the interrupt controller hands a fetch, by priming the bus; the SPU's read-back.\n\n");
    EnterCriticalSection();
    *(volatile uint32_t*)(0x200 + 0x40 * 4) = (uint32_t)safeHandlerEntry;   /* hookUnresolvedExceptionHandler, as code-in-io-safe */
    ExitCriticalSection();
    for (int k = 0; k < 4; k++) {
        bar(k, 0x0000FF);
        EnterCriticalSection();
        nExc = 0;
        if (k == 0) {
            volatile uint32_t* p = (volatile uint32_t*)0x1F801C00;
            p[0] = 0x03E00008u; p[1] = 0;                        /* code-in-io's copy: jr $ra; nop */
            spu[0] = p[0]; spu[1] = p[1];
            p[0] = 0; p[1] = 0;                                  /* voice 0's volume, pitch and start back to 0 */
        } else if (k == 1) guardLineBreak(0x1F801078);
        else if (k == 2) guardLineRI(0x1F801078);
        else guardLineBreak(0x1F80107C);
        stepExc[k] = nExc; stepFirst[k] = exc0;
        ExitCriticalSection();
        bar(k, 0x00FF00);
    }
    printf("step 0 - %s: %08x %08x\n", NAMES[0], (unsigned)spu[0], (unsigned)spu[1]);
    for (int k = 1; k < 4; k++) {
        printf("step %d - %s: %d exception%s\n", k, NAMES[k], stepExc[k], stepExc[k] == 1 ? "" : "s");
        if (stepExc[k])
            printf("    first: cause %2u EPC %08x BadVaddr %08x ra %08x\n", (unsigned)stepFirst[k].cause, (unsigned)stepFirst[k].epc,
                   (unsigned)stepFirst[k].bad, (unsigned)stepFirst[k].ra);
    }
    printf("\ncpu/code-in-io-prime: Done.\n");
    for (;;) VSync(0);
}
