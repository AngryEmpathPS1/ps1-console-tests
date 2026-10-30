/*
 * cpu/ram-store-io - an I/O read behind RAM stores: does it wait for them to reach RAM? (2026-10-14 small hours)
 *
 * WHY. gp0-poll on the SCPH-9001: gpu-cost-sweep's poll loop (a GPUSTAT read and a Timer 2 read by call, two RAM loads,
 * four RAM stores alternating between the stack and .sdata) runs at 48 cycles an iteration there and 53-60 here; this
 * core's GPUSTAT read waits behind the four stores' drain (6 cycles each as new bursts, M207) - 12-18 cycles before its
 * value can be used. Does an I/O read wait for queued RAM stores at all, and does a store that alternates DRAM pages
 * cost a new burst each?
 *
 * WHAT IT DOES. Cached code, interrupts off, Timer 2 at the system clock; each row the two Timer 2 reads' distance,
 * fastest..slowest of 16 over the refresh:
 *   S n k   lhu T2; n stores to ONE 1 KB block (sw to A, A+4, ...); k nops; lhu T2          n = 1, 2, 4; k = 0, 8
 *   P n k   the same, the stores alternating between two blocks 64 KB apart (A, B, A+4, B+4)
 *   G n     lhu T2; n alternating stores; lw GPUSTAT; lhu T2                                   n = 2, 4
 *   L n     lhu T2; n alternating stores; lw RAM (a third block); lhu T2                       n = 2, 4
 *   N       lhu T2; 4 alternating stores; nothing; lhu T2 at 4 instructions                  (the drain alone)
 *
 * READ IT LIKE THIS. Cycles, fastest..slowest; no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-store-io --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t bufA[64] __attribute__((aligned(1024)));
static volatile uint32_t padding[16384] __attribute__((unused));
static volatile uint32_t bufB[64] __attribute__((aligned(1024)));
static volatile uint32_t bufC[64] __attribute__((aligned(1024)));

/* t9 = 1F800000h, t6 = &bufA, t7 = &bufB, t5 = &bufC (set by the caller); each returns its count in v0 */
#define NOPS(K) ".rept " #K "\nnop\n.endr\n"
#define FN(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\n" body \
            "nop\nsubu $v0, $t2, $t1\njr $ra\nandi $v0, $v0, 0xffff\n.set pop\n");
#define T1 "lhu $t1, 0x1120($t9)\n"
#define T2 "lhu $t2, 0x1120($t9)\n"
#define S1 "sw $zero, 0($t6)\n"
#define S2 S1 "sw $zero, 4($t6)\n"
#define S4 S2 "sw $zero, 8($t6)\nsw $zero, 12($t6)\n"
#define P2 "sw $zero, 0($t6)\nsw $zero, 0($t7)\n"
#define P4 P2 "sw $zero, 4($t6)\nsw $zero, 4($t7)\n"
FN(s1k0, T1 S1 T2) FN(s2k0, T1 S2 T2) FN(s4k0, T1 S4 T2)
FN(s1k8, T1 S1 NOPS(8) T2) FN(s2k8, T1 S2 NOPS(8) T2) FN(s4k8, T1 S4 NOPS(8) T2)
FN(p2k0, T1 P2 T2) FN(p4k0, T1 P4 T2) FN(p2k8, T1 P2 NOPS(8) T2) FN(p4k8, T1 P4 NOPS(8) T2)
FN(g2, T1 P2 "lw $t3, 0x1814($t9)\n" T2) FN(g4, T1 P4 "lw $t3, 0x1814($t9)\n" T2)
FN(l2, T1 P2 "lw $t3, 0($t5)\n" T2) FN(l4, T1 P4 "lw $t3, 0($t5)\n" T2)
FN(n4, T1 P4 "nop\nnop\nnop\nnop\n" T2)
FN(z0, T1 T2)

extern "C" uint32_t callRow(uint32_t (*f)(void), volatile uint32_t* a, volatile uint32_t* b, volatile uint32_t* c);
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callRow\ncallRow:\nmove $t8, $ra\nlui $t9, 0x1f80\nmove $t6, $a1\nmove $t7, $a2\nmove $t5, $a3\njalr $a0\nnop\njr $t8\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

typedef uint32_t (*Row)(void);
static void row(const char* name, Row f) {
    uint32_t lo = 0xFFFF, hi = 0;
    callRow(f, bufA, bufB, bufC);
    for (uint32_t k = 0; k < 16; k++) {
        spin(k * 8);
        const uint32_t v = callRow(f, bufA, bufB, bufC);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    printf("  %-6s %4u..%-4u\n", name, (unsigned)lo, (unsigned)hi);
}

int main() {
    printf("\ncpu/ram-store-io\n");
    printf("An I/O read behind RAM stores (cached; Timer 2 cycles, fastest..slowest of 16 over the refresh).\n");
    printf("Z: two Timer 2 reads.  S n k: n stores to one 1 KB block, k nops.  P n k: alternating between two blocks.\n");
    printf("G n: n alternating stores, a GPUSTAT read.  L n: n alternating stores, a RAM read.  N: 4 alternating, 4 nops.\n");
    printf("There are no assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("Z", z0);
    row("S1k0", s1k0); row("S2k0", s2k0); row("S4k0", s4k0); row("S1k8", s1k8); row("S2k8", s2k8); row("S4k8", s4k8);
    row("P2k0", p2k0); row("P4k0", p4k0); row("P2k8", p2k8); row("P4k8", p4k8);
    row("G2", g2); row("G4", g4); row("L2", l2); row("L4", l4); row("N4", n4);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-store-io: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
