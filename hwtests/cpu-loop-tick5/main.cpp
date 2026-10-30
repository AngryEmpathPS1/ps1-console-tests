/*
 * cpu/cpu-loop-tick5 - after a Timer 2 read held behind a queued store: when may the NEXT store start? (2026-10-28 night)
 *
 * WHY. hwtests/cpu-loop-tick4 on the SCPH-7501 and 7502: `sw; lhu T2; ...` loops read as if the bus carried the store's
 * word and then ~3 cycles of the read before the next store could start (9.2 a turn for one store, 11.2 for two); a knob
 * holding the bus that long after such a read (experiments/_mkbusrd211.js BUSRD) fits 9 of 11 tick4 rows to the hundredth -
 * and moves bios-window's bracket (a store-queued read, then an exception handler's two stores) 2 cycles off every console.
 * Which is it: a hold the next store always meets, or something about where / how soon the next store comes?
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration:
 *   NEAR-k   sw (to A); lhu v1, T2; k nops; sw (to A+8, the same DRAM page); addiu; bnez; nop      k = 0 1 2 3 4 6
 *   FAR-k    the same, the second store to a word 16 KB away (another DRAM page)                   k = 0 2 4
 *   NOREAD   sw; nop; sw (A+8); addiu; bnez; nop - no read between
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick5 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));   /* 16 KB + : the FAR store lands a DRAM page away */

extern "C" uint32_t rd16(uint32_t a);
asm(".section .text\n.set push\n.set noreorder\n.global rd16\nrd16:\n  lhu $v0, 0($a0)\n  jr $ra\n  andi $v0, $v0, 0xffff\n.set pop\n");

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

LOOP(near0, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(near1, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(near2, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(near3, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  nop\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(near4, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  nop\n  nop\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(near6, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(far0, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 0x4000(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(far2, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  sw $v0, 0x4000(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(far4, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  nop\n  nop\n  sw $v0, 0x4000(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(noread, "  sw $v0, 4(%2)\n  nop\n  sw $v0, 12(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick5\n");
    printf("After a store-queued Timer 2 read: when the next store may start. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; int per; };
    static const V vs[] = {
        { "NEAR-0", near0, T2_COUNT, 1 },
        { "NEAR-1", near1, T2_COUNT, 1 },
        { "NEAR-2", near2, T2_COUNT, 1 },
        { "NEAR-3", near3, T2_COUNT, 1 },
        { "NEAR-4", near4, T2_COUNT, 1 },
        { "NEAR-6", near6, T2_COUNT, 1 },
        { "FAR-0", far0, T2_COUNT, 1 },
        { "FAR-2", far2, T2_COUNT, 1 },
        { "FAR-4", far4, T2_COUNT, 1 },
        { "NOREAD", noread, T2_COUNT, 1 },
    };
    static uint32_t c[10][2];
    for (int i = 0; i < 10; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 10; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick5: Done.\n");
    for (;;) VSync(0);
}
