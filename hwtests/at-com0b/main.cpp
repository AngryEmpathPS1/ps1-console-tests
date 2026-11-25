/*
 * cpu/at-com0b - a read of ANOTHER region behind an SPU read at COM0 12-15: which values, how far apart, which way round
 * (2026-11-25, the SPU session; follows hwtests/at-com0)
 *
 * WHY. expRecovery's KNOWN-OPEN: hwtests/at-com0 on the SCPH-7001 and 1001 (three runs each, identical) is this core's to the
 * cycle in every cell but one - COM_DELAY 132Fh (COM0 15), SPUSTAT then CDSTAT: 1989 against 1926 over 64 turns, a cycle a
 * turn. One cell: whether it is COM0 15 alone or anything over some value, whether a nop between the reads takes it away, and
 * whether the CD read ahead of the SPU one or an on-die or ROM read behind it pays as well, is not in that log.
 *
 * WHAT IT DOES. Interrupts masked, Timer 2 at the system clock, cached loops of 64 turns, the fastest of 8 runs. For COM_DELAY
 * 132Ch 132Dh 132Eh 132Fh (COM0 12-15), 130Fh and 102Fh (COM1 0, COM2 0 beside COM0 15) and 31125h (the kernel's):
 *   SC   lhu SPUSTAT ; lbu CDSTAT            at-com0's shape
 *   SC1  lhu SPUSTAT ; nop ; lbu CDSTAT      SC2 / SC3: two / three nops between
 *   CS   lbu CDSTAT ; lhu SPUSTAT            the other way round
 *   SB   lhu SPUSTAT ; lbu BFC00000h         a ROM read behind it
 *   ST   lhu SPUSTAT ; lhu Timer 1           an on-die read behind it
 *   S3   lhu SPUSTAT ; nop ; nop ; nop       the SPU read alone, as long as SC3
 * A row: the value, then the eight shapes' cycles over 64 turns. COM_DELAY is put back as found.
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-com0b`.
 */

#include <common.h>

#define COM_DELAY (*(volatile uint32_t *)0x1F801020u)
#define I_MASK    (*(volatile uint32_t *)0x1F801074u)

extern "C" uint32_t b_sc(void), b_sc1(void), b_sc2(void), b_sc3(void), b_cs(void), b_sb(void), b_st(void), b_s3(void);
/* each: Timer 2 read, 64 turns of the shape, Timer 2 read; the difference (wrapped at 16 bits) */
#define LOOPFN(name, body) \
  ".balign 16\n.global " #name "\n" #name ":\n  lui $t8, 0x1F80\n  lui $t5, 0xBFC0\n  li $t9, 64\n  lhu $t6, 0x1120($t8)\n  nop\n" \
  "1:\n" body "  addiu $t9, $t9, -1\n  bnez $t9, 1b\n  nop\n  lhu $t7, 0x1120($t8)\n  nop\n  subu $v0, $t7, $t6\n  jr $ra\n  andi $v0, $v0, 0xFFFF\n"
__asm__(".set push\n.set noreorder\n"
        LOOPFN(b_sc,  "  lhu $v0, 0x1DAE($t8)\n  lbu $v1, 0x1800($t8)\n")
        LOOPFN(b_sc1, "  lhu $v0, 0x1DAE($t8)\n  nop\n  lbu $v1, 0x1800($t8)\n")
        LOOPFN(b_sc2, "  lhu $v0, 0x1DAE($t8)\n  nop\n  nop\n  lbu $v1, 0x1800($t8)\n")
        LOOPFN(b_sc3, "  lhu $v0, 0x1DAE($t8)\n  nop\n  nop\n  nop\n  lbu $v1, 0x1800($t8)\n")
        LOOPFN(b_cs,  "  lbu $v1, 0x1800($t8)\n  lhu $v0, 0x1DAE($t8)\n")
        LOOPFN(b_sb,  "  lhu $v0, 0x1DAE($t8)\n  lbu $v1, 0($t5)\n")
        LOOPFN(b_st,  "  lhu $v0, 0x1DAE($t8)\n  lhu $v1, 0x1110($t8)\n")
        LOOPFN(b_s3,  "  lhu $v0, 0x1DAE($t8)\n  nop\n  nop\n  nop\n")
        ".set pop\n");

static uint32_t best(uint32_t (*f)(void)) { uint32_t m = ~0u; for (int i = 0; i < 9; i++) { uint32_t t = f(); if (i && t < m) m = t; } return m; }

int main() {
    printf("\ncpu/at-com0b\n");
    printf("A read of another region behind an SPU read at COM0 12-15; cached, 64 turns, fastest of 8; Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    printf("COM_DELAY       SC   SC1   SC2   SC3    CS    SB    ST    S3\n");
    static const uint32_t vals[] = { 0x132Cu, 0x132Du, 0x132Eu, 0x132Fu, 0x130Fu, 0x102Fu, 0x31125u };
    static uint32_t (*const F[8])(void) = { b_sc, b_sc1, b_sc2, b_sc3, b_cs, b_sb, b_st, b_s3 };
    const uint32_t com = COM_DELAY;
    *(volatile uint16_t *)0x1F801124u = 0;   // Timer 2: system clock, free run
    for (uint32_t v : vals) {
        uint32_t r[8];
        const uint32_t im = I_MASK; I_MASK = 0;
        COM_DELAY = v;
        for (int i = 0; i < 8; i++) r[i] = best(F[i]);
        COM_DELAY = com; I_MASK = im;
        printf("%08X  %5u %5u %5u %5u %5u %5u %5u %5u\n", (unsigned)v, (unsigned)r[0], (unsigned)r[1], (unsigned)r[2], (unsigned)r[3], (unsigned)r[4], (unsigned)r[5], (unsigned)r[6], (unsigned)r[7]);
    }
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
