/*
 * cpu/ram-store-io2 - the write queue against four kinds of read, n = 1..6 stores (2026-10-14 small hours).
 *
 * WHY. ram-store-io and ram-banks on the SCPH-9001: a read behind queued RAM stores waits for some of them and not
 * others - an on-die read after two stores to two blocks at 7 cycles, less than after two to one block (9); after four
 * to two blocks 13; a RAM read from a third block after two such stores 18, after four 19 - and no one rule this core
 * tried (the whole queue, M207; or the transaction on the bus) reads them all. This is the matrix the rule must hold.
 *
 * WHAT IT DOES. Cached code, interrupts off, Timer 2 at the system clock; each row lhu T2; n stores; [k nops]; a read;
 * lhu T2 - fastest..slowest of 16 over the refresh. Stores to ONE 1 KB block (S: A, A+4, ...) or alternating between
 * two (P: A, B, A+4, B+4, ...; B the next 1 KB block, as the toolchain lays bufA / bufB). The read:
 *   T   none (the second Timer 2 read is the read: on-die)
 *   G   lw GPUSTAT (the GPU, off-chip)
 *   O   lw from a third block (RAM no store went to)
 *   M   lw A+8 (the block the first store went to)
 * Rows: <read><S|P><n> for n = 1..6, and <read><S|P>4k8 with 8 nops between the stores and the read.
 *
 * READ IT LIKE THIS. Cycles, fastest..slowest; no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js ram-store-io2 --ntsc`.
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
FN(f_TS1, T1 "sw $zero, 0($t6)\n" T2)
FN(f_TS2, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\n" T2)
FN(f_TS3, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\n" T2)
FN(f_TS4, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" T2)
FN(f_TS5, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\n" T2)
FN(f_TS6, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nsw $zero, 20($t6)\n" T2)
FN(f_TP1, T1 "sw $zero, 0($t6)\n" T2)
FN(f_TP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\n" T2)
FN(f_TP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\n" T2)
FN(f_TP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" T2)
FN(f_TP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\n" T2)
FN(f_TP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\n" T2)
FN(f_GS1, T1 "sw $zero, 0($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GS2, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GS3, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GS4, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GS5, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GS6, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nsw $zero, 20($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP1, T1 "sw $zero, 0($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_GP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\nlw $t3, 0x1814($t9)\n" T2)
FN(f_OS1, T1 "sw $zero, 0($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OS2, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OS3, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OS4, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OS5, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OS6, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nsw $zero, 20($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP1, T1 "sw $zero, 0($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_OP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nlw $t3, 0($t5)\n" T2)
FN(f_OP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\nlw $t3, 0($t5)\n" T2)
FN(f_MS1, T1 "sw $zero, 0($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MS2, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MS3, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MS4, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MS5, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MS6, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\nsw $zero, 16($t6)\nsw $zero, 20($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP1, T1 "sw $zero, 0($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP2, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MP3, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP4, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_MP5, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nlw $t3, 8($t6)\n" T2)
FN(f_MP6, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\nsw $zero, 8($t6)\nsw $zero, 8($t7)\nlw $t3, 8($t6)\n" T2)
FN(f_TS4k8, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" NOPS(8) "" T2)
FN(f_TP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "" T2)
FN(f_GS4k8, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_GP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0x1814($t9)\n" T2)
FN(f_OS4k8, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_OP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "lw $t3, 0($t5)\n" T2)
FN(f_MS4k8, T1 "sw $zero, 0($t6)\nsw $zero, 4($t6)\nsw $zero, 8($t6)\nsw $zero, 12($t6)\n" NOPS(8) "lw $t3, 8($t6)\n" T2)
FN(f_MP4k8, T1 "sw $zero, 0($t6)\nsw $zero, 0($t7)\nsw $zero, 4($t6)\nsw $zero, 4($t7)\n" NOPS(8) "lw $t3, 8($t6)\n" T2)

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
    printf("\ncpu/ram-store-io2\n");
    printf("lhu T2; n stores (S one 1 KB block, P alternating two); [k nops]; a read (T none, G GPUSTAT, O RAM elsewhere,\n");
    printf("M RAM in the first store's block); lhu T2 - cycles, fastest..slowest of 16. No assertions.\n\n");
    printf("A %08X B %08X C %08X\n", (unsigned)(uint32_t)bufA, (unsigned)(uint32_t)bufB, (unsigned)(uint32_t)bufC);
    EnterCriticalSection();
    const uint16_t m2 = read16(0x1F801124u);
    write16(0x1F801124u, 0);
    row("TS1", f_TS1);
    row("TS2", f_TS2);
    row("TS3", f_TS3);
    row("TS4", f_TS4);
    row("TS5", f_TS5);
    row("TS6", f_TS6);
    row("TP1", f_TP1);
    row("TP2", f_TP2);
    row("TP3", f_TP3);
    row("TP4", f_TP4);
    row("TP5", f_TP5);
    row("TP6", f_TP6);
    row("GS1", f_GS1);
    row("GS2", f_GS2);
    row("GS3", f_GS3);
    row("GS4", f_GS4);
    row("GS5", f_GS5);
    row("GS6", f_GS6);
    row("GP1", f_GP1);
    row("GP2", f_GP2);
    row("GP3", f_GP3);
    row("GP4", f_GP4);
    row("GP5", f_GP5);
    row("GP6", f_GP6);
    row("OS1", f_OS1);
    row("OS2", f_OS2);
    row("OS3", f_OS3);
    row("OS4", f_OS4);
    row("OS5", f_OS5);
    row("OS6", f_OS6);
    row("OP1", f_OP1);
    row("OP2", f_OP2);
    row("OP3", f_OP3);
    row("OP4", f_OP4);
    row("OP5", f_OP5);
    row("OP6", f_OP6);
    row("MS1", f_MS1);
    row("MS2", f_MS2);
    row("MS3", f_MS3);
    row("MS4", f_MS4);
    row("MS5", f_MS5);
    row("MS6", f_MS6);
    row("MP1", f_MP1);
    row("MP2", f_MP2);
    row("MP3", f_MP3);
    row("MP4", f_MP4);
    row("MP5", f_MP5);
    row("MP6", f_MP6);
    row("TS4k8", f_TS4k8);
    row("TP4k8", f_TP4k8);
    row("GS4k8", f_GS4k8);
    row("GP4k8", f_GP4k8);
    row("OS4k8", f_OS4k8);
    row("OP4k8", f_OP4k8);
    row("MS4k8", f_MS4k8);
    row("MP4k8", f_MP4k8);
    write16(0x1F801124u, m2);
    ExitCriticalSection();
    printf("\ncpu/ram-store-io2: Done.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
