/*
 * cpu/cd-store - what does a store to the CD controller's registers cost the CPU?
 *
 * WHY (M377). M373 sent SPU and CD stores through the write buffer, their bus time nocash's formula with the
 * register's WRITE delay (bits 0-3), because hwtests/spu-xfer showed on all three consoles that an SPU store costs a
 * nine-instruction loop nothing. Before M373 every SPU and CD store stalled the CPU the region's READ delay. The CD
 * half took the SPU's rule by analogy - no console has timed a CD store - and the SCPH-9001's own drive program,
 * run under this core (CDROMLLE), then passed fewer of nugget's console-measured CD tests: 103 -> 101 of 105 at
 * refresh phase 0, and fewer over the other phases too (STATUS M377). A drive program meets the CPU only through
 * these registers, so what a store to them costs is part of every CD exchange.
 *
 * WHAT IT DOES. Interrupts off, the drive left alone. Every row is a per-iteration cost above a loop of nops in the
 * same slots, 100 iterations, from cached code (as cpu/slow-b2b). The stores write 0 to 1F801800h, the index
 * register, which selects bank 0 and does nothing else. The control is the SPU: stores of 0 to 1F801D84h (the
 * reverb output volume, left; nothing plays), whose cost spu-xfer measured as nothing.
 *   STORES       sb to the CD alone, 2, 4, 6 and 8 back to back - the write buffer holds four, so a store that only
 *                queues costs nothing until the fifth; one that stalls costs its time every time.
 *   STORE, LOAD  sb to the CD then lbu from 1F801800h with 0-6 nops between - does the load wait for the store?
 *   CONTROLS     lbu from the CD alone (cpu/mem-delay reads 7.85-8.04 for 8 bits), sh to the SPU 1, 4 and 8.
 *
 * READ IT LIKE THIS. A CD store that goes into the write buffer reads ~0 for 1-4 stores and grows past four; one
 * that stalls the CPU reads about its bus time for one store and that many again for each more. The load rows say
 * how long a CD store keeps the bus.
 *
 * THE RULE: run it here first: `node hwtests/run.js cd-store`.
 * ON THIS CORE AT v0.377 (2026-10-06): sb x1 / x2 / x4 / x6 / x8 0.00 / 4.70 / 22.56 / 40.54 / 50.58 (the write
 * buffer: an 8-bit CD store holds the bus 9 cycles at the kernel's 00020843h, so stores faster than that back up and
 * the fifth waits); sb;lbu 14.96, then 13.96 / 12.91 / 10.92 / 9.93 with 1/2/4/6 nops; lbu alone 6.96; the SPU's sh
 * 0.00 / 14.64 / 34.64 for 1/4/8 (7 cycles of bus). From M373 to M376 a full buffer never stalled (the stall was lost,
 * STATUS M377) and every STORES row read 0. On M372's rule (a CD store stalls the CPU its read time): sb 6.91 per
 * store (6.91 / 13.91 / 27.91 / 41.96 / 56.05), sb;lbu 13.9 at every spacing, the SPU's sh 16.91 / 67.91 / 135.98.
 */
#include <common.h>
#include <measure.hpp>

#define COUNT 100

#define N1 "nop\n"
#define N2 N1 N1
#define N3 N2 N1
#define N4 N2 N2
#define N5 N4 N1
#define N6 N4 N2
#define N7 N4 N3
#define N8 N4 N4
#define N9 N8 N1
#define N10 N8 N2
#define N12 N8 N4
#define N14 N8 N6
#define N16 N8 N8
#define CDS  "sb   $zero, 0($t7)\n"         /* 1F801800h: the index register, bank 0 */
#define CDL  "lbu  $t1, 0($t7)\n"
#define SPS  "sh   $zero, 0($t6)\n"         /* 1F801D84h: reverb output volume, left */
#define CDS2 CDS CDS
#define CDS4 CDS2 CDS2
#define CDS6 CDS4 CDS2
#define CDS8 CDS4 CDS4
#define SPS4 SPS SPS SPS SPS
#define SPS8 SPS4 SPS4

#define LOOP_BODY(INSTR)                        \
    ".set push\n"                               \
    ".set noreorder\n"                          \
    ".set noat\n"                               \
    "li   $t7, 0xBF801800\n"                    \
    "li   $t6, 0xBF801D84\n"                    \
    "li   $t2, %[n]\n"                          \
    "1:\n"                                      \
    "nop\n"                                     \
    INSTR                                       \
    "addiu $t3, $t3, 1\n"                       \
    "addiu $t2, $t2, -1\n"                      \
    "bnez  $t2, 1b\n"                           \
    "nop\n"                                     \
    ".set pop\n"

#define MEASURE(INSTR)                                                          \
    measure<MeasureMethod::CpuClock>([] {                                       \
        asm volatile(LOOP_BODY(INSTR)                                           \
                     :: [n] "i"(COUNT) : "t1","t2","t3","t6","t7","memory");    \
    })

static void row(const char* name, uint32_t cycles, uint32_t base) {
    uint32_t d = cycles > base ? cycles - base : 0;
    printf("  %-18s %6u - %6u = %6u over %d  ->  %u.%02u cycles\n",
           name, cycles, base, d, COUNT, d / COUNT, d % COUNT);
}

int main() {
    printf("\ncpu/cd-store\n");
    printf("What a store to the CD controller's registers costs the CPU (STATUS M377).\n");
    printf("Per-iteration cost above a loop of nops in the same slots, %d iterations.\n", COUNT);
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();

    const uint32_t b8 = MEASURE(N8), b16 = MEASURE(N16);

    printf("STORES to 1F801800h (above 8 nops; 8 stores above 16)\n");
    row("sb alone", MEASURE(CDS N7), b8);
    row("sb x2", MEASURE(CDS2 N6), b8);
    row("sb x4", MEASURE(CDS4 N4), b8);
    row("sb x6", MEASURE(CDS6 N2), b8);
    row("sb x8", MEASURE(CDS8 N8), b16);

    printf("\nSTORE, THEN A LOAD from 1F801800h (above 8 nops)\n");
    row("sb;lbu", MEASURE(CDS CDL N6), b8);
    row("sb;nop;lbu", MEASURE(CDS N1 CDL N5), b8);
    row("sb;2nop;lbu", MEASURE(CDS N2 CDL N4), b8);
    row("sb;4nop;lbu", MEASURE(CDS N4 CDL N2), b8);
    row("sb;6nop;lbu", MEASURE(CDS N6 CDL), b8);

    printf("\nCONTROLS\n");
    row("lbu alone", MEASURE(CDL N7), b8);
    row("SPU sh alone", MEASURE(SPS N7), b8);
    row("SPU sh x4", MEASURE(SPS4 N4), b8);
    row("SPU sh x8", MEASURE(SPS8 N8), b16);

    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
