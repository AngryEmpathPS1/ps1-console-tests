/*
 * cpu/cpu-loop-tick22 - is cpu-loop-tick's two-cycle Timer 2 read the branch's or the spacing's? (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick on the SCPH-7501: `lhu T2; andi; addiu; bnez; nop` reads 9.05 cycles an iteration there and
 * 7.05 here, equal once two RAM loads follow the read (20.26 both). hwtests/io-shadow (I_STAT then Timer 2, k nops behind,
 * unrolled, no branch) fits the consoles to the hundredth here, so the loop's branch or its spacing is the question.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 iterations between two Timer 2 reads, cold and hot (cycles an iteration):
 *   T2+Kn      lhu T2; andi; K nops (K = 0-4); addiu; bnez; nop
 *   NOP,T2     nop; lhu T2; andi; addiu; bnez; nop          (the read off the branch target)
 *   T2,NOPDS   lhu T2; addiu; bnez; nop                     (no andi: nothing reads the value)
 *   UNROLL4    four times (lhu T2; andi; nop; nop; nop) then addiu; bnez; nop - printed per iteration (four reads)
 *   T2+1LD     lhu T2; andi; lw (RAM); addiu; bnez; nop     (one RAM load)
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick2 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[4] __attribute__((aligned(16)));

extern "C" uint32_t rd16(uint32_t a);
asm(".section .text\n.set push\n.set noreorder\n.global rd16\nrd16:\n  lhu $v0, 0($a0)\n  jr $ra\n  andi $v0, $v0, 0xffff\n.set pop\n");

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

LOOP(l0, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(l1, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  nop\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(l2, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  nop\n  nop\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(l3, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  nop\n  nop\n  nop\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(l4, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  nop\n  nop\n  nop\n  nop\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(lNopT2, "  nop\n  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(lNoAndi, "  lhu $v0, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
#define R4 "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  nop\n  nop\n  nop\n"
LOOP(lUnroll, R4 R4 R4 R4 "  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(l1Ld, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lw $a1, 4(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick2\n");
    printf("A Timer 2 read in a loop: the branch's or the spacing's (cycles a read). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); int per; };
    static const V vs[] = {
        { "T2+0n", l0, 1 }, { "T2+1n", l1, 1 }, { "T2+2n", l2, 1 }, { "T2+3n", l3, 1 }, { "T2+4n", l4, 1 },
        { "NOP,T2", lNopT2, 1 }, { "T2,NOPDS", lNoAndi, 1 }, { "UNROLL4", lUnroll, 4 }, { "T2+1LD", l1Ld, 1 },
    };
    static uint32_t c[9][2];
    for (int i = 0; i < 9; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, T2_COUNT);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 9; i++)
        printf("  %-9s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick2: Done.\n");
    for (;;) VSync(0);
}
