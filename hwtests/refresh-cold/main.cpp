/*
 * cpu/refresh-cold - what a DRAM refresh costs code that misses the instruction cache, at every phase from it: line
 * fills alone, and a RAM load, a store or an I_STAT read in the stream (2026-10-11)
 *
 * WHY. The stream (src/psx.js, "IN CODE THAT MISSES THE INSTRUCTION CACHE THE BUS IS A QUEUE", v0.377.75) leaves the
 * refresh out of its queue: one that comes due during the stream waits for the next line fill's grant (busGrant), which
 * charges it 5 cycles there whatever the bus was doing (CB_REF, the refresh inside the queue, is off: on it read worse).
 * v0.377.78's DRAM page mode (STATUS 2026-10-11 afternoon) moved cputest's access-time - two one-cycle rows, SCRATCHPAD
 * 8 and CACHECTRL 32, five cycles each - by shifting where a refresh meets its measure loop's first fills, and the refresh
 * rule there decides it. hwtests/line-fill reads fills 9.12-9.28 cycles apart a line (9 and the refreshes) - an average.
 * hwtests/refresh-io measures a refresh against warm code only. This measures it against cold code, phase by phase.
 *
 * WHAT IT DOES. Interrupts off; Timer 2 at the system clock, free-running (its count mod 256 keeps the 256-cycle refresh
 * grid's phase). CALIBRATION as refresh-io: `lhu T2; jalr; lw RAM; jr; lhu T2` (warm) at every phase; F = the first
 * phase of its highest cost (the RAM read waits for the refresh). Then each case: a block of 20 cache lines, all
 * nops but what the case puts in its line 8 (the ninth) and the return in its last, each block in its own 512-byte
 * slot of a 4 KB page. A trial runs the evictor for the block's slot (32 lines of nops in another page sharing its cache
 * lines - nothing else leaves the cache), spins on Timer 2 to a target phase, runs 0..7 nops, reads Timer 2, calls the
 * block (every line a miss) and reads Timer 2 again when it returns - all of it from one warm slot, so the only bus
 * traffic in the timed window is the block's. 16 trials at each of the 256 targets.
 *   cases (line 8 of the block):
 *   N    nops only
 *   L1   lw RAM at word 1
 *   L3   lw RAM at word 3
 *   LU   lw RAM at word 1, its reader at word 2
 *   LL   lw RAM at words 1 and 2 (one page)
 *   LX   lw RAM at word 1 and two lines on
 *   LLL  lw RAM at words 0, 1 and 2
 *   S1   sw RAM at word 1
 *   SS   sw RAM at words 1 and 2
 *   SL   sw RAM at word 1, lw RAM at word 3
 *   I1   lw I_STAT at word 1
 *   IU   lw I_STAT at word 1, its reader at word 2
 * Each prints its commonest cost, then the cost (the commonest of the trials that landed there; '.' where none did) by
 * x = (F - the first Timer 2 read's phase) mod 256 - x is how many cycles into the window the refresh comes due, so
 * the rows read in the block's own order - sixteen to a line.
 *
 * READ IT LIKE THIS. Where the refresh falls outside the block (x past its cost) a row is flat. Inside, N gives what a
 * refresh costs a run of fills at each place against the fill's nine cycles (5 everywhere if it always waits for the
 * next fill and holds it; less where it fits the bus's idle cycles between one fill's burst and the next request). The
 * other rows minus N, near line 8's time (x about 72), say where the stream's read or store goes against it.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js refresh-cold --frames=3000 --quiet=600`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_MODE 0x1F801124u
#define NC 12

extern "C" {
uint32_t rcRam[64] __attribute__((aligned(64)));
void rcSweep(void (*blk)(void), void (*ev)(void), uint8_t* hist, int reps);
void rcCalib(void);
void rcB0(void); void rcB1(void); void rcB2(void); void rcB3(void); void rcB4(void); void rcB5(void); void rcB6(void); void rcB7(void); void rcB8(void); void rcB9(void); void rcB10(void); void rcB11(void);
void rcE0(void); void rcE1(void); void rcE2(void); void rcE3(void); void rcE4(void); void rcE5(void); void rcE6(void);
}

__asm__(
    ".text\n"
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".balign 4096\n"
    ".balign 512\n"
    ".global rcB0\n"
    "rcB0:\n"
    ".rept 78\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB1\n"
    "rcB1:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0($s3)\n"
    ".rept 44\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB2\n"
    "rcB2:\n"
    ".rept 35\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0($s3)\n"
    ".rept 42\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB3\n"
    "rcB3:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0($s3)\n"
    "addu $t6, $t5, $t5\n"
    ".rept 43\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB4\n"
    "rcB4:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0($s3)\n"
    "lw $t6, 4($s3)\n"
    ".rept 43\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB5\n"
    "rcB5:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0($s3)\n"
    ".rept 7\n"
    "nop\n"
    ".endr\n"
    "lw $t6, 4($s3)\n"
    ".rept 36\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB6\n"
    "rcB6:\n"
    ".rept 32\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0($s3)\n"
    "lw $t6, 4($s3)\n"
    "lw $t7, 8($s3)\n"
    ".rept 43\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcSweep\n"
    "rcSweep:\n"
    "addiu $sp, $sp, -48\n"
    "sw $ra, 44($sp)\n"
    "sw $s0, 40($sp)\n"
    "sw $s1, 36($sp)\n"
    "sw $s2, 32($sp)\n"
    "sw $s3, 28($sp)\n"
    "sw $s4, 24($sp)\n"
    "sw $s5, 20($sp)\n"
    "sw $s6, 16($sp)\n"
    "sw $s7, 12($sp)\n"
    "move $s0, $a0\n"
    "move $s1, $a1\n"
    "move $s2, $a2\n"
    "move $s7, $a3\n"
    "lui $s3, %hi(rcRam)\n"
    "addiu $s3, $s3, %lo(rcRam)\n"
    "li $s4, -1\n"
    "rcRep:\n"
    "li $s5, 0\n"
    "rcTgt:\n"
    "beqz $s1, rcSpin0\n"
    "nop\n"
    "jalr $s1\n"
    "nop\n"
    "rcSpin0:\n"
    "lui $t4, 0x1f80\n"
    "rcSpin:\n"
    "lhu $t0, 0x1120($t4)\n"
    "nop\n"
    "subu $t0, $t0, $s5\n"
    "andi $t0, $t0, 0xff\n"
    "sltiu $t0, $t0, 6\n"
    "beqz $t0, rcSpin\n"
    "nop\n"
    "andi $t1, $s4, 7\n"
    "sll $t1, $t1, 2\n"
    "lui $t0, %hi(rcSled)\n"
    "addiu $t0, $t0, %lo(rcSled)\n"
    "subu $t0, $t0, $t1\n"
    "jr $t0\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "nop\n"
    "rcSled:\n"
    "lhu $s6, 0x1120($t4)\n"
    "jalr $s0\n"
    "nop\n"
    "lhu $t3, 0x1120($t4)\n"
    "bltz $s4, rcDrain\n"
    "subu $t3, $t3, $s6\n"
    "andi $t3, $t3, 0xffff\n"
    "sltiu $t0, $t3, 512\n"
    "beqz $t0, rcDrain\n"
    "andi $t1, $s6, 0xff\n"
    "sll $t1, $t1, 9\n"
    "addu $t1, $t1, $t3\n"
    "addu $t1, $t1, $s2\n"
    "lbu $t0, 0($t1)\n"
    "nop\n"
    "sltiu $t2, $t0, 255\n"
    "beqz $t2, rcDrain\n"
    "addiu $t0, $t0, 1\n"
    "sb $t0, 0($t1)\n"
    "rcDrain:\n"
    "li $t0, 8\n"
    "rcDrain1:\n"
    "addiu $t0, $t0, -1\n"
    "bnez $t0, rcDrain1\n"
    "nop\n"
    "bltz $s4, rcNext\n"
    "nop\n"
    "addiu $s5, $s5, 1\n"
    "sltiu $t0, $s5, 256\n"
    "bnez $t0, rcTgt\n"
    "nop\n"
    "rcNext:\n"
    "addiu $s4, $s4, 1\n"
    "slt $t0, $s4, $s7\n"
    "bnez $t0, rcRep\n"
    "nop\n"
    "lw $ra, 44($sp)\n"
    "lw $s0, 40($sp)\n"
    "lw $s1, 36($sp)\n"
    "lw $s2, 32($sp)\n"
    "lw $s3, 28($sp)\n"
    "lw $s4, 24($sp)\n"
    "lw $s5, 20($sp)\n"
    "lw $s6, 16($sp)\n"
    "lw $s7, 12($sp)\n"
    "jr $ra\n"
    "addiu $sp, $sp, 48\n"
    ".global rcCalib\n"
    "rcCalib:\n"
    "lw $t5, 0($s3)\n"
    "jr $ra\n"
    "nop\n"
    ".balign 4096\n"
    ".balign 512\n"
    ".global rcB7\n"
    "rcB7:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "sw $zero, 16($s3)\n"
    ".rept 44\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB8\n"
    "rcB8:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "sw $zero, 16($s3)\n"
    "sw $zero, 20($s3)\n"
    ".rept 43\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB9\n"
    "rcB9:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "sw $zero, 16($s3)\n"
    "nop\n"
    "lw $t5, 0($s3)\n"
    ".rept 42\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB10\n"
    "rcB10:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0x1070($t4)\n"
    ".rept 44\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    ".global rcB11\n"
    "rcB11:\n"
    ".rept 33\n"
    "nop\n"
    ".endr\n"
    "lw $t5, 0x1070($t4)\n"
    "addu $t6, $t5, $t5\n"
    ".rept 43\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".balign 512\n"
    "nop\n"
    ".balign 512\n"
    "nop\n"
    ".balign 512\n"
    "nop\n"
    ".balign 4096\n"
    ".balign 512\n"
    ".global rcE0\n"
    "rcE0:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    ".global rcE1\n"
    "rcE1:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    ".global rcE2\n"
    "rcE2:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    ".global rcE3\n"
    "rcE3:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    ".global rcE4\n"
    "rcE4:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    ".global rcE5\n"
    "rcE5:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    ".global rcE6\n"
    "rcE6:\n"
    ".rept 124\n"
    "nop\n"
    ".endr\n"
    "jr $ra\n"
    "nop\n"
    ".rept 2\n"
    "nop\n"
    ".endr\n"
    ".balign 512\n"
    "nop\n"
    ".set pop\n"
);

typedef void (*Fn)(void);
static const Fn BLK[NC] = { rcB0, rcB1, rcB2, rcB3, rcB4, rcB5, rcB6, rcB7, rcB8, rcB9, rcB10, rcB11 };
static const Fn EV[NC] = { rcE0, rcE1, rcE2, rcE3, rcE4, rcE5, rcE6, rcE0, rcE1, rcE2, rcE3, rcE4 };
static const char* const NAME[NC] = {
    "N    nops only",
    "L1   lw RAM at word 1",
    "L3   lw RAM at word 3",
    "LU   lw RAM at word 1, its reader at word 2",
    "LL   lw RAM at words 1 and 2 (one page)",
    "LX   lw RAM at word 1 and two lines on",
    "LLL  lw RAM at words 0, 1 and 2",
    "S1   sw RAM at word 1",
    "SS   sw RAM at words 1 and 2",
    "SL   sw RAM at word 1, lw RAM at word 3",
    "I1   lw I_STAT at word 1",
    "IU   lw I_STAT at word 1, its reader at word 2"
};
static uint8_t hist[256 * 512];     /* [phase][cost] trial counts */

static void clear() { for (int i = 0; i < 256 * 512; i++) hist[i] = 0; }
static int modeAt(int ph) {
    const uint8_t* h = &hist[(ph & 0xFF) * 512];
    int best = -1, bn = 0;
    for (int c = 0; c < 512; c++) if (h[c] > bn) { bn = h[c]; best = c; }
    return best;
}
static int modeAll() {
    static uint32_t tot[512];
    for (int c = 0; c < 512; c++) tot[c] = 0;
    for (int p = 0; p < 256; p++) for (int c = 0; c < 512; c++) tot[c] += hist[p * 512 + c];
    int best = 0;
    for (int c = 1; c < 512; c++) if (tot[c] > tot[best]) best = c;
    return best;
}

int main() {
    printf("\ncpu/refresh-cold\n");
    printf("What a DRAM refresh costs cold code (every line a miss) at each phase from it (Timer 2 cycles).\n");
    printf("No assertions - compare against your emulator.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);

    clear();
    rcSweep(rcCalib, 0, hist, 16);
    const int base = modeAll();
    int F = 0, hi = -1;
    for (int p = 0; p < 256; p++) { const int m = modeAt(p); if (m > hi) { hi = m; F = p; } }
    printf("CALIBRATION  lhu T2; jalr; lw RAM; jr; lhu T2: commonest %d, highest %d first at phase %d (F)\n", base, hi, F);
    printf("  F-8 .. F+15:");
    for (int k = -8; k < 16; k++) { const int m = modeAt(F + k); if (m < 0) printf(" ."); else printf(" %d", m); }
    printf("\n\n");

    for (int i = 0; i < NC; i++) {
        clear();
        rcSweep(BLK[i], EV[i], hist, 16);
        printf("%s  commonest %d; cost by x = 0 .. 255:\n", NAME[i], modeAll());
        for (int x = 0; x < 256; x++) {
            const int m = modeAt(F - x);
            if (m < 0) printf("    ."); else printf(" %4d", m);
            if (x % 16 == 15) printf("\n");
        }
    }

    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}
}
