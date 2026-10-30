/*
 * cpu/ram-banks - do RAM's two 1 KB halves of a 2 KB block keep their own open row? (2026-10-14 small hours)
 *
 * WHY. ram-store-io on the SCPH-9001: four stores alternating between the two 1 KB halves of one 2 KB block (A, A+1K,
 * A+4, A+1K+4) and then a RAM read finish 19 cycles after a Timer 2 read - as four stores to one block do, a burst -
 * where this core starts a new burst at every change of 1 KB block (CB_WPG, from wb-page2: "1, 2 and 4 KB apart never
 * pair") and reads 34-40. If address bit 10 picks one of two DRAM banks and each keeps its own open row, both readings
 * hold: alternating within a 2 KB block hits two open rows; strides of 1 KB keep moving to new rows. This lays stores
 * out to tell the two apart.
 *
 * WHAT IT DOES. Cached code, interrupts off, Timer 2 at the system clock. Each row: lhu T2; four stores alternating
 * between X and Y (X, Y, X+4, Y+4); lw from a fifth address Z (the read waits for the queue to drain); lhu T2 - fastest..
 * slowest of 16 over the refresh. X = A (2 KB aligned); Z = A+8 (X's row) unless named:
 *   SAME      Y = A+512   (one 1 KB block)
 *   ALT1K     Y = A+1K    (the other half of X's 2 KB block)
 *   ALT2K     Y = A+2K    (X's half, the next row)
 *   ALT3K     Y = A+3K    (the other half, the next row)
 *   ALT4K     Y = A+4K
 *   ALT5K     Y = A+5K    (the other half, two rows on)
 *   ALT1K/Z5K the ALT1K stores, the read from A+5K (the other half's next-but-one row)
 *   PRE1K     a read from A+1K first (opening it), then SAME's stores to A only... and the read from A+1K+8
 * and ONE: a single store to A, the read from A+8 (the floor).
 *
 * READ IT LIKE THIS. Cycles, fastest..slowest; no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-banks --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t ram[4096] __attribute__((aligned(2048)));   /* 16 KB, A = &ram[0] (2 KB aligned) */

/* t9 = 1F800000h, t6 = A; each returns its count in v0 */
#define FN(name, body) \
    extern "C" uint32_t name(void); \
    __asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n.global " #name "\n" #name ":\n" body \
            "nop\nsubu $v0, $t2, $t1\njr $ra\nandi $v0, $v0, 0xffff\n.set pop\n");
#define T1 "lhu $t1, 0x1120($t9)\n"
#define T2 "lhu $t2, 0x1120($t9)\n"
/* X = A (t6), Y = A + off: offsets fit lw/sw's 16-bit immediate up to 32 KB */
#define ALT(Y) "sw $zero, 0($t6)\nsw $zero, " #Y "($t6)\nsw $zero, 4($t6)\nsw $zero, " #Y "+4($t6)\n"
#define RD(Z) "lw $t3, " #Z "($t6)\n"
FN(one, T1 "sw $zero, 0($t6)\n" RD(8) T2)
FN(same, T1 ALT(512) RD(8) T2)
FN(alt1k, T1 ALT(1024) RD(8) T2)
FN(alt2k, T1 ALT(2048) RD(8) T2)
FN(alt3k, T1 ALT(3072) RD(8) T2)
FN(alt4k, T1 ALT(4096) RD(8) T2)
FN(alt5k, T1 ALT(5120) RD(8) T2)
FN(alt1kz5k, T1 ALT(1024) RD(5128) T2)
FN(pre1k, "lw $t4, 1024($t6)\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n" T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" RD(1032) T2)
FN(nopre1k, "lw $t4, 4096($t6)\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n" T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" RD(1032) T2)

extern "C" uint32_t callRow(uint32_t (*f)(void), volatile uint32_t* a);
extern "C" void spin(uint32_t n);
__asm__(".text\n.set push\n.set noreorder\n.set noat\n.balign 16\n"
        ".global callRow\ncallRow:\nmove $t8, $ra\nlui $t9, 0x1f80\nmove $t6, $a1\njalr $a0\nnop\njr $t8\nnop\n"
        ".global spin\nspin:\n1:\nbnez $a0, 1b\naddiu $a0, $a0, -1\njr $ra\nnop\n"
        ".set pop\n");

typedef uint32_t (*Row)(void);
static void row(const char* name, Row f) {
    uint32_t lo = 0xFFFF, hi = 0;
    callRow(f, ram);
    for (uint32_t k = 0; k < 16; k++) {
        spin(k * 8);
        const uint32_t v = callRow(f, ram);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    printf("  %-9s %4u..%-4u\n", name, (unsigned)lo, (unsigned)hi);
}

int main() {
    printf("\ncpu/ram-banks\n");
    printf("Four stores alternating X, Y, X+4, Y+4, then a RAM read (cached; Timer 2 cycles, fastest..slowest of 16).\n");
    printf("X = A (2 KB aligned); Y = A + 512 (SAME), 1K .. 5K (ALTnK); the read from A+8, or A+5K+8 (ALT1K/Z5K).\n");
    printf("PRE1K / NOPRE1K: a read from A+1K (or A+4K) first, four stores to A's block, the read from A+1K+8.\n");
    printf("ONE: a store to A, the read from A+8. There are no assertions - compare against your emulator.\n\n");
    printf("A at %08X\n", (unsigned)(uint32_t)ram);
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("ONE", one); row("SAME", same); row("ALT1K", alt1k); row("ALT2K", alt2k); row("ALT3K", alt3k);
    row("ALT4K", alt4k); row("ALT5K", alt5k); row("ALT1K/Z5K", alt1kz5k); row("PRE1K", pre1k); row("NOPRE1K", nopre1k);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-banks: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
