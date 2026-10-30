/*
 * cpu/cpu-loop-tick8 - a RAM load behind stores and a store-queued Timer 2 read; far-page store streams by load; the store in
 * the read's delay slot (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick4-7 on the SCPH-9001 fit a knob (experiments/_mkbusrd211.js BUSRD 5 + BUSRDP + WRLP: a store to the
 * DRAM page of the last queued store waits in the CPU until the on-die read behind the queue is off the bus, 3 cycles after the
 * queue) in 47 of 64 rows, src/ 27. What it leaves: cpu-loop-tick's INLINE / REAL / T2+LD+ST - stores, the read, RAM loads of
 * the stored words - 1.4-1.9 a turn short, tick4's SWSH-0+L (sw; sh; lhu T2; lw) 0.38 short (a RAM load waiting for the hold
 * reads it 0.84 over); two stores on two DRAM pages back to back 12.30 a turn here, 12.50 on the console, with or without a read
 * (tick6 NOP-2K: the refresh, or the row change, costs the console more in a saturated stream); and tick5's NEAR-0 (the second
 * store in the read's delay slot) 13.16, between the knob's 15.08 and 11.16.
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word; every loop ends `addiu; bnez; nop`.
 *   SWSH-L      sw A+4; sh A; lw A+8                       a RAM load behind two stores, no read
 *   SWSH-T-L    sw A+4; sh A; lhu T2; lw A+8               (= tick4 SWSH-0+L)
 *   SWSH-T-1L   ... lhu T2; nop; lw A+8                    the load 1, 2, 4 instructions behind the read
 *   SWSH-T-2L   ... lhu T2; nop; nop; lw A+8
 *   SWSH-T-4L   ... lhu T2; 4 nops; lw A+8
 *   SWSH-T-LF   sw A+4; sh A; lhu T2; lw A+16K             the load to another DRAM page
 *   SW-T-L      sw A+4; lhu T2; lw A+8                     one store
 *   INL         sw A+4; sh A; lhu T2; andi v0; lhu A; lw A+4   cpu-loop-tick INLINE's shape: loads of the stored words
 *   INL-NOT2    sw A+4; sh A; lhu A; lw A+4                the same without the read
 *   INL-1       sw A+4; sh A; lhu T2; andi v0; lhu A       one load
 *   F-0         sw A; sw A+16K                             two DRAM pages back to back: the bus 12, the CPU 5
 *   F-4         sw A; 4 nops; sw A+16K                     the CPU 9
 *   F-7         sw A; 7 nops; sw A+16K                     the CPU 12
 *   F4P         sw A; sw A+2K; sw A+4K; sw A+6K            four pages
 *   D-NOFIRST   lhu T2; sw A+12                            a store in the read's delay slot, nothing queued before
 *   D-NOP       sw A+4; nop; lhu T2; sw A+12               a nop between the first store and the read
 *   D-SH        sw A+4; lhu T2; sh A+12                    the delay-slot store a halfword (tick5 NEAR-0 with sh)
 *   D-LW        sw A+4; lw T2; sw A+12                     a word read
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick8 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u

static volatile uint32_t ramClk[0x1810] __attribute__((aligned(16)));   /* 24 KB + : the far stores land DRAM pages away */

#define N 1000
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

LOOP(swshL, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lw $a1, 8(%2)\n")
LOOP(swshTL, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  lw $a1, 8(%2)\n")
LOOP(swshT1L, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  lw $a1, 8(%2)\n")
LOOP(swshT2L, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  lw $a1, 8(%2)\n")
LOOP(swshT4L, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  nop\n  nop\n  nop\n  nop\n  lw $a1, 8(%2)\n")
LOOP(swshTLF, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%1)\n  lw $a1, 0x4000(%2)\n")
LOOP(swTL, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  lw $a1, 8(%2)\n")
LOOP(inl, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n")
LOOP(inlNoT2, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v1, 0(%2)\n  lw $a1, 4(%2)\n")
LOOP(inl1, "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n  lhu $v0, 0(%1)\n  andi $v0, $v0, 0xffff\n  lhu $v1, 0(%2)\n")
LOOP(f0, "  sw $v0, 0(%2)\n  sw $v0, 0x4000(%2)\n")
LOOP(f4, "  sw $v0, 0(%2)\n  nop\n  nop\n  nop\n  nop\n  sw $v0, 0x4000(%2)\n")
LOOP(f7, "  sw $v0, 0(%2)\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n  nop\n  sw $v0, 0x4000(%2)\n")
LOOP(f4p, "  sw $v0, 0(%2)\n  sw $v0, 0x800(%2)\n  sw $v0, 0x1000(%2)\n  sw $v0, 0x1800(%2)\n")
LOOP(dNoFirst, "  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n")
LOOP(dNop, "  sw $v0, 4(%2)\n  nop\n  lhu $v1, 0(%1)\n  sw $v0, 12(%2)\n")
LOOP(dSh, "  sw $v0, 4(%2)\n  lhu $v1, 0(%1)\n  sh $v0, 12(%2)\n")
LOOP(dLw, "  sw $v0, 4(%2)\n  lw $v1, 0(%1)\n  sw $v0, 12(%2)\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick8\n");
    printf("A RAM load behind stores and a read; far-page store streams; the delay-slot store. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); };
    static const V vs[] = {
        { "SWSH-L", swshL }, { "SWSH-T-L", swshTL }, { "SWSH-T-1L", swshT1L }, { "SWSH-T-2L", swshT2L },
        { "SWSH-T-4L", swshT4L }, { "SWSH-T-LF", swshTLF }, { "SW-T-L", swTL }, { "INL", inl }, { "INL-NOT2", inlNoT2 },
        { "INL-1", inl1 }, { "F-0", f0 }, { "F-4", f4 }, { "F-7", f7 }, { "F4P", f4p }, { "D-NOFIRST", dNoFirst },
        { "D-NOP", dNop }, { "D-SH", dSh }, { "D-LW", dLw },
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
    printf("\ncpu/cpu-loop-tick8: Done.\n");
    for (;;) VSync(0);
}
