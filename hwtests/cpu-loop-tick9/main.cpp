/*
 * cpu/cpu-loop-tick9 - the reader of a RAM load behind stores and a store-queued Timer 2 read; the store in the read's delay
 * slot by what follows it (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick8 on the SCPH-9001: `sw; sh; lhu T2; andi; lhu A; lw A+4` (INL) reads 25.63 a turn on the console
 * and here, but cpu-loop-tick's T2+LD+ST - the same with three ALU instructions reading the loaded registers - 32.02 against
 * 30.15: the console's RAM loads there look to let the CPU run on and charge their readers, where this core's load waits. And
 * the store in the read's delay slot (`sw A+4; lhu T2; sw A+12`) reads 13.16 whatever comes before it (tick8 D-*), where
 * experiments/_mkbusrd211.js BUSRD 5 reads 15.08 and with that store exempt 11.16.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word; every loop ends `addiu; bnez; nop`. INL = sw A+4; sh A; lhu v0, T2; andi v0; lhu v1, A; lw a1, A+4.
 *   INL-R2      INL; nop; addu a2, a1, a1             a reader of the lw two behind it
 *   INL-R3      INL; 2 nops; addu a2, a1, a1
 *   INL-R5      INL; 4 nops; addu a2, a1, a1
 *   INL-V2      INL; subu a2, v1, v1                  a reader of the lhu two behind it
 *   INL-ALU     INL; subu v1, v0, v1; andi v1; addu v1, v1, a1   (= cpu-loop-tick T2+LD+ST)
 *   INL-ALU-NOT2  INL-ALU without the Timer 2 read and its andi
 *   INL-ALU-NOST  INL-ALU without the two stores (cpu-loop-tick T2+LD with the ALU)
 *   LD-R2       lhu v1, A; lw a1, A+4; nop; addu a2, a1, a1     the loads alone
 *   SW-LD-R2    sw A+4; lw a1, A+8; nop; addu a2, a1, a1        one store, a load, its reader
 *   SW-T-LD-R2  sw A+4; lhu T2; lw a1, A+8; nop; addu a2, a1, a1
 *   SW-T-LD-R5  sw A+4; lhu T2; lw a1, A+8; 4 nops; addu a2, a1, a1
 *   DS-k        sw A+4; lhu T2; sw A+12; k nops        the delay-slot store, k = 0 2 4 6
 *   DS-2ST      sw A+4; lhu T2; sw A+12; sw A+20        two stores after the read
 *   DS-FAR      sw A+4; lhu T2; sw A+16K               the delay-slot store to another page
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick9 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));   /* 16 KB + : the far store lands a DRAM page away */

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define INL "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n"
#define ALU "  subu $v1, $v0, $v1\n  andi $v1, $v1, 0xffff\n  addu $v1, $v1, $a1\n"
LOOP(inlR2, INL "  nop\n  addu $a2, $a1, $a1\n")
LOOP(inlR3, INL "  nop\n  nop\n  addu $a2, $a1, $a1\n")
LOOP(inlR5, INL "  nop\n  nop\n  nop\n  nop\n  addu $a2, $a1, $a1\n")
LOOP(inlV2, INL "  subu $a2, $v1, $v1\n")
LOOP(inlAlu, INL ALU)
LOOP(inlAluNoT2, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n" ALU)
LOOP(inlAluNoSt, "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n" ALU)
LOOP(ldR2, "  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n  nop\n  addu $a2, $a1, $a1\n")
LOOP(swLdR2, "  sw $v0, 4(%2)\n  lw $a1, 8(%2)\n  nop\n  addu $a2, $a1, $a1\n")
LOOP(swTLdR2, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  lw $a1, 8(%2)\n  nop\n  addu $a2, $a1, $a1\n")
LOOP(swTLdR5, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  lw $a1, 8(%2)\n  nop\n  nop\n  nop\n  nop\n  addu $a2, $a1, $a1\n")
LOOP(ds0, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n")
LOOP(ds2, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n")
LOOP(ds4, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n  nop\n  nop\n")
LOOP(ds6, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n")
LOOP(ds2st, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  sw $v0, 20(%2)\n")
LOOP(dsFar, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 0x4000(%2)\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick9\n");
    printf("The reader of a RAM load behind stores and a read; the delay-slot store. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); };
    static const V vs[] = {
        { "INL-R2", inlR2 }, { "INL-R3", inlR3 }, { "INL-R5", inlR5 }, { "INL-V2", inlV2 }, { "INL-ALU", inlAlu },
        { "INL-ALU-NT", inlAluNoT2 }, { "INL-ALU-NS", inlAluNoSt }, { "LD-R2", ldR2 }, { "SW-LD-R2", swLdR2 },
        { "SW-T-LD-R2", swTLdR2 }, { "SW-T-LD-R5", swTLdR5 }, { "DS-0", ds0 }, { "DS-2", ds2 }, { "DS-4", ds4 },
        { "DS-6", ds6 }, { "DS-2ST", ds2st }, { "DS-FAR", dsFar },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][2];
    for (int i = 0; i < NV; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, T2_COUNT);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick9: Done.\n");
    for (;;) VSync(0);
}
