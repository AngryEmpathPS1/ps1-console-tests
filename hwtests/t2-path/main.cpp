/*
 * cpu/t2-path - ps1-tests' measure() taken apart: a Timer 2 reset, what runs between, and the read, one piece at a time
 * (2026-10-11 night)
 *
 * WHY. hwtests/at-split on the SCPH-9001 and 1001 (byte-identical): at the runner's landing every access-time window -
 * the nop loops' too - reads 2 cycles longer on the consoles than this core. hwtests/counter-store's store-then-read rows
 * are the consoles' here to the cycle, so the 2 sit in what measure() adds: the call into the measured function, its loop,
 * the return, the mode read before the reset, the stores left in the write queue before it. Each is added here alone.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock; every row a fixed run of instructions (`.set noreorder`),
 * called twice and timed the second time (cached), the value read back from Timer 2 after the reset:
 *   A  sh 0,T2; read                                  counter-store's N = 0
 *   B  sh 0,T2; jal stub (jr ra; nop); nop; read       a call and return between
 *   C  sh 0,T2; jal loop10; nop; read                  ... a 10-turn loop inside (li; 1: nop; addiu; bnez; nop; jr ra)
 *   D  lhu MODE; sh 0,T2; jal loop10; nop; read        resetTimer's mode read first (measure()'s own order)
 *   E  sw; sw; lhu MODE; sh 0,T2; jal loop10; nop; read  two RAM stores queued ahead (a prologue's)
 *   F  sh 0,T2; 10-turn loop inline; read              the loop without the call
 *   G  measure<CpuClock>() around a 10-turn nop lambda  the real thing (its call through a register, its return)
 *   H  measure<CpuClock>() around a 100-turn nop lambda  access-time's nop window
 *
 * READ IT LIKE THIS. Each row is cycles from the reset to the read. A - counter-store's known row - anchors it; each step
 * down adds one piece, so the first row that differs from the emulator by the consoles' 2 names the piece.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js t2-path`.
 */
#include <common.h>
#include <measure.hpp>
#include <io.h>

uint32_t t2Buf[8];

extern "C" uint32_t rowA(void); extern "C" uint32_t rowB(void); extern "C" uint32_t rowC(void);
extern "C" uint32_t rowD(void); extern "C" uint32_t rowE(void); extern "C" uint32_t rowF(void);

/* each row returns the Timer 2 value read; t9 = 1F800000h; ra kept in t8 around the calls */
__asm__(".text\n.set push\n.set noreorder\n.set noat\n"
        ".global t2Stub\nt2Stub:\njr $ra\nnop\n"
        ".global t2Loop10\nt2Loop10:\nli $t0, 10\n1:\nnop\naddiu $t0, $t0, -1\nbnez $t0, 1b\nnop\njr $ra\nnop\n"
        ".global rowA\nrowA:\nlui $t9, 0x1f80\nsh $zero, 0x1120($t9)\nlhu $v0, 0x1120($t9)\nnop\njr $ra\nnop\n"
        ".global rowB\nrowB:\nmove $t8, $ra\nlui $t9, 0x1f80\nsh $zero, 0x1120($t9)\njal t2Stub\nnop\nnop\nlhu $v0, 0x1120($t9)\nnop\njr $t8\nnop\n"
        ".global rowC\nrowC:\nmove $t8, $ra\nlui $t9, 0x1f80\nsh $zero, 0x1120($t9)\njal t2Loop10\nnop\nnop\nlhu $v0, 0x1120($t9)\nnop\njr $t8\nnop\n"
        ".global rowD\nrowD:\nmove $t8, $ra\nlui $t9, 0x1f80\nlhu $t1, 0x1124($t9)\nsh $zero, 0x1120($t9)\njal t2Loop10\nnop\nnop\nlhu $v0, 0x1120($t9)\nnop\njr $t8\nnop\n"
        ".global rowE\nrowE:\nmove $t8, $ra\nlui $t9, 0x1f80\nla $t2, t2Buf\nsw $zero, 0($t2)\nsw $zero, 4($t2)\nlhu $t1, 0x1124($t9)\nsh $zero, 0x1120($t9)\njal t2Loop10\nnop\nnop\nlhu $v0, 0x1120($t9)\nnop\njr $t8\nnop\n"
        ".global rowF\nrowF:\nlui $t9, 0x1f80\nsh $zero, 0x1120($t9)\nli $t0, 10\n2:\nnop\naddiu $t0, $t0, -1\nbnez $t0, 2b\nnop\nlhu $v0, 0x1120($t9)\nnop\njr $ra\nnop\n"
        ".set pop\n");

typedef uint32_t (*Row)(void);
static uint32_t twice(Row f) { f(); return f() & 0xFFFF; }

int main() {
    printf("\ncpu/t2-path\n");
    printf("measure() taken apart: Timer 2 cycles from the reset to the read (cached, the second call).\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    const uint32_t a = twice(rowA), b = twice(rowB), c = twice(rowC), d = twice(rowD), e = twice(rowE), f = twice(rowF);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("  A  sh T2; read                                 %5u\n", (unsigned)a);
    printf("  B  sh T2; jal stub; nop; read                  %5u\n", (unsigned)b);
    printf("  C  sh T2; jal loop10; nop; read                %5u\n", (unsigned)c);
    printf("  D  lhu MODE; sh T2; jal loop10; nop; read      %5u\n", (unsigned)d);
    printf("  E  sw; sw; lhu MODE; sh T2; jal loop10; read   %5u\n", (unsigned)e);
    printf("  F  sh T2; loop10 inline; read                  %5u\n", (unsigned)f);
    auto n10 = [] { for (int i = 0; i < 10; i++) asm("nop"); };
    auto n100 = [] { for (int i = 0; i < 100; i++) asm("nop"); };
    measure<MeasureMethod::CpuClock>(n10);
    const uint32_t g = measure<MeasureMethod::CpuClock>(n10);
    measure<MeasureMethod::CpuClock>(n100);
    const uint32_t h = measure<MeasureMethod::CpuClock>(n100);
    printf("  G  measure() around a 10-turn nop lambda       %5u\n", (unsigned)g);
    printf("  H  measure() around a 100-turn nop lambda      %5u\n", (unsigned)h);
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
