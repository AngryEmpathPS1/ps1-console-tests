/*
 * cpu/cpu-loop-tick10 - the reader of the first of two RAM loads after a read of Timer 2, SPUSTAT or nothing; the store in the
 * read's delay slot, filled in (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick9 on the SCPH-9001: a reader of the first of two back-to-back RAM loads (other registers), in the
 * second's delay slot, waits about a cycle (INL-V2 +0.88; experiments/_mklov211.js LOV 1 makes it, cpu-loop-tick's REAL and
 * RAMREAD and tick9's INL-ALU-NT / -NS exact) - but cpu-loop-tick's SPUSTAT row (the same loop, the read SPUSTAT) is exact
 * without it and 1.43 over with it. Does the wait need something before the loads, and not an SPU read? And tick9's DS rows
 * (sw A+4; lhu T2; sw A+12 in the read's delay slot; k nops) read 13.15 13.15 15.21 17.09 for k = 0 2 4 6, between the two
 * shapes this core can make (11 / 15): the odd k, and the delay-slot store against a store to the same word, behind a
 * halfword, behind two stores.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word; every loop ends `addiu; bnez; nop`. LD = lhu v1, A; lw a1, A+4. V = subu a2, v1, v1 (the first load's reader in the
 * lw's delay slot).
 *   LD / LD-V          LD, without and with V                         nothing before the loads
 *   T2-LD / T2-LD-V    lhu v0, T2; andi v0; LD (V)                     (= tick9 INL-ALU-NS's start)
 *   SPU-LD / SPU-LD-V  lhu v0, SPUSTAT; andi v0; LD (V)
 *   SPU-ST-LD(-V)      sw A+4; sh A; lhu v0, SPUSTAT; andi v0; LD (V)  (= cpu-loop-tick SPUSTAT's shape)
 *   N2-LD-V            2 nops; LD; V
 *   DS-1 / -3 / -5     sw A+4; lhu T2; sw A+12; k nops
 *   DS-SAMEW           sw A+4; lhu T2; sw A+4                         the delay-slot store to the first store's word
 *   DS-SH              sh A+4; lhu T2; sw A+12                        a halfword first
 *   DS-2PRE            sw A+4; sw A+8; lhu T2; sw A+12                two stores first
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick10 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPUSTAT  0x1F801DAEu

static volatile uint32_t ramClk[0x40] __attribute__((aligned(16)));

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define LD "  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n"
#define RV "  subu $a2, $v1, $v1\n"
#define RD "  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n"
#define ST "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n"
LOOP(ld, LD)
LOOP(ldV, LD RV)
LOOP(rdLd, RD LD)
LOOP(rdLdV, RD LD RV)
LOOP(stRdLd, ST RD LD)
LOOP(stRdLdV, ST RD LD RV)
LOOP(n2LdV, "  nop\n  nop\n" LD RV)
LOOP(ds1, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n")
LOOP(ds3, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n  nop\n")
LOOP(ds5, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n  nop\n  nop\n  nop\n  nop\n  nop\n")
LOOP(dsSameW, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 4(%2)\n")
LOOP(dsSh, "  sh $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n")
LOOP(ds2Pre, "  sw $v0, 4(%2)\n  sw $v0, 8(%2)\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick10\n");
    printf("The first of two RAM loads' reader after Timer 2, SPUSTAT or nothing; the delay-slot store. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; };
    static const V vs[] = {
        { "LD", ld, T2_COUNT }, { "LD-V", ldV, T2_COUNT }, { "T2-LD", rdLd, T2_COUNT }, { "T2-LD-V", rdLdV, T2_COUNT },
        { "SPU-LD", rdLd, SPUSTAT }, { "SPU-LD-V", rdLdV, SPUSTAT }, { "SPU-ST-LD", stRdLd, SPUSTAT },
        { "SPU-ST-LDV", stRdLdV, SPUSTAT }, { "N2-LD-V", n2LdV, T2_COUNT }, { "DS-1", ds1, T2_COUNT }, { "DS-3", ds3, T2_COUNT },
        { "DS-5", ds5, T2_COUNT }, { "DS-SAMEW", dsSameW, T2_COUNT }, { "DS-SH", dsSh, T2_COUNT }, { "DS-2PRE", ds2Pre, T2_COUNT },
    };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][2];
    for (int i = 0; i < NV; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick10: Done.\n");
    for (;;) VSync(0);
}
