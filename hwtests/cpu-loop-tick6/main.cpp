/*
 * cpu/cpu-loop-tick6 - two stores with a Timer 2 read or a nop between, by how far apart they land (2026-10-28 night).
 *
 * WHY. hwtests/cpu-loop-tick5 on the SCPH-9001 (two runs to the hundredth): `sw A; lhu T2; k nops; sw A+8` reads 13.2 a turn
 * at k = 0 and 15.1 at k = 1-6, and the same with the second store 16 KB away 12.5 - FASTER on another page; with a nop for
 * the read, 6.1. Neither this core (10.2 / 12.25 / 11.7 / 11.2) nor a bus hold after the read (flat 11.2) is that. So the
 * read breaks the DRAM burst, and the second store's cost depends on where it lands: which distance is "another page"?
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration:
 *   RD-d    sw (A); lhu v1, T2; nop; sw (A + d); addiu; bnez; nop     d = 8, 64, 2 KB, 16 KB
 *   NOP-d   sw (A); nop;        nop; sw (A + d); addiu; bnez; nop     d = 8, 64, 2 KB, 16 KB
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick6 --ntsc`.
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

LOOP(rd8, "  sw $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 8(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(rd64, "  sw $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 64(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(rd2K, "  sw $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 2048(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(rd16K, "  sw $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 16384(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(nop8, "  sw $v0, 0(%2)\n  nop\n  nop\n  sw $v0, 8(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(nop64, "  sw $v0, 0(%2)\n  nop\n  nop\n  sw $v0, 64(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(nop2K, "  sw $v0, 0(%2)\n  nop\n  nop\n  sw $v0, 2048(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(nop16K, "  sw $v0, 0(%2)\n  nop\n  nop\n  sw $v0, 16384(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick6\n");
    printf("Two stores, a Timer 2 read or a nop between, by distance. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; int per; };
    static const V vs[] = {
        { "RD-8", rd8, T2_COUNT, 1 },
        { "RD-64", rd64, T2_COUNT, 1 },
        { "RD-2K", rd2K, T2_COUNT, 1 },
        { "RD-16K", rd16K, T2_COUNT, 1 },
        { "NOP-8", nop8, T2_COUNT, 1 },
        { "NOP-64", nop64, T2_COUNT, 1 },
        { "NOP-2K", nop2K, T2_COUNT, 1 },
        { "NOP-16K", nop16K, T2_COUNT, 1 },
    };
    static uint32_t c[8][2];
    for (int i = 0; i < 8; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 8; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick6: Done.\n");
    for (;;) VSync(0);
}
