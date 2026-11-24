/*
 * cpu/cpu-loop-tick19 - two on-die reads, an SPU read and three RAM stores a turn, taken apart (2026-11-24, the SPU session).
 *
 * WHY. hwtests/spu-irqstat's watch loop (`lhu Timer 2; lhu I_STAT; lhu SPUSTAT; sh; sh; sh; addiu; bne; nop`, the stores of
 * what was read) is 34 cycles a turn on every console (six of them, two boots each: the Timer 2 stamps it stores are 34 apart)
 * and 35 on this core, and a DRAM refresh inside a turn costs it ~2 cycles there and 9 here. The SPU's IRQ places were read
 * through that loop, so its turn is in every one of them. Which instruction owes the cycle is not in that log: this takes the
 * loop apart.
 *
 * WHAT IT DOES. As cpu-loop-tick18: interrupts off; each loop 500 times between two Timer 2 reads, cold, hot and uncached
 * (through its KSEG1 alias), cycles an iteration (the refresh's share is in the hundredths). T = Timer 2's count (1F801120h),
 * I = I_STAT (1F801070h), P = SPUSTAT (1F801DAEh); A = a RAM word; every loop ends `addiu; bnez; nop`. 1F801014h is left as
 * the runner has it and printed.
 *   FULL       lhu v1,T; lhu a1,I; lhu a3,P; sh v1,A+8; sh a1,A+10; sh a3,A+12      spu-irqstat's turn
 *   FULL-O     lhu v1,T; lhu a1,I; lhu a3,P; sh a2,A+8; sh a2,A+10; sh a2,A+12      ... storing another register
 *   FULL-N1    lhu v1,T; lhu a1,I; lhu a3,P; nop; sh v1; sh a1; sh a3                ... a nop behind the SPU read
 *   NO-ST      lhu v1,T; lhu a1,I; lhu a3,P; nop; nop; nop                          no stores
 *   ST1        lhu v1,T; lhu a1,I; lhu a3,P; sh v1,A+8; nop; nop                    one store
 *   ST2        lhu v1,T; lhu a1,I; lhu a3,P; sh v1,A+8; sh a1,A+10; nop             two
 *   SPU-ST3    nop; nop; lhu a3,P; sh a2; sh a2; sh a3                              no on-die read
 *   T-SPU      lhu v1,T; nop; lhu a3,P; sh v1; sh a2; sh a3                         Timer 2 alone ahead of it
 *   I-SPU      nop; lhu a1,I; lhu a3,P; sh a2; sh a1; sh a3                         I_STAT alone right ahead of it
 *   IO2-ST3    lhu v1,T; lhu a1,I; nop; sh v1; sh a1; sh a2                         no SPU read
 *   SPU-ONLY   nop; nop; lhu a3,P; nop; nop; nop                                    the SPU read alone
 *   ST3        nop; nop; nop; sh a2; sh a2; sh a2                                   the stores alone
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick19 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramW[16] __attribute__((aligned(64)));
#define SPAD ((volatile uint32_t*)0x1F800000u)

#define N 500   /* 1,000 turns of the uncached rows pass Timer 2's 65,536 */
/* %1 = A (RAM), %2 = S (scratchpad), %3 = Timer 2's address (I_STAT -B0h from it, SPUSTAT +C8Eh); label 2: is the loop's end. */
static uint32_t seg;   /* 0 cached, 20000000h through the KSEG1 alias */
#define LOOP(name, body) static void __attribute__((noinline)) name(volatile uint32_t* a, volatile uint32_t* s) { \
    uint32_t n = N; uint32_t t2 = T2_COUNT; uint32_t sg = seg; \
    asm volatile(".set push\n.set noreorder\n" \
        "la $t0, 2f\n  or $t0, $t0, %4\n  sw $t0, 0(%1)\n  sw $t0, 0(%2)\n" \
        "1:\n" body "\n2:\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(a), "r"(s), "r"(t2), "r"(sg) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define RT "  lhu $v1, 0(%3)\n"
#define RI "  lhu $a1, -0xB0(%3)\n"
#define RP "  lhu $a3, 0xC8E(%3)\n"
#define NP "  nop\n"
LOOP(full,    RT RI RP "  sh $v1, 8(%1)\n  sh $a1, 10(%1)\n  sh $a3, 12(%1)\n")
LOOP(fullO,   RT RI RP "  sh $a2, 8(%1)\n  sh $a2, 10(%1)\n  sh $a2, 12(%1)\n")
LOOP(fullN1,  RT RI RP NP "  sh $v1, 8(%1)\n  sh $a1, 10(%1)\n  sh $a3, 12(%1)\n")
LOOP(noSt,    RT RI RP NP NP NP)
LOOP(st1,     RT RI RP "  sh $v1, 8(%1)\n" NP NP)
LOOP(st2,     RT RI RP "  sh $v1, 8(%1)\n  sh $a1, 10(%1)\n" NP)
LOOP(spuSt3,  NP NP RP "  sh $a2, 8(%1)\n  sh $a2, 10(%1)\n  sh $a3, 12(%1)\n")
LOOP(tSpu,    RT NP RP "  sh $v1, 8(%1)\n  sh $a2, 10(%1)\n  sh $a3, 12(%1)\n")
LOOP(iSpu,    NP RI RP "  sh $a2, 8(%1)\n  sh $a1, 10(%1)\n  sh $a3, 12(%1)\n")
LOOP(io2St3,  RT RI NP "  sh $v1, 8(%1)\n  sh $a1, 10(%1)\n  sh $a2, 12(%1)\n")
LOOP(spuOnly, NP NP RP NP NP NP)
LOOP(st3,     NP NP NP "  sh $a2, 8(%1)\n  sh $a2, 10(%1)\n  sh $a2, 12(%1)\n")

typedef void (*LoopFn)(volatile uint32_t*, volatile uint32_t*);
static uint32_t timeIt(LoopFn f0, bool uncached) {
    seg = uncached ? 0x20000000u : 0;
    const LoopFn f = uncached ? (LoopFn)((uint32_t)f0 | 0xA0000000u) : f0;
    const uint16_t a = read16(T2_COUNT);
    f(ramW, SPAD);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick19\n");
    printf("two on-die reads, an SPU read and three RAM stores a turn, taken apart. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    const uint32_t spuDelay = read32(0x1F801014);
    struct V { const char* name; void (*f)(volatile uint32_t*, volatile uint32_t*); };
    static const V vs[] = {
        { "FULL", full }, { "FULL-O", fullO }, { "FULL-N1", fullN1 }, { "NO-ST", noSt }, { "ST1", st1 }, { "ST2", st2 },
        { "SPU-ST3", spuSt3 }, { "T-SPU", tSpu }, { "I-SPU", iSpu }, { "IO2-ST3", io2St3 }, { "SPU-ONLY", spuOnly }, { "ST3", st3 },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][3];
    for (int i = 0; i < NV; i++) { for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, false); c[i][2] = timeIt(vs[i].f, true); }
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  1F801014h = %08x\n", (unsigned)spuDelay);
    printf("  variant     cold        hot     uncached   (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N * 100 / N),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N * 100 / N), (unsigned)(c[i][2] / N), (unsigned)(c[i][2] % N * 100 / N));
    printf("\ncpu/cpu-loop-tick19: Done.\n");
    for (;;) VSync(0);
}
