/*
 * cpu/bus-latch - what puts its word on the main bus, and in which order?
 *
 * WHY (2026-10-08, v0.377.46). hwtests/imask-upper on the SCPH-5501, 7501 and
 * 9001 showed the top half of a 32-bit read of a 16-bit register (I_MASK,
 * I_STAT, the timers, RAM_SIZE) is the last word on the main bus: a RAM load's
 * data, a store's (a Timer 2 store's, but not a store to I_MASK itself), an
 * I-cache fill's last word, and from uncached code the instruction two after
 * the read. src/psx.js (busWord, busValue) now keeps that word. What nothing
 * measured, and the model therefore had to pick:
 *   - does a store to I_STAT drive it (the model: yes, as Timer 2's)?
 *   - which lanes a narrow RAM store drives (the model: the register shifted to
 *     the byte lane, as the on-die registers latch it - M198/M202);
 *   - whether a BIOS ROM read, an SPU read or write, or a DMA's words reach it
 *     (the model: the SPU store yes, the rest no);
 *   - THE ORDER. A data word goes on the bus at its instruction's MEM stage,
 *     three slots after its fetch. ps1-tests' own console says so for the fetches
 *     AFTER the read (io-access-bitwidth: a store right before the read lands
 *     after the next fetch's line fill, and before the fill of the one after).
 *     The model keeps that, and one slot back puts the data after the read's own
 *     line fill too (that console's I_MASK sw cell), but TWO slots back keeps
 *     program order (data first) where the pipeline puts it after - exact order
 *     would cost the core a counter a slot (~2% of its speed).
 *
 * WHAT IT DOES. Interrupts off. Each case sets a marker (a store of 5A5Axxxxh to
 * Timer 2's target, which the consoles showed latches), does one thing, and reads
 * I_MASK with lw a few instructions later; the top half is printed with what each
 * answer means. The HOT cases run twice and keep the second call (their lines
 * cached); the COLD cases run straight after the kernel's FlushCache (A0h:44h), so
 * the read's own line misses: a store at word 3, 2 or 1 of one line (one, two,
 * three slots before the read) and the read at word 0 of the next.
 *
 * READ IT LIKE THIS. Each row prints the top half and what it would be under each
 * rule. COLD k1 / k2: 5A5A means the pipeline's order (the store lands after the
 * fill of the read's own fetch), 03E0 (the line's last word, `jr ra`) program
 * order; COLD k3 should read 03E0 either way (the store and the fill in the same
 * slot, the fetch after).
 *
 * ON THIS CORE (2026-10-08, v0.377.46, `node hwtests/run.js bus-latch`): every
 * row the answer marked "(this core)" - I_STAT A1A1; the I_MASK store 5A5A; sh
 * B3B3 / B2B2, sb C700 / C5C6; ROM and SPU reads 5A5A, the SPU store D6D6; RAM
 * C3C3; DMA 1100; COLD k1 5A5A and the k1 load C3C3 (pipeline order: io-access-bitwidth's I_MASK
 * cell measured it one slot back), k2 03E0 (program order - this row is the question), k3 03E0; the hot control
 * 5A5A.
 * A console that differs on a row has decided that row.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define I_MASK    0x1F801074u
#define I_STAT    0x1F801070u
#define T2_TARGET 0x1F801128u
#define SPU_V0ADSR 0x1F801C08u
#define D6_MADR   0x1F8010E0u
#define D6_BCR    0x1F8010E4u
#define D6_CHCR   0x1F8010E8u
#define DPCR      0x1F8010F0u

extern "C" {
uint32_t bl_store(uint32_t mark, uint32_t addr, uint32_t val, uint32_t rd);
uint32_t bl_storeh(uint32_t mark, uint32_t addr, uint32_t val, uint32_t rd);
uint32_t bl_storeb(uint32_t mark, uint32_t addr, uint32_t val, uint32_t rd);
uint32_t bl_markload(uint32_t mark, uint32_t val, uint32_t rd, uint32_t from);
uint32_t bl_markdma(uint32_t mark, uint32_t val, uint32_t rd, uint32_t chcr);
uint32_t bl_cold_k1(uint32_t mark, uint32_t val, uint32_t rd);
uint32_t bl_cold_k2(uint32_t mark, uint32_t val, uint32_t rd);
uint32_t bl_cold_k3(uint32_t mark, uint32_t val, uint32_t rd);
uint32_t bl_cold_load_k1(uint32_t from, uint32_t val, uint32_t rd);
}

__asm__(".set push\n.set noreorder\n.text\n"
    /* the marker (Timer 2's target - the C code may have stored the case's value on the stack), a store, three nops,
     * the read */
    ".balign 64\n.global bl_store\nbl_store:\n"
    "  lui $t0, 0x1F80\n  sw $a0, 0x1128($t0)\n  nop\n  nop\n  sw $a2, 0($a1)\n  nop\n  nop\n  nop\n  lw $v0, 0($a3)\n"
    "  nop\n  nop\n  jr $ra\n  nop\n"
    ".balign 64\n.global bl_storeh\nbl_storeh:\n"
    "  lui $t0, 0x1F80\n  sw $a0, 0x1128($t0)\n  nop\n  nop\n  sh $a2, 0($a1)\n  nop\n  nop\n  nop\n  lw $v0, 0($a3)\n"
    "  nop\n  nop\n  jr $ra\n  nop\n"
    ".balign 64\n.global bl_storeb\nbl_storeb:\n"
    "  lui $t0, 0x1F80\n  sw $a0, 0x1128($t0)\n  nop\n  nop\n  sb $a2, 0($a1)\n  nop\n  nop\n  nop\n  lw $v0, 0($a3)\n"
    "  nop\n  nop\n  jr $ra\n  nop\n"
    /* the marker, a load from `from`, three nops, the read */
    ".balign 64\n.global bl_markload\nbl_markload:\n"
    "  sw $a1, 0($a0)\n  nop\n  nop\n  lw $t0, 0($a3)\n  nop\n  nop\n  nop\n  lw $v0, 0($a2)\n  nop\n  nop\n  jr $ra\n  nop\n"
    /* the marker, DMA6 (OTC) started and waited for, the read */
    ".balign 64\n.global bl_markdma\nbl_markdma:\n"
    "  sw $a1, 0($a0)\n  lui $t1, 0x1100\n  ori $t1, $t1, 2\n  sw $t1, 0($a3)\n"
    "1:\n  lw $t2, 0($a3)\n  nop\n  srl $t2, $t2, 24\n  andi $t2, $t2, 1\n  bnez $t2, 1b\n  nop\n"
    "  nop\n  nop\n  nop\n  lw $v0, 0($a2)\n  nop\n  nop\n  jr $ra\n  nop\n"
    /* COLD: the store at word 3 / 2 / 1 of a line, the read at word 0 of the next */
    ".balign 64\n.global bl_cold_k1\nbl_cold_k1:\n"
    "  nop\n  nop\n  nop\n  sw $a1, 0($a0)\n  lw $v0, 0($a2)\n  nop\n  nop\n  jr $ra\n  nop\n"
    ".balign 64\n.global bl_cold_k2\nbl_cold_k2:\n"
    "  nop\n  nop\n  sw $a1, 0($a0)\n  nop\n  lw $v0, 0($a2)\n  nop\n  nop\n  jr $ra\n  nop\n"
    ".balign 64\n.global bl_cold_k3\nbl_cold_k3:\n"
    "  nop\n  sw $a1, 0($a0)\n  nop\n  nop\n  lw $v0, 0($a2)\n  nop\n  nop\n  jr $ra\n  nop\n"
    /* COLD: a RAM load at word 3, the read at word 0 of the next */
    ".balign 64\n.global bl_cold_load_k1\nbl_cold_load_k1:\n"
    "  nop\n  nop\n  nop\n  lw $t0, 0($a0)\n  lw $v0, 0($a2)\n  nop\n  nop\n  jr $ra\n  nop\n"
    ".set pop\n");

/* The kernel's FlushCache, A0h function 44h: every I-cache line invalid (bios-window's). */
static void flushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}

static volatile uint32_t ramWord = 0xC3C35A00u;
static volatile uint32_t ramPad[4] = { 0, 0, 0, 0 };
static uint32_t ot[16];

static void row(const char *what, uint32_t v, const char *rules) {
    printf("  %-40s %04X   %s\n", what, (unsigned)(v >> 16), rules);
}

int main() {
    printf("\ncpu/bus-latch\n");
    printf("What fills the top half of a 32-bit read of I_MASK: each case's last word on the main bus.\n");
    printf("There are no assertions - compare against your emulator, and say which console model ran it.\n\n");
    EnterCriticalSection();
    const uint32_t mask = read32(I_MASK) & 0x7FF;
    const uint32_t t2 = read32(T2_TARGET) & 0xFFFF;
    const uint32_t mark = 0x5A5A0000u | t2;
    const uint16_t adsrLo = read16(SPU_V0ADSR), adsrHi = read16(SPU_V0ADSR + 2);
    uint32_t v[16];

    printf("HOT (second call, lines cached)                    top    rules\n");
    for (int rep = 0; rep < 2; rep++) v[0] = bl_store(mark, I_STAT, 0xA1A1FFFFu, I_MASK);
    row("sw A1A1FFFFh to I_STAT", v[0], "A1A1 it drives the bus (this core), 5A5A it does not");
    for (int rep = 0; rep < 2; rep++) v[1] = bl_store(mark, I_MASK, 0xA5A50000u | mask, T2_TARGET);
    row("sw A5A5xxxxh to I_MASK, read T2 target", v[1], "5A5A it does not (imask-upper; this core), A5A5 it does when another is read");
    for (int rep = 0; rep < 2; rep++) v[2] = bl_storeh(mark, (uint32_t)&ramPad[0] + 2, 0xB2B2B3B3u, I_MASK);
    row("sh B2B2B3B3h to RAM+2", v[2], "B3B3 the halfword shifted to its lane (this core), B2B2 the register");
    for (int rep = 0; rep < 2; rep++) v[3] = bl_storeh(mark, (uint32_t)&ramPad[1], 0xB2B2B3B3u, I_MASK);
    row("sh B2B2B3B3h to RAM+0", v[3], "B2B2 the register's own upper half (this core), 0000 lanes left undriven");
    for (int rep = 0; rep < 2; rep++) v[4] = bl_storeb(mark, (uint32_t)&ramPad[2] + 3, 0xC4C5C6C7u, I_MASK);
    row("sb C4C5C6C7h to RAM+3", v[4], "C700 the byte shifted to its lane (this core), C4C5 the register");
    for (int rep = 0; rep < 2; rep++) v[5] = bl_storeb(mark, (uint32_t)&ramPad[3] + 1, 0xC4C5C6C7u, I_MASK);
    row("sb C4C5C6C7h to RAM+1", v[5], "C5C6 the byte shifted to its lane (this core), C4C5 the register");
    for (int rep = 0; rep < 2; rep++) v[6] = bl_markload(T2_TARGET, mark, I_MASK, 0xBFC00000u);
    row("marker, lw BFC00000h (ROM, 3C08xxxxh)", v[6], "3C08 a ROM read drives it, 5A5A it does not (this core)");
    write16(SPU_V0ADSR, 0x1111); write16(SPU_V0ADSR + 2, 0xE5E5);
    for (int rep = 0; rep < 2; rep++) v[7] = bl_markload(T2_TARGET, mark, I_MASK, SPU_V0ADSR);
    row("marker, lw SPU voice 0 ADSR (E5E51111h)", v[7], "E5E5 an SPU read drives it, 5A5A it does not (this core)");
    for (int rep = 0; rep < 2; rep++) v[8] = bl_store(mark, SPU_V0ADSR, 0xD6D62222u, I_MASK);
    row("sw D6D62222h to SPU voice 0 ADSR", v[8], "D6D6 an SPU store drives it (this core), 5A5A it does not");
    write16(SPU_V0ADSR, adsrLo); write16(SPU_V0ADSR + 2, adsrHi);
    for (int rep = 0; rep < 2; rep++) v[9] = bl_markload(T2_TARGET, mark, I_MASK, (uint32_t)&ramWord);
    row("marker, lw RAM (C3C35A00h) - control", v[9], "C3C3 (imask-upper's row)");
    write32(DPCR, read32(DPCR) | 0x08000000u);
    write32(D6_MADR, (uint32_t)&ot[15]); write32(D6_BCR, 16);
    for (int rep = 0; rep < 2; rep++) v[10] = bl_markdma(T2_TARGET, mark, I_MASK, D6_CHCR);
    row("marker, DMA6 OTC ends on 00FFFFFFh", v[10], "00FF the DMA's last word, 1100 the CHCR store's own (this core)");

    printf("\nCOLD (after FlushCache: the read's own line misses)\n");
    for (int rep = 0; rep < 3; rep++) {
        flushCache(); v[11] = bl_cold_k1(T2_TARGET, mark, I_MASK);
        row("sw marker 1 slot before the read", v[11], "5A5A pipeline order (this core), 03E0 program order");
        flushCache(); v[12] = bl_cold_k2(T2_TARGET, mark, I_MASK);
        row("sw marker 2 slots before the read", v[12], "5A5A pipeline order, 03E0 program order (this core)");
        flushCache(); v[13] = bl_cold_k3(T2_TARGET, mark, I_MASK);
        row("sw marker 3 slots before the read", v[13], "03E0 both (a tie: the fetch after the store)");
        flushCache(); v[14] = bl_cold_load_k1((uint32_t)&ramWord, 0, I_MASK);
        row("lw RAM (C3C3) 1 slot before the read", v[14], "C3C3 pipeline order (this core), 03E0 program order");
    }
    for (int rep = 0; rep < 2; rep++) v[15] = bl_cold_k1(T2_TARGET, mark, I_MASK);
    row("the 1-slot store, hot - control", v[15], "5A5A both (no fill between)");

    write32(I_MASK, mask);
    write32(T2_TARGET, t2);
    ExitCriticalSection();
    printf("\ncpu/bus-latch: Done.\n");
    for (;;) VSync(0);
}
