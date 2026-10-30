/*
 * cpu/cpu-loop-tick11 - the loops' turns one by one: n = 1..12 turns timed, so t(n) - t(n-1) is turn n (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick9 on the SCPH-9001: `sw A+4; lhu T2; sw A+12` (the second store in the read's delay slot) reads
 * 13.15 a turn with 0 or 2 nops after it - between the two shapes a model of the write queue can make there (11: the next
 * store goes on the delay-slot store's burst; 15: it does not). An average of 1,000 turns cannot say whether every turn is 13
 * or they alternate 11 / 15; this can.
 *
 * WHAT IT DOES. Interrupts off; for each loop and n = 1..12, the loop run n turns between two Timer 2 reads, eight times;
 * the least of the eight printed (cycles, the reads' own cost inside). Loops as cpu-loop-tick4/5/8/9 (A = a RAM word; every
 * loop ends `addiu; bnez; nop`):
 *   DS-0    sw A+4; lhu T2; sw A+12          DS-2  ... sw A+12; 2 nops        DS-4  ... sw A+12; 4 nops
 *   DS-2ST  sw A+4; lhu T2; sw A+12; sw A+20
 *   NEAR-1  sw A+4; lhu T2; nop; sw A+12     (15 a turn)
 *   SW-0    sw A+4; lhu T2                   (9)
 *   F-0     sw A; sw A+16K                   (12, a refresh 10)
 *   INL-ALU sw A+4; sh A; lhu v0, T2; andi v0; lhu v1, A; lw a1, A+4; subu v1, v0, v1; andi v1; addu v1, v1, a1   (32.00)
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick11 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));

#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p, uint32_t n) { \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

LOOP(ds0, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n")
LOOP(ds2, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n")
LOOP(ds4, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n  nop\n  nop\n")
LOOP(ds2st, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  sw $v0, 20(%2)\n")
LOOP(near1, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  nop\n  sw $v0, 12(%2)\n")
LOOP(sw0, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n")
LOOP(f0, "  sw $v0, 0(%2)\n  sw $v0, 0x4000(%2)\n")
LOOP(inlAlu, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n"
             "  subu $v1, $v0, $v1\n  andi $v1, $v1, 0xffff\n  addu $v1, $v1, $a1\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*, uint32_t), uint32_t n) {
    const uint16_t a = read16(T2_COUNT);
    f(T2_COUNT, ramClk, n);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick11\n");
    printf("Each loop timed for n = 1..12 turns, the least of eight (cycles). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*, uint32_t); };
    static const V vs[] = {
        { "DS-0", ds0 }, { "DS-2", ds2 }, { "DS-4", ds4 }, { "DS-2ST", ds2st }, { "NEAR-1", near1 }, { "SW-0", sw0 },
        { "F-0", f0 }, { "INL-ALU", inlAlu },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]), NT = 12, REP = 8 };
    static uint32_t t[NV][NT + 1];
    for (int i = 0; i < NV; i++)
        for (int n = 1; n <= NT; n++) {
            uint32_t best = 0xFFFFFFFFu;
            for (int r = 0; r < REP; r++) { const uint32_t c = timeIt(vs[i].f, n); if (c < best) best = c; }
            t[i][n] = best;
        }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant    t(1..12), then the turns t(n) - t(n-1) for n = 2..12\n");
    for (int i = 0; i < NV; i++) {
        printf("  %-8s", vs[i].name);
        for (int n = 1; n <= NT; n++) printf(" %u", (unsigned)t[i][n]);
        printf("\n  %-8s", "");
        for (int n = 2; n <= NT; n++) printf(" %d", (int)(t[i][n] - t[i][n - 1]));
        printf("\n");
    }
    printf("\ncpu/cpu-loop-tick11: Done.\n");
    for (;;) VSync(0);
}
