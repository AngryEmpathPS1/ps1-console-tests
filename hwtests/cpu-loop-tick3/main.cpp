/*
 * cpu/cpu-loop-tick3 - which instruction after an on-die read pays its two cycles: a reader, a writer, where (2026-10-28).
 *
 * WHY. hwtests/cpu-loop-tick2 on the SCPH-7501: `lhu v0, T2; andi v0, v0` in a loop costs two cycles more than here at every
 * spacing (9 / 7 ... 13 / 11; four unrolled 39 / 31), nothing without the andi (7 / 7) or with a RAM load behind it (14.27
 * both). The andi both reads and writes the loaded register, in the load's delay slot. spu-latch's read16 (`lhu v0; jr ra;
 * andi v0, v0`, the andi second) pays one (31 / 30).
 *
 * WHAT IT DOES. Interrupts off; each loop (the variant, then addiu; bnez; nop) 1,000 times between two Timer 2 reads, cold
 * and hot, cycles an iteration:
 *   RD1 / RD2 / RD3   lhu v0, T2 then `andi v1, v0` at position 1 / 2 / 3 (nops before it) - a reader only
 *   WR1 / WR2         `addiu v0, zero, 1` at position 1 / 2 - a writer only
 *   BOTH1 / BOTH2     `andi v0, v0` at position 1 / 2 (cpu-loop-tick2's T2+0n; read16's shape)
 *   ISTAT1            I_STAT (1F801070h) read, `andi v0, v0` at 1
 *   LW1               lw (a word) of Timer 2, `andi v0, v0` at 1
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick3 --ntsc`.
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

LOOP(rd1, "  lhu $v0, 0(%1)\n  andi $v1, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(rd2, "  lhu $v0, 0(%1)\n  nop\n  andi $v1, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(rd3, "  lhu $v0, 0(%1)\n  nop\n  nop\n  andi $v1, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(wr1, "  lhu $v0, 0(%1)\n  addiu $v0, $zero, 1\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(wr2, "  lhu $v0, 0(%1)\n  nop\n  addiu $v0, $zero, 1\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(both1, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(both2, "  lhu $v0, 0(%1)\n  nop\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(istat1, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")
LOOP(lw1, "  lw $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick3\n");
    printf("Which instruction after an on-die read pays its two cycles. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; int per; };
    static const V vs[] = {
        { "RD1", rd1, T2_COUNT, 1 }, { "RD2", rd2, T2_COUNT, 1 }, { "RD3", rd3, T2_COUNT, 1 }, { "WR1", wr1, T2_COUNT, 1 },
        { "WR2", wr2, T2_COUNT, 1 }, { "BOTH1", both1, T2_COUNT, 1 }, { "BOTH2", both2, T2_COUNT, 1 },
        { "ISTAT1", istat1, 0x1F801070u, 1 }, { "LW1", lw1, T2_COUNT, 1 },
    };
    static uint32_t c[9][2];
    for (int i = 0; i < 9; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < 9; i++)
        printf("  %-9s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick3: Done.\n");
    for (;;) VSync(0);
}
