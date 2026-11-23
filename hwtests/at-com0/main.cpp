/*
 * cpu/at-com0 (2026-11-23, the cold-io session) - the slow-read recovery at the COM0 values games write.
 *
 * WHY. psx.js's expRecovery KNOWN-OPEN: the cycle a slow read right behind a slow read of a COM0 region waits was measured at the
 * kernel's COM_DELAY (00031125h, COM0 5; hwtests/spu-read-grid). tools/branchaudit.js on games.js's games (2026-11-23): COM_DELAY is
 * rewritten with other COM0 values thousands of times a game - 00001325h, 00001323h and 0000132Ch in FF7, GT2, RE and Ridge Racer (COM0
 * 5, 3 and 12), the CD / SPU libraries switching it around their accesses. Nothing measures the recovery there.
 *
 * WHAT IT DOES. Interrupts masked, Timer 2 at the system clock, cached loops of 64 turns. For each COM_DELAY value (31125h, 1325h,
 * 1323h, 132Ch, and 1320h / 132Fh for the edges) and each shape, the fastest of 8 runs:
 *   SP1  lhu SPUSTAT ; nop                 SP2  lhu SPUSTAT ; lhu SPUSTAT     (the SPU's region uses COM0)
 *   CD1  lbu CDSTAT ; nop                  CD2  lbu CDSTAT ; lbu CDSTAT       (the CD's does not)
 *   SC   lhu SPUSTAT ; lbu CDSTAT          (a read of another region behind an SPU read)
 * A row: the value, then the five shapes' cycles over 64 turns. COM_DELAY is put back as found.
 *
 * There are no assertions - compare against your emulator. Run it here first: `node hwtests/run.js at-com0`.
 */

#include <common.h>

#define COM_DELAY (*(volatile uint32_t *)0x1F801020u)
#define I_MASK    (*(volatile uint32_t *)0x1F801074u)

extern "C" uint32_t c0_sp1(void), c0_sp2(void), c0_cd1(void), c0_cd2(void), c0_sc(void);
/* each: Timer 2 read, 64 turns of the shape, Timer 2 read; the difference (wrapped at 16 bits) */
#define LOOPFN(name, body) \
  ".balign 16\n.global " #name "\n" #name ":\n  lui $t8, 0x1F80\n  li $t9, 64\n  lhu $t6, 0x1120($t8)\n  nop\n" \
  "1:\n" body "  addiu $t9, $t9, -1\n  bnez $t9, 1b\n  nop\n  lhu $t7, 0x1120($t8)\n  nop\n  subu $v0, $t7, $t6\n  jr $ra\n  andi $v0, $v0, 0xFFFF\n"
__asm__(".set push\n.set noreorder\n"
        LOOPFN(c0_sp1, "  lhu $v0, 0x1DAE($t8)\n  nop\n")
        LOOPFN(c0_sp2, "  lhu $v0, 0x1DAE($t8)\n  lhu $v1, 0x1DAE($t8)\n")
        LOOPFN(c0_cd1, "  lbu $v0, 0x1800($t8)\n  nop\n")
        LOOPFN(c0_cd2, "  lbu $v0, 0x1800($t8)\n  lbu $v1, 0x1800($t8)\n")
        LOOPFN(c0_sc,  "  lhu $v0, 0x1DAE($t8)\n  lbu $v1, 0x1800($t8)\n")
        ".set pop\n");

static uint32_t best(uint32_t (*f)(void)) { uint32_t m = ~0u; for (int i = 0; i < 9; i++) { uint32_t t = f(); if (i && t < m) m = t; } return m; }

int main() {
    printf("\ncpu/at-com0\n");
    printf("SPU and CD status reads, single and in pairs, at the COM_DELAY values games write; cached, 64 turns, fastest of 8; Timer 2 cycles.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    printf("COM_DELAY      SP1   SP2   CD1   CD2    SC\n");
    static const uint32_t vals[] = { 0x31125u, 0x1325u, 0x1323u, 0x132Cu, 0x1320u, 0x132Fu };
    const uint32_t com = COM_DELAY;
    *(volatile uint16_t *)0x1F801124u = 0;   // Timer 2: system clock, free run
    for (uint32_t v : vals) {
        const uint32_t im = I_MASK; I_MASK = 0;
        COM_DELAY = v;
        const uint32_t a = best(c0_sp1), b = best(c0_sp2), c = best(c0_cd1), d = best(c0_cd2), e = best(c0_sc);
        COM_DELAY = com; I_MASK = im;
        printf("%08X  %5u %5u %5u %5u %5u\n", (unsigned)v, (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d, (unsigned)e);
    }
    printf("Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
