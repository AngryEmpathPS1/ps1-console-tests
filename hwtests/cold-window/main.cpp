/*
 * cpu/cold-window - hwtests/at-split's measure window rebuilt instruction for instruction, its two cold lines made cold or
 * warm one at a time (2026-10-11 night)
 *
 * WHY. hwtests/at-split on the SCPH-9001 and 1001: at the runner's landing every window reads 2 cycles longer on the
 * consoles than this core (v0.377.79: still). hwtests/t2-path showed measure()'s warm path is this core's to the cycle,
 * so the 2 are in the window's COLD lines. This core's trace of at-split's nop window (the RAM row): the counter reset
 * (sh in write16, cached), jr ra, its slot, then `addiu v1, 100` at a cached line's last word; the loop's line a miss
 * (its first nop 5 cycles); 100 turns; the line after the loop a miss (lui 5 cycles, jal, its delay slot 3 cycles - the
 * stream's word 2); read16 cached. Which of the two fills holds the 2 cycles?
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock. One block, laid out as at-split's (the loop at word 0 of
 * its line, the jal to the read at word 1 of the next), timed from the counter reset to the read, with the loop line (L1)
 * and the line after it (L2) each cold or warm: before each trial the block runs once (everything warm), then an evictor
 * 4 KB above it - a `jr ra; nop` at L1's index, at L2's, or both - takes the lines it shares. Loops of 100 and of 1.
 * 16 trials a case, spread over the DRAM refresh; fastest..slowest.
 *
 * READ IT LIKE THIS. Both warm is t2-path's ground (this core's to the cycle). The first case that differs from the
 * emulator names the line: L1 cold alone, L2 cold alone, or only both.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js cold-window`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

/* The block page (4 KB aligned) and its evictor page right above it. In the block page: w16 and r16 (the timer's write
 * and read, at-split's write16/read16, kept warm in their own lines), then the measured block at 0x100:
 *   L0 0x100: nop; jal w16; nop; addiu v1, zero, N      (the return lands on the addiu, L0's last word)
 *   L1 0x110: nop; addiu v1, v1, -1; bnez v1, L1; nop
 *   L2 0x120: lui t4, 0x1f80; jal r16; addiu a0, t4, 0x1120; nop (at-split: s3, saved by its prologue)
 *   L3 0x130: move v0, t3 ... return (t3 = r16's read)
 * The evictor page: `jr ra; nop` at 0x110 (L1's index) and at 0x120 (L2's). */
#define BLOCK(name, N) \
    ".balign 4096\n.global " #name "\n" #name ":\n" \
    "w16" #name ":\nsh $zero, 0x1120($t9)\njr $ra\nnop\nnop\n" \
    "r16" #name ":\nlhu $t3, 0x1120($t9)\njr $ra\nnop\nnop\n" \
    ".balign 256\n" \
    "nop\njal w16" #name "\nnop\naddiu $v1, $zero, " #N "\n" \
    "1:\nnop\naddiu $v1, $v1, -1\nbnez $v1, 1b\nnop\n" \
    "lui $t4, 0x1f80\njal r16" #name "\naddiu $a0, $t4, 0x1120\nnop\n" \
    "andi $v0, $t3, 0xffff\njr $t8\nnop\nnop\n" \
    ".balign 4096\n" \
    #name "_ev:\n.space 0x110\n" #name "_e1:\njr $ra\nnop\nnop\nnop\n" #name "_e2:\njr $ra\nnop\nnop\nnop\n"

extern "C" uint32_t cw100(void); extern "C" uint32_t cw1(void);
extern "C" void cw100_e1(void); extern "C" void cw100_e2(void); extern "C" void cw1_e1(void); extern "C" void cw1_e2(void);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n" BLOCK(cw100, 100) BLOCK(cw1, 1) ".set pop\n");

extern "C" uint32_t callBlock(uint32_t entry);
extern "C" void spin(uint32_t n);
/* callBlock: t9 = 1F800000h, t8 = the return; jump to the block's L0 (entry = block + 0x100) */
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callBlock\ncallBlock:\nmove $t8, $ra\nlui $t9, 0x1f80\njr $a0\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

typedef void (*Ev)(void);
struct Span { uint32_t lo, hi; };
static Span trials(uint32_t block, Ev e1, Ev e2) {
    Span s = { 0xFFFF, 0 };
    const uint32_t entry = block + 0x100;
    for (uint32_t k = 0; k < 17; k++) {
        callBlock(entry);                      /* everything warm */
        if (e1) e1();
        if (e2) e2();
        spin(k * 8);
        const uint32_t v = callBlock(entry);
        if (k == 0) continue;                  /* the first pass warms the evictors themselves */
        if (v < s.lo) s.lo = v;
        if (v > s.hi) s.hi = v;
    }
    return s;
}
static void line(const char* name, uint32_t block, Ev e1, Ev e2) {
    const Span s = trials(block, e1, e2);
    printf("  %-22s %4u..%-4u\n", name, (unsigned)s.lo, (unsigned)s.hi);
}

int main() {
    printf("\ncpu/cold-window\n");
    printf("at-split's window (counter reset to read), its loop line L1 and the line after it L2 cold or warm;\n");
    printf("Timer 2 cycles, fastest..slowest of 16 over the refresh.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    line("loop 100, both warm", (uint32_t)cw100, 0, 0);
    line("loop 100, L1 cold", (uint32_t)cw100, cw100_e1, 0);
    line("loop 100, L2 cold", (uint32_t)cw100, 0, cw100_e2);
    line("loop 100, both cold", (uint32_t)cw100, cw100_e1, cw100_e2);
    line("loop 1, both warm", (uint32_t)cw1, 0, 0);
    line("loop 1, L1 cold", (uint32_t)cw1, cw1_e1, 0);
    line("loop 1, L2 cold", (uint32_t)cw1, 0, cw1_e2);
    line("loop 1, both cold", (uint32_t)cw1, cw1_e1, cw1_e2);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
