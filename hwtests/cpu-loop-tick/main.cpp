/*
 * cpu/cpu-loop-tick - the cycles of a Timer 2 polling loop, piece by piece (2026-10-28).
 *
 * WHY. spu-latch's wait loop (the KON written D cycles after SPUSTAT bit 11 changes; tools/consensus.js: 2,671 of its
 * rows outside the consoles' spread, the most of any SPU program) steps 31 cycles an iteration on the SCPH-7501 (its
 * printed delays 104, 135, 166, 197 ...) and 30 here (107, 137, 167, 194 ...; scratch/loopprobe.js: 30 most, 32 some).
 * The loop (libcommon's read16 called by jal, then the unwrapped clock kept in RAM at gp): jal, or, lhu T2, jr, andi,
 * lhu (gp), lw (gp), subu, andi, addu, subu, sltu, sw (gp), bnez, sh (gp) - fifteen instructions, a timer read, two RAM
 * loads, two RAM stores the timer read meets next time round. Which piece is the console's extra cycle no one test says.
 *
 * WHAT IT DOES. Interrupts off. Each variant runs its loop 1,000 times between two Timer 2 reads (system clock) and
 * prints the cycles an iteration (in hundredths), each run twice (the second with its lines cached).
 *   REAL      the loop as spu-latch compiles it (jal to a read16, the RAM clock at a pointer)
 *   INLINE    the same without the call (lhu T2 in line)
 *   T2        the timer read, andi, the count and the branch alone
 *   T2+LD     with the two RAM loads
 *   T2+ST     with the two RAM stores (none of the loads)
 *   T2+LD+ST  loads and stores, no call (INLINE in another order: the stores first)
 *   ST,NOP3   INLINE with three nops between the stores and the next timer read
 *   SPUSTAT   INLINE reading SPUSTAT (1F801DAEh) instead of Timer 2
 *   RAMREAD   INLINE reading a RAM word instead of Timer 2
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[4] __attribute__((aligned(16)));
static volatile uint32_t ramWord[4] __attribute__((aligned(16)));

extern "C" uint32_t rd16(uint32_t a);
asm(".section .text\n.set push\n.set noreorder\n.global rd16\nrd16:\n  lhu $v0, 0($a0)\n  jr $ra\n  andi $v0, $v0, 0xffff\n.set pop\n");

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

LOOP(loopReal,
    "  move $a0, %1\n  jal rd16\n  nop\n"
    "  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n  subu $v1, $v0, $v1\n  andi $v1, $v1, 0xffff\n  addu $v1, $v1, $a1\n"
    "  subu $a1, $v1, $a1\n  sltu $a1, $a1, %0\n  addiu %0, %0, -1\n  sw $v1, 4(%2)\n  bnez %0, 1b\n  sh $v0, 0(%2)\n")
LOOP(loopInline,
    "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n"
    "  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n  subu $v1, $v0, $v1\n  andi $v1, $v1, 0xffff\n  addu $v1, $v1, $a1\n"
    "  subu $a1, $v1, $a1\n  sltu $a1, $a1, %0\n  addiu %0, %0, -1\n  sw $v1, 4(%2)\n  bnez %0, 1b\n  sh $v0, 0(%2)\n")
LOOP(loopT2,
    "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(loopT2Ld,
    "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(loopT2St,
    "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  sw $v0, 4(%2)\n  bnez %0, 1b\n  sh $v0, 0(%2)\n")
LOOP(loopT2LdSt,
    "  sw $v1, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n"
    "  subu $v1, $v0, $v1\n  andi $v1, $v1, 0xffff\n  addu $v1, $v1, $a1\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(loopStNop3,
    "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n"
    "  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n  subu $v1, $v0, $v1\n  andi $v1, $v1, 0xffff\n  addu $v1, $v1, $a1\n"
    "  subu $a1, $v1, $a1\n  sltu $a1, $a1, %0\n  addiu %0, %0, -1\n  sw $v1, 4(%2)\n  sh $v0, 0(%2)\n  nop\n  nop\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick\n");
    printf("A Timer 2 polling loop's cycles an iteration, piece by piece (1,000 iterations). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; };
    static const V vs[] = {
        { "REAL", loopReal, T2_COUNT }, { "INLINE", loopInline, T2_COUNT }, { "T2", loopT2, T2_COUNT },
        { "T2+LD", loopT2Ld, T2_COUNT }, { "T2+ST", loopT2St, T2_COUNT }, { "T2+LD+ST", loopT2LdSt, T2_COUNT },
        { "ST,NOP3", loopStNop3, T2_COUNT }, { "SPUSTAT", loopInline, 0x1F801DAEu },
        { "RAMREAD", loopInline, (uint32_t)&ramWord[0] },
    };
    static uint32_t c[9][2];
    for (int i = 0; i < 9; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 9; i++)
        printf("  %-9s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick: Done.\n");
    for (;;) VSync(0);
}
