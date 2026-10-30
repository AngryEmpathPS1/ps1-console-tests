/*
 * cpu/io-store-shadow - a store in an on-die load's shadow: what it costs, and when it lands, at positions 1-4
 * (2026-10-11 night)
 *
 * WHY. hwtests/t2-path on the SCPH-1001 (three runs, identical): `sh 0,T2; jal loop10; nop; lhu T2` reads 45 on the
 * console and here, and with resetTimer's mode read in front (`lhu MODE; sh 0,T2; ...`) 45 on the console, 43 here. This
 * core charges an on-die load's whole stall at the load and gives two cycles back at shadow positions 3 and 4, so the
 * store at position 1 lands after the stall, two cycles late against what follows it. Either the store runs in the
 * shadow (it lands early and the cycles are still given back) or it ends the shadow (nothing is given back, and what
 * follows is two cycles later). The total says which; the counter says when the store lands.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, cached (each row run once before it is timed), 16 trials
 * spread over the DRAM refresh, fastest and slowest:
 *   F p  lhu T2; store to Timer 1's target at position p (1-4, 0 = none); nops to position 9; lhu T2   - the total
 *   G p  lhu T2 MODE; sh 0,T2 at position p (1-4; 0 = no load, the store first); nops to position 9; lhu T2 - the landing
 *   H p  lhu T2; sw to RAM at position p (1-4, 0 = none); nops to position 17; lhu T2                   - a RAM store
 *
 * READ IT LIKE THIS. F: if the store runs in the shadow, every p reads as p = 0; if it ends it, p = 1..2 read 2 more. G:
 * the store's landing against the read at a fixed place - each row against p = 0 shifted by the load's position.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js io-store-shadow`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t ramWord[4];

/* t9 = 1F800000h, t7 = &ramWord on entry (set by the caller); each returns its count in v0 */
#define NOPS(K) ".rept " #K "\nnop\n.endr\n"
#define FN(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\n" body \
            "nop\nsubu $v0, $t2, $t1\njr $ra\nandi $v0, $v0, 0xffff\n.set pop\n");
/* F: total, a store to T1_TARGET (1F801118h) at position P */
#define FROW(P) FN(f##P, "lhu $t1, 0x1120($t9)\n" NOPS(P - 1) "sh $zero, 0x1118($t9)\n" NOPS(8 - P) "lhu $t2, 0x1120($t9)\n")
FN(f0, "lhu $t1, 0x1120($t9)\n" NOPS(8) "lhu $t2, 0x1120($t9)\n")
FROW(1) FROW(2) FROW(3) FROW(4)
/* G: the landing - t1 = 0 so the subtraction returns the count read */
#define GROW(P) FN(g##P, "lhu $t3, 0x1124($t9)\n" NOPS(P - 1) "sh $zero, 0x1120($t9)\n" NOPS(8 - P) "lhu $t2, 0x1120($t9)\nmove $t1, $zero\n")
FN(g0, "nop\nsh $zero, 0x1120($t9)\n" NOPS(7) "lhu $t2, 0x1120($t9)\nmove $t1, $zero\n")
GROW(1) GROW(2) GROW(3) GROW(4)
/* H: a RAM store at position P, the read at 17 */
#define HROW(P) FN(h##P, "lhu $t1, 0x1120($t9)\n" NOPS(P - 1) "sw $zero, 0($t7)\n" NOPS(16 - P) "lhu $t2, 0x1120($t9)\n")
FN(h0, "lhu $t1, 0x1120($t9)\n" NOPS(16) "lhu $t2, 0x1120($t9)\n")
HROW(1) HROW(2) HROW(3) HROW(4)

extern "C" uint32_t callRow(uint32_t (*f)(void), volatile uint32_t* ram);
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callRow\ncallRow:\nmove $t8, $ra\nlui $t9, 0x1f80\nmove $t7, $a1\njalr $a0\nnop\njr $t8\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

typedef uint32_t (*Row)(void);
static void row(const char* name, Row f) {
    uint32_t lo = 0xFFFF, hi = 0;
    callRow(f, ramWord);
    for (uint32_t k = 0; k < 16; k++) {
        spin(k * 8);
        const uint32_t v = callRow(f, ramWord);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    printf("  %-4s %4u..%-4u\n", name, (unsigned)lo, (unsigned)hi);
}

int main() {
    printf("\ncpu/io-store-shadow\n");
    printf("A store in an on-die load's shadow (cached; Timer 2 cycles, fastest..slowest of 16 over the refresh).\n");
    printf("F p: lhu T2; sh T1_TARGET at p; nops; lhu T2 at 9 - the total.\n");
    printf("G p: lhu T2 MODE; sh 0,T2 at p; nops; lhu T2 at 9 - the count read (p 0: no load).\n");
    printf("H p: lhu T2; sw RAM at p; nops; lhu T2 at 17 - the total.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u), t1t = read16(0x1F801118u);
    write16(0x1F801124u, 0);
    row("F0", f0); row("F1", f1); row("F2", f2); row("F3", f3); row("F4", f4);
    row("G0", g0); row("G1", g1); row("G2", g2); row("G3", g3); row("G4", g4);
    row("H0", h0); row("H1", h1); row("H2", h2); row("H3", h3); row("H4", h4);
    write16(0x1F801118u, t1t);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
