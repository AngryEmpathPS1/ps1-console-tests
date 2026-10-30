/*
 * cpu/bios-window - what does a read past the 512 KB BIOS ROM return at each
 * window size, and how late does a bus error land?
 *
 * WHY (ROAD A8 "the BIOS mirror at 1FC80000h", dug 2026-09-27; ROAD B). The
 * BIOS ROM is 512 KB and its region 4 MB (psx-spx "512K BIOS ROM (Kernel)
 * (4096K max)"). Bits 16-20 of 1F801010h, BIOS ROM Delay/Size, set a window
 * of 1 SHL N bytes, and psx-spx says "Trying to access addresses that exceed
 * the selected size causes an exception" (max 16h, 4 MB); Sony's Hardware
 * manual maps the ROM at 1FC00000h-1FC7FFFFh and calls an unmapped access a
 * Bus Error. Every BIOS but one sets 13h (512 KB); v4.0 sets 14h (1 MB, psx-spx
 * "they did accidentally still use the 1024K setting"). So what is in the
 * window past the chip? Four references, four answers: DuckStation mirrors
 * the ROM through 4 MB (for the PS2 BIOS, 19063d4), Redux reads FFh, Mednafen
 * and MiSTer 0, none with an exception. This emulator raises a bus error past
 * 512 KB at every window size and never faults inside the chip: it ignores
 * the size field.
 *
 * And WHEN a bus error lands. nocash's DIAG (DIAG.A22 4362-4368): "the CPU
 * seems to keep executing the next 1-3 opcodes after the fault location, and
 * THEN trigger the exception" unless the access sits where his .align puts it
 * or the code is uncached. His graded rows (CPU EXCEPTIONS > BUS ERRORS) use
 * those alignments and want EPC at the faulting access, and this emulator
 * raises every bus error there (M202 took his CAUSE.CE rows: CE is the NEXT
 * instruction's). A compiled-out experiment beside them (4454-4476) notes a
 * different opcode skipped at each of four line positions - notes that do
 * not square with his own graded rows, so this test measures instead.
 *
 * WHAT IT DOES. Interrupts off, our own exception handler at 80000080h (the
 * kernel's is put back before anything prints: its printf runs from the ROM).
 *   WINDOW   at sizes 12h-16h, a timed lw of BFC00000h + 0, 3FFFCh, 40000h,
 *            7FFFCh, 80000h, 80100h, FFFFCh, 100000h, 1FFFFCh, 200000h,
 *            3FFFFCh: the value (or the bus error), and Timer 2 around it (a
 *            bus error's time includes the handler)
 *   LATENESS nocash's method. The faulting access at word 0, 1, 2 or 3 of an
 *            I-cache line, then eight ORI/XORI setting bits 0-7 of a register;
 *            the handler shifts that register up 16 and returns to EPC+4, so
 *            the bits set BEFORE the exception end up in the top half and the
 *            opcode at EPC (skipped) is missing from both. Cold (after
 *            FlushCache), warm (again at once) and uncached (KSEG1). Five
 *            accesses: lw BFC80000h under window 13h (the question above), and
 *            DIAG's four - lbu 1F801024h and sh 1F801078h (I/O gaps), lw and
 *            sw at 08000000h (past the RAM window; the store aimed where a
 *            mirror would land on a marked RAM word)
 *
 * READ IT LIKE THIS. WINDOW: past 7FFFCh, a value equal to the ROM word 512 KB
 * below is a mirror, FFFFFFFF or 0 is an empty bus, BUS ERROR is the window
 * doing its job. LATENESS: EPC +0, before 00, after FF is an exact exception
 * (the access skipped, all eight run after it); EPC +2, before 01, after FC
 * is an exception one opcode late - the first ORI ran first and the XORI at
 * EPC was skipped. CAUSE has its IP bits masked (an interrupt pending is
 * noise); its CE field (bits 28-29) is the opcode "in hand" (M202): ORI's 1,
 * XORI's 2, NOP's 0.
 *
 * ON THIS CORE (v0.258): the size field is ignored. Every read past 7FFFCh is
 * a bus error at every size, and at 12h the reads inside the chip still return
 * the ROM (the register holds each size it is given: 0012243Fh .. 0016243Fh).
 * A ROM read is 37 cycles through this bracket; a bus error 28 WITH the
 * handler - the faulting access itself costs nothing here, which a console
 * will not agree with. Every bus error is exact (EPC +0, before 00, after FF)
 * at every word, cold, warm and uncached; CE is the ORI's 1 after a load or an
 * I/O store and the XORI's 2 after the RAM store (M202: a RAM store faults
 * through the write buffer); the loaded register keeps 1EF7 and the marked
 * RAM word is intact.
 * TEST THE TEST: a scratch core whose bus errors land K = 1, 2, 3 opcodes late
 * (the access aborted, the exception raised at the K-th opcode after it)
 * reads EPC +K, before 00 / 01 / 03, after FE / FC / F8 on every row.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define BIOS_REG 0x1F801010u
#define T2_MODE  0x1F801124u

extern "C" {
volatile uint32_t bw_epc, bw_cause, bw_value;
void bw_vector(void);
uint32_t bw_timed(uint32_t addr);
extern char bw_timed_ld[];
#define DECL(N) uint32_t N(uint32_t); extern char N##_ld[];
DECL(bw_lw0) DECL(bw_lw1) DECL(bw_lw2) DECL(bw_lw3)
DECL(bw_lbu0) DECL(bw_lbu1) DECL(bw_lbu2) DECL(bw_lbu3)
DECL(bw_sh0) DECL(bw_sh1) DECL(bw_sh2) DECL(bw_sh3)
DECL(bw_sw0) DECL(bw_sw1) DECL(bw_sw2) DECL(bw_sw3)
}

/* One access at word OFF of an I-cache line, then eight opcodes that each set one bit of $t2 (nocash's r10);
 * ORI and XORI alternate so CAUSE.CE (the opcode in hand, M202) says which one it was: 1 and 2. */
#define BW_CASE(NAME, OFF, OP) \
    ".balign 16\n" \
    ".global " #NAME "\n.global " #NAME "_ld\n" \
    #NAME ":\n" \
    "  li    $t2, 0\n" \
    "  li    $v1, 0x1EF7\n" \
    "  .balign 16\n" \
    "  .rept " #OFF "\n  nop\n  .endr\n" \
    #NAME "_ld:\n" \
    "  " OP "\n" \
    "  ori   $t2, $t2, 0x01\n  xori  $t2, $t2, 0x02\n  ori   $t2, $t2, 0x04\n  xori  $t2, $t2, 0x08\n" \
    "  ori   $t2, $t2, 0x10\n  xori  $t2, $t2, 0x20\n  ori   $t2, $t2, 0x40\n  xori  $t2, $t2, 0x80\n" \
    "  nop\n  nop\n  nop\n  nop\n" \
    "  lui   $t4, %hi(bw_value)\n" \
    "  sw    $v1, %lo(bw_value)($t4)\n" \
    "  jr    $ra\n" \
    "  move  $v0, $t2\n"

__asm__(
    ".text\n"
    ".set push\n.set noreorder\n.set noat\n"
    /* Copied to 80000080h: four words, as the kernel's own vector. */
    ".balign 16\n"
    ".global bw_vector\n"
    "bw_vector:\n"
    "  lui   $k0, %hi(bw_handler)\n"
    "  addiu $k0, $k0, %lo(bw_handler)\n"
    "  jr    $k0\n"
    "  nop\n"
    /* nocash's handler (DIAG.A22 4699-4720): the bits set so far move up 16, EPC and CAUSE are kept, and it
     * returns to EPC+4 - skipping the faulting access if the exception is exact, the opcode at EPC if late. */
    ".global bw_handler\n"
    "bw_handler:\n"
    "  sll   $t2, $t2, 16\n"
    "  mfc0  $k1, $13\n"
    "  lui   $k0, %hi(bw_cause)\n"
    "  sw    $k1, %lo(bw_cause)($k0)\n"
    "  mfc0  $k1, $14\n"
    "  lui   $k0, %hi(bw_epc)\n"
    "  sw    $k1, %lo(bw_epc)($k0)\n"
    "  addiu $k0, $k1, 4\n"
    "  jr    $k0\n"
    "  rfe\n"
    /* A timed load: Timer 2 before and after, eight nops of room for a late exception. */
    ".balign 16\n"
    ".global bw_timed\n.global bw_timed_ld\n"
    "bw_timed:\n"
    "  lui   $t3, 0x1F80\n"
    "  li    $v1, 0x1EF7\n"
    "  lhu   $t0, 0x1120($t3)\n"
    "bw_timed_ld:\n"
    "  lw    $v1, 0($a0)\n"
    "  .rept 8\n  nop\n  .endr\n"
    "  lhu   $t1, 0x1120($t3)\n"
    "  lui   $t4, %hi(bw_value)\n"
    "  sw    $v1, %lo(bw_value)($t4)\n"
    "  subu  $v0, $t1, $t0\n"
    "  jr    $ra\n"
    "  andi  $v0, $v0, 0xFFFF\n"
    BW_CASE(bw_lw0, 0, "lw $v1, 0($a0)") BW_CASE(bw_lw1, 1, "lw $v1, 0($a0)")
    BW_CASE(bw_lw2, 2, "lw $v1, 0($a0)") BW_CASE(bw_lw3, 3, "lw $v1, 0($a0)")
    BW_CASE(bw_lbu0, 0, "lbu $v1, 0($a0)") BW_CASE(bw_lbu1, 1, "lbu $v1, 0($a0)")
    BW_CASE(bw_lbu2, 2, "lbu $v1, 0($a0)") BW_CASE(bw_lbu3, 3, "lbu $v1, 0($a0)")
    BW_CASE(bw_sh0, 0, "sh $v1, 0($a0)") BW_CASE(bw_sh1, 1, "sh $v1, 0($a0)")
    BW_CASE(bw_sh2, 2, "sh $v1, 0($a0)") BW_CASE(bw_sh3, 3, "sh $v1, 0($a0)")
    BW_CASE(bw_sw0, 0, "sw $v1, 0($a0)") BW_CASE(bw_sw1, 1, "sw $v1, 0($a0)")
    BW_CASE(bw_sw2, 2, "sw $v1, 0($a0)") BW_CASE(bw_sw3, 3, "sw $v1, 0($a0)")
    ".set pop\n");

typedef uint32_t (*Case)(uint32_t);

/* The kernel's FlushCache, A0h function 44h: every I-cache line invalid. */
static void flushCache() {
    __asm__ volatile("li $t2, 0xA0\n\tli $t1, 0x44\n\tjalr $t2\n\tnop\n\t"
                     :::"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9",
                         "v0", "v1", "a0", "a1", "a2", "a3", "ra", "memory");
}

static Case uncached(Case f) { return (Case)(((uint32_t)f & 0x1FFFFFFFu) | 0xA0000000u); }
static int opcodesPast(uint32_t epc, const char *ld) {
    return ((int)(epc & 0x1FFFFFFFu) - (int)((uint32_t)ld & 0x1FFFFFFFu)) / 4;
}

#define NSIZE 5
#define NOFF 11
static const uint8_t sizes[NSIZE] = { 0x12, 0x13, 0x14, 0x15, 0x16 };
static const uint32_t offs[NOFF] = { 0x000000, 0x03FFFC, 0x040000, 0x07FFFC, 0x080000, 0x080100,
                                     0x0FFFFC, 0x100000, 0x1FFFFC, 0x200000, 0x3FFFFC };
static uint32_t wVal[NSIZE][NOFF], wEpc[NSIZE][NOFF], wCause[NSIZE][NOFF], wCyc[NSIZE][NOFF], romWord[NOFF], wReg[NSIZE];

#define NKIND 5
static const char *kindName[NKIND] = { "lw  BFC80000h (13h)", "lbu 1F801024h", "lw  08000000h", "sh  1F801078h",
                                       "sw  08xxxxxxh" };
static const char *modeName[3] = { "cold", "warm", "uncached" };
static uint32_t lR10[NKIND][4][3], lEpc[NKIND][4][3], lCause[NKIND][4][3], lVal[NKIND][4][3];
static volatile uint32_t marked = 0x55AA6677;   /* where a mirror of the RAM store would land */

int main() {
    printf("\ncpu/bios-window\n");
    printf("Reads past the 512 KB BIOS ROM at each window size, and where a bus error lands.\n");
    printf("There are no assertions - compare against your emulator.\n\n");

    static const Case lw[4] = { bw_lw0, bw_lw1, bw_lw2, bw_lw3 }, lbu[4] = { bw_lbu0, bw_lbu1, bw_lbu2, bw_lbu3 };
    static const Case sh[4] = { bw_sh0, bw_sh1, bw_sh2, bw_sh3 }, sw[4] = { bw_sw0, bw_sw1, bw_sw2, bw_sw3 };
    static const char *lwL[4] = { bw_lw0_ld, bw_lw1_ld, bw_lw2_ld, bw_lw3_ld };
    static const char *lbuL[4] = { bw_lbu0_ld, bw_lbu1_ld, bw_lbu2_ld, bw_lbu3_ld };
    static const char *shL[4] = { bw_sh0_ld, bw_sh1_ld, bw_sh2_ld, bw_sh3_ld };
    static const char *swL[4] = { bw_sw0_ld, bw_sw1_ld, bw_sw2_ld, bw_sw3_ld };
    const Case *kindFn[NKIND] = { lw, lbu, lw, sh, sw };
    const char *const *kindLd[NKIND] = { lwL, lbuL, lwL, shL, swL };
    const uint32_t kindAddr[NKIND] = { 0xBFC80000u, 0x1F801024u, 0x08000000u, 0x1F801078u,
                                       0x08000000u | ((uint32_t)&marked & 0x1FFFFFu) };

    const uint32_t reg = read32(BIOS_REG);
    for (int o = 0; o < NOFF; o++) romWord[o] = read32(0xBFC00000u + (offs[o] & 0x7FFFFu));

    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    volatile uint32_t *vec = (volatile uint32_t *)0x80000080u;
    uint32_t saved[4];
    for (int i = 0; i < 4; i++) { saved[i] = vec[i]; vec[i] = ((const uint32_t *)bw_vector)[i]; }
    flushCache();

    for (int s = 0; s < NSIZE; s++) {
        write32(BIOS_REG, (reg & ~0x001F0000u) | ((uint32_t)sizes[s] << 16));
        wReg[s] = read32(BIOS_REG);                            /* does the register hold the size it was given? */
        for (int o = 0; o < NOFF; o++) {
            bw_timed(0xBFC00000u + offs[o]);                  /* the second call runs from the I-cache */
            bw_epc = 0; bw_cause = 0;
            wCyc[s][o] = bw_timed(0xBFC00000u + offs[o]);
            wVal[s][o] = bw_value; wEpc[s][o] = bw_epc; wCause[s][o] = bw_cause;
        }
    }
    write32(BIOS_REG, (reg & ~0x001F0000u) | (0x13u << 16));   /* BFC80000h faults only past a 512 KB window */
    for (int k = 0; k < NKIND; k++)
        for (int o = 0; o < 4; o++)
            for (int m = 0; m < 3; m++) {
                if (m == 0) flushCache();
                const Case f = m == 2 ? uncached(kindFn[k][o]) : kindFn[k][o];
                bw_epc = 0; bw_cause = 0; bw_value = 0;
                lR10[k][o][m] = f(kindAddr[k]);
                lEpc[k][o][m] = bw_epc; lCause[k][o][m] = bw_cause; lVal[k][o][m] = bw_value;
            }
    write32(BIOS_REG, reg);
    for (int i = 0; i < 4; i++) vec[i] = saved[i];
    flushCache();
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("1F801010h (BIOS ROM Delay/Size) read %08x: window size %02xh.\n", (unsigned)reg, (unsigned)(reg >> 16) & 0x1F);
    for (int s = 0; s < NSIZE; s++) {
        printf("\nsize %02xh (%d KB), 1F801010h reads %08x:\n", sizes[s], 1 << (sizes[s] - 10), (unsigned)wReg[s]);
        for (int o = 0; o < NOFF; o++) {
            printf("  %08x  ", (unsigned)(0xBFC00000u + offs[o]));
            if (wEpc[s][o]) {
                printf("BUS ERROR  EPC %+d  CAUSE %08x  reg %08x", opcodesPast(wEpc[s][o], bw_timed_ld),
                       (unsigned)(wCause[s][o] & ~0xFF00u), (unsigned)wVal[s][o]);
            } else {
                printf("%08x", (unsigned)wVal[s][o]);
                if (offs[o] >= 0x80000) {
                    if (wVal[s][o] == romWord[o]) printf("  = ROM +%05x, a mirror", (unsigned)(offs[o] & 0x7FFFF));
                    else if (wVal[s][o] == 0xFFFFFFFFu) printf("  all ones");
                    else if (wVal[s][o] == 0) printf("  zero");
                    else printf("  not the ROM (+%05x reads %08x)", (unsigned)(offs[o] & 0x7FFFF), (unsigned)romWord[o]);
                } else if (wVal[s][o] != romWord[o]) printf("  not the ROM word %08x", (unsigned)romWord[o]);
            }
            printf("  %4d cycles\n", (int)wCyc[s][o]);
        }
    }

    printf("\nWhere a bus error lands (DIAG's method): the access at word 0-3 of an I-cache line, then eight\n");
    printf("ORI/XORI setting bits 0-7; the handler shifts the bits up 16 and returns to EPC+4. 'before' = bits\n");
    printf("set before the exception, 'after' = after it; EPC in opcodes past the access.\n");
    printf("access               word  mode      EPC  before after  CAUSE     reg\n");
    for (int k = 0; k < NKIND; k++)
        for (int o = 0; o < 4; o++)
            for (int m = 0; m < 3; m++) {
                const uint32_t r = lR10[k][o][m];
                printf("%-20s %d     %-8s  ", kindName[k], o, modeName[m]);
                if (lEpc[k][o][m]) printf("%+3d", opcodesPast(lEpc[k][o][m], kindLd[k][o]));
                else printf("  -");
                printf("  %02x     %02x     %08x  ", (unsigned)(r >> 16) & 0xFF, (unsigned)r & 0xFF,
                       (unsigned)(lCause[k][o][m] & ~0xFF00u));
                if (k == 3 || k == 4) printf("-\n");
                else printf("%08x\n", (unsigned)lVal[k][o][m]);
            }
    printf("\nThe marked RAM word at %08x (where a mirror of the stores at 08xxxxxxh lands): %08x (was 55aa6677)\n",
           (unsigned)&marked, (unsigned)marked);
    printf("\ncpu/bios-window: Done.\n");
    for (;;) VSync(0);
}
