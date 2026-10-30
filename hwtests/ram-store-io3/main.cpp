/*
 * cpu/ram-store-io3 - does the write buffer merge a store into a queued word's 16-byte line? (2026-10-14 small hours)
 *
 * WHY. ram-store-io2 on the SCPH-9001: n stores alternating between two 1 KB blocks (A, B, A+4, B+4, A+8, B+8) then a
 * read - the read goes on the bus at 7 / 9 / 9 / 15 / 15 / 21 cycles for n = 1..6 (a RAM read elsewhere), in steps of
 * two, and every model of 6-cycle stores in a queue with stalls misses n = 4 or n = 5 by one store. A store merged into
 * a queued (not yet started) word of the same 16-byte line would explain n = 5 - but not the read from A's own block
 * (ram-store-io2's M, n = 5: 31, every store a transaction). ram-store-io2's stores to one block shared 16-byte lines;
 * these do not.
 *
 * WHAT IT DOES. As ram-store-io2 (cached, Timer 2 at the system clock; lhu T2; n stores; [k nops]; a read; lhu T2 -
 * fastest..slowest of 16), the stores:
 *   P   A, B, A+4, B+4, ...      ram-store-io2's again (a block's stores share 16-byte lines)
 *   Q   A, B, A+16, B+16, ...    the same blocks, each store a fresh 16-byte line
 *   L   A, A+16, A+32, ...       one block, each store a fresh line (ram-store-io2's S shared lines)
 * The read:
 *   T   none (the second Timer 2 read is the read: on-die)
 *   G   lw GPUSTAT (the GPU, off-chip)
 *   O   lw from a third block (RAM no store went to)
 *   M   lw A+8 (the block the first store went to)
 * Rows: <read><P|Q|L><n> for n = 1..6, and <read><P|Q|L>4k8 with 8 nops between the stores and the read.
 *
 * READ IT LIKE THIS. Cycles, fastest..slowest; no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-store-io3 --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t bufA[64] __attribute__((aligned(1024)));
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
FN(f_TP1, T1 "sw $zero, 0($t6)\n" T2)
FN(f_TP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\n" T2)
FN(f_TP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\n" T2)
FN(f_TP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" T2)
FN(f_TP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\n" T2)
FN(f_TP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\n" T2)
FN(f_TQ1, T1 "sw $zero, 0($t6)\n" T2)
FN(f_TQ2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\n" T2)
FN(f_TQ3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\n" T2)
FN(f_TQ4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\n" T2)
FN(f_TQ5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\n" T2)
FN(f_TQ6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nsw $zero, 32($t7)\n" T2)
FN(f_TL1, T1 "sw $zero, 0($t6)\n" T2)
FN(f_TL2, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\n" T2)
FN(f_TL3, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\n" T2)
FN(f_TL4, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\n" T2)
FN(f_TL5, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\n" T2)
FN(f_TL6, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nsw $zero, 80($t6)\n" T2)
FN(f_GP1, T1 "sw $zero, 0($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GQ1, T1 "sw $zero, 0($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GQ2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GQ3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GQ5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GQ6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nsw $zero, 32($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GL1, T1 "sw $zero, 0($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GL2, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GL3, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GL4, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GL5, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GL6, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nsw $zero, 80($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_OP1, T1 "sw $zero, 0($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OQ1, T1 "sw $zero, 0($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OQ2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OQ3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OQ4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OQ5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OQ6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nsw $zero, 32($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OL1, T1 "sw $zero, 0($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OL2, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OL3, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OL4, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OL5, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OL6, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nsw $zero, 80($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_MP1, T1 "sw $zero, 0($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MQ1, T1 "sw $zero, 0($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MQ2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MQ3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MQ4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MQ5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MQ6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\nsw $zero, 32($t6)\nsw $zero, 32($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_ML1, T1 "sw $zero, 0($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_ML2, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_ML3, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_ML4, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_ML5, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_ML6, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\nsw $zero, 64($t6)\nsw $zero, 80($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_TP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "" T2)
FN(f_TQ4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\n" NOPS(8) "" T2)
FN(f_TL4k8, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\n" NOPS(8) "" T2)
FN(f_GP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GQ4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GL4k8, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_OP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OQ4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OL4k8, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_MP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "lw $t3, 8($t6)\n" T2)
FN(f_MQ4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 16($t6)\nsw $zero, 16($t7)\n" NOPS(8) "lw $t3, 8($t6)\n" T2)
FN(f_ML4k8, T1 "sw $zero, 0($t6)\nsw $zero, 16($t6)\nsw $zero, 32($t6)\nsw $zero, 48($t6)\n" NOPS(8) "lw $t3, 8($t6)\n" T2)

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
        spin(64 + k * 8);   /* long enough that the harness's own stores have drained */
        const uint32_t v = callRow(f, bufA, bufB, bufC);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    printf("  %-8s %4u..%-4u\n", name, (unsigned)lo, (unsigned)hi);
}

int main() {
    printf("\ncpu/ram-store-io3\n");
    printf("lhu T2; n stores (P A,B,A+4.. Q A,B,A+16.. L A,A+16..); [k nops]; a read (T none, G GPUSTAT, O RAM elsewhere,\n");
    printf("M RAM in the first store's block); lhu T2 - cycles, fastest..slowest of 16. No assertions.\n\n");
    printf("A %08X B %08X C %08X\n", (unsigned)(uint32_t)bufA, (unsigned)(uint32_t)bufB, (unsigned)(uint32_t)bufC);
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("TP1", f_TP1);
    row("TP2", f_TP2);
    row("TP3", f_TP3);
    row("TP4", f_TP4);
    row("TP5", f_TP5);
    row("TP6", f_TP6);
    row("TQ1", f_TQ1);
    row("TQ2", f_TQ2);
    row("TQ3", f_TQ3);
    row("TQ4", f_TQ4);
    row("TQ5", f_TQ5);
    row("TQ6", f_TQ6);
    row("TL1", f_TL1);
    row("TL2", f_TL2);
    row("TL3", f_TL3);
    row("TL4", f_TL4);
    row("TL5", f_TL5);
    row("TL6", f_TL6);
    row("GP1", f_GP1);
    row("GP2", f_GP2);
    row("GP3", f_GP3);
    row("GP4", f_GP4);
    row("GP5", f_GP5);
    row("GP6", f_GP6);
    row("GQ1", f_GQ1);
    row("GQ2", f_GQ2);
    row("GQ3", f_GQ3);
    row("GQ4", f_GQ4);
    row("GQ5", f_GQ5);
    row("GQ6", f_GQ6);
    row("GL1", f_GL1);
    row("GL2", f_GL2);
    row("GL3", f_GL3);
    row("GL4", f_GL4);
    row("GL5", f_GL5);
    row("GL6", f_GL6);
    row("OP1", f_OP1);
    row("OP2", f_OP2);
    row("OP3", f_OP3);
    row("OP4", f_OP4);
    row("OP5", f_OP5);
    row("OP6", f_OP6);
    row("OQ1", f_OQ1);
    row("OQ2", f_OQ2);
    row("OQ3", f_OQ3);
    row("OQ4", f_OQ4);
    row("OQ5", f_OQ5);
    row("OQ6", f_OQ6);
    row("OL1", f_OL1);
    row("OL2", f_OL2);
    row("OL3", f_OL3);
    row("OL4", f_OL4);
    row("OL5", f_OL5);
    row("OL6", f_OL6);
    row("MP1", f_MP1);
    row("MP2", f_MP2);
    row("MP3", f_MP3);
    row("MP4", f_MP4);
    row("MP5", f_MP5);
    row("MP6", f_MP6);
    row("MQ1", f_MQ1);
    row("MQ2", f_MQ2);
    row("MQ3", f_MQ3);
    row("MQ4", f_MQ4);
    row("MQ5", f_MQ5);
    row("MQ6", f_MQ6);
    row("ML1", f_ML1);
    row("ML2", f_ML2);
    row("ML3", f_ML3);
    row("ML4", f_ML4);
    row("ML5", f_ML5);
    row("ML6", f_ML6);
    row("TP4k8", f_TP4k8);
    row("TQ4k8", f_TQ4k8);
    row("TL4k8", f_TL4k8);
    row("GP4k8", f_GP4k8);
    row("GQ4k8", f_GQ4k8);
    row("GL4k8", f_GL4k8);
    row("OP4k8", f_OP4k8);
    row("OQ4k8", f_OQ4k8);
    row("OL4k8", f_OL4k8);
    row("MP4k8", f_MP4k8);
    row("MQ4k8", f_MQ4k8);
    row("ML4k8", f_ML4k8);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-store-io3: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
