/*
 * cpu/cpu-loop-tick4 - RAM stores just before a Timer 2 read, by kind, count and distance (2026-10-28).
 *
 * WHY. hwtests/cpu-loop-tick on the SCPH-7501: two RAM stores (sw; sh in the branch's delay slot) right before the next
 * Timer 2 read cost ~2 cycles more there than here (INLINE 32.05 / 30.20, T2+LD+ST alike), none with three nops between
 * (ST,NOP3 32.07 / 32.05); stores with no RAM loads a cycle LESS there (T2+ST 12.25 / 13.19). v0.377.210 fitted the writer
 * half; this is spu-latch's wait loop's other cycle.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. Every
 * loop: [stores] [k nops] lhu v1, T2; [a RAM load or not]; addiu; bnez; nop (the read into v1: no writer behind it).
 *   SW-k      one sw, k nops (k = 0 1 2 3)             SH-0    one sh, no nops
 *   SWSH-k    sw then sh, k nops (k = 0 1 2)           SWSW-0  two sw
 *   SWSH-0+L  sw, sh, the read, then lw a RAM word     NONE+L  the read and the lw only
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick4 --ntsc`.
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

LOOP(sw0, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(sw1, "  sw $v0, 4(%2)\n  nop\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(sw2, "  sw $v0, 4(%2)\n  nop\n  nop\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(sw3, "  sw $v0, 4(%2)\n  nop\n  nop\n  nop\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(sh0, "  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(swsh0, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(swsh1, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  nop\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(swsh2, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  nop\n  nop\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(swsw0, "  sw $v0, 4(%2)\n  sw $v0, 8(%2)\n  lhu $v1, 0(%1)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(swsh0l, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  lw $a1, 8(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(nonel, "  lhu $v1, 0(%1)\n  lw $a1, 8(%2)\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick4\n");
    printf("RAM stores just before a Timer 2 read, by kind, count and distance. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; int per; };
    static const V vs[] = {
        { "SW-0", sw0, T2_COUNT, 1 },
        { "SW-1", sw1, T2_COUNT, 1 },
        { "SW-2", sw2, T2_COUNT, 1 },
        { "SW-3", sw3, T2_COUNT, 1 },
        { "SH-0", sh0, T2_COUNT, 1 },
        { "SWSH-0", swsh0, T2_COUNT, 1 },
        { "SWSH-1", swsh1, T2_COUNT, 1 },
        { "SWSH-2", swsh2, T2_COUNT, 1 },
        { "SWSW-0", swsw0, T2_COUNT, 1 },
        { "SWSH-0+L", swsh0l, T2_COUNT, 1 },
        { "NONE+L", nonel, T2_COUNT, 1 },
    };
    static uint32_t c[11][2];
    for (int i = 0; i < 11; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 11; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick4: Done.\n");
    for (;;) VSync(0);
}
