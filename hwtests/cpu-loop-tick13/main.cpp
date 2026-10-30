/*
 * cpu/cpu-loop-tick13 - a slow-bus read (the CD status register, SPUSTAT) by what follows it and what is queued before it
 * (2026-10-28)
 *
 * WHY. hwtests/cpu-loop-tick12 on the SCPH-9001: a CD / SPU register read behind queued RAM stores is not this core's "the
 * queue first, then the read, the CPU waiting": behind two DRAM pages' stores it costs what one store does, with nothing
 * reading its value the turn is this core's to the hundredth, with a writer of its register in the delay slot 2.6 longer. Does
 * the CPU run on past a slow read as it does past an on-die one, and when does its value come - and what waits for it: a
 * reader, a writer, a RAM load, an on-die read?
 *
 * WHAT IT DOES. Interrupts off; each loop 1,000 times between two Timer 2 reads, cold and hot, cycles an iteration. A = a RAM
 * word; every loop ends `addiu; bnez; nop`. R = lbu v0, CD (1F801800h) or lhu v0, SPUSTAT; "6n" = six nops.
 *   R-6N        R; 6n                          nothing reading the value
 *   R-W1        R; andi v0, v0; 5n             a writer of its register in the delay slot
 *   R-R2 / R-R4 R; nop; addu a2, v0, v0; 4n / R; 3n; addu; 2n     a reader at 2 / 4
 *   R-L1 / R-L3 R; lw a1, A; 5n / R; 2n; lw a1, A; 3n             a RAM load at 1 / 3
 *   R-I1        R; lhu v1, T2; 5n              an on-die read at 1
 *   S1-R / S2-R / SF-R / S3F-R   sw A+4 / sw A+4; sh A / sw A+4; sw A+16K / sw A+4; sw A+8K; sw A+16K - then R; 6n
 *   S2-R-W1     sw A+4; sh A; R; andi v0, v0; 5n
 * the CD rows first, then the same twelve with SPUSTAT.
 *
 * THE RULE: run it here first: `node hwtests/run.js cpu-loop-tick13 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define CDSTAT   0x1F801800u
#define SPUSTAT  0x1F801DAEu

static volatile uint32_t ramClk[0x1010] __attribute__((aligned(16)));

#define N 1000
/* %1 = the slow register, %2 = A, %3 = Timer 2 */
#define LOOP(name, body) static void name(uint32_t io, volatile uint32_t* p) { \
    uint32_t n = N; uint32_t t2 = T2_COUNT; \
    asm volatile(".set push\n.set noreorder\n1:\n" body "\n  addiu %0, %0, -1\n  bnez %0, 1b\n  nop\n.set pop\n" \
        : "+r"(n) : "r"(io), "r"(p), "r"(t2) : "v0", "v1", "a0", "a1", "a2", "a3", "t0", "ra", "memory"); }

#define N1 "  nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define N4 N2 N2
#define N5 N4 N1
#define N6 N3 N3
#define S1 "  sw $v0, 4(%2)\n"
#define S2 "  sw $v0, 4(%2)\n  sh $v0, 0(%2)\n"
#define SF "  sw $v0, 4(%2)\n  sw $v0, 0x4000(%2)\n"
#define S3F "  sw $v0, 4(%2)\n  sw $v0, 0x2000(%2)\n  sw $v0, 0x4000(%2)\n"
#define W1 "  andi $v0, $v0, 0xff\n"
#define RD "  addu $a2, $v0, $v0\n"
#define LDA "  lw $a1, 0(%2)\n"
#define IOR "  lhu $v1, 0(%3)\n"

#define SET(P, R) \
LOOP(P##6n, R N6) \
LOOP(P##w1, R W1 N5) \
LOOP(P##r2, R N1 RD N4) \
LOOP(P##r4, R N3 RD N2) \
LOOP(P##l1, R LDA N5) \
LOOP(P##l3, R N2 LDA N3) \
LOOP(P##i1, R IOR N5) \
LOOP(P##s1, S1 R N6) \
LOOP(P##s2, S2 R N6) \
LOOP(P##sf, SF R N6) \
LOOP(P##s3f, S3F R N6) \
LOOP(P##s2w1, S2 R W1 N5)
SET(cd, "  lbu $v0, 0(%1)\n")
SET(sp, "  lhu $v0, 0(%1)\n")

static uint32_t timeIt(void (*f)(uint32_t, volatile uint32_t*), uint32_t io) {
    const uint16_t a = read16(T2_COUNT);
    f(io, ramClk);
    const uint16_t b = read16(T2_COUNT);
    return (uint16_t)(b - a);
}

int main() {
    printf("\ncpu/cpu-loop-tick13\n");
    printf("A CD / SPU register read by what follows it and what is queued before it. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    struct V { const char* name; void (*f)(uint32_t, volatile uint32_t*); uint32_t io; };
#define ROWS(P, NAME, IO) { NAME "-6N", P##6n, IO }, { NAME "-W1", P##w1, IO }, { NAME "-R2", P##r2, IO }, { NAME "-R4", P##r4, IO }, \
    { NAME "-L1", P##l1, IO }, { NAME "-L3", P##l3, IO }, { NAME "-I1", P##i1, IO }, { NAME "-S1", P##s1, IO }, \
    { NAME "-S2", P##s2, IO }, { NAME "-SF", P##sf, IO }, { NAME "-S3F", P##s3f, IO }, { NAME "-S2W1", P##s2w1, IO }
    static const V vs[] = { ROWS(cd, "CD", CDSTAT), ROWS(sp, "SPU", SPUSTAT) };
    enum { NV = sizeof(vs) / sizeof(vs[0]) };
    static uint32_t c[NV][2];
    for (int i = 0; i < NV; i++) for (int k = 0; k < 2; k++) c[i][k] = timeIt(vs[i].f, vs[i].io);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("  variant     cold        hot      (cycles an iteration)\n");
    for (int i = 0; i < NV; i++)
        printf("  %-10s %3u.%02u     %3u.%02u\n", vs[i].name, (unsigned)(c[i][0] / N), (unsigned)(c[i][0] % N / 10),
               (unsigned)(c[i][1] / N), (unsigned)(c[i][1] % N / 10));
    printf("\ncpu/cpu-loop-tick13: Done.\n");
    for (;;) VSync(0);
}
